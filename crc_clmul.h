/*
 * CRC32 (PKZIP) and CRC64 (XZ) using SSE4.1 and the CLMUL instruction.
 * And the same ported to AArch64.
 *
 * Derived from 'github.com/rawrunprotected/crc' and
 * 'www.intel.com/content/dam/www/public/us/en/documents/white-papers/
 * fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf'
 */

#include <stddef.h>
#include <stdint.h>

#if 0
static uint64_t calc_lo(uint64_t p, uint64_t a, int n) {
	uint64_t b = 0; int i;
	for (i = 0; i < n; i++) {
		b = b >> 1 | (a & 1) << (n - 1);
		a = (a >> 1) ^ ((0 - (a & 1)) & p);
	}
	return b;
}

/* same as ~crc(&a, sizeof(a), ~0) */
static uint64_t calc_hi(uint64_t p, uint64_t a, int n) {
	int i;
	for (i = 0; i < n; i++)
		a = (a >> 1) ^ ((0 - (a & 1)) & p);
	return a;
}
#endif

#ifdef __aarch64__
#include <arm_neon.h>

#define MASK_L(in, mask, r) r = vqtbl1q_u8(in, mask);
#define MASK_H(in, mask, r) \
	r = vqtbl1q_u8(in, veorq_u8(mask, vinv));
#define MASK_LH(in, mask, low, high) \
	MASK_L(in, mask, low) MASK_H(in, mask, high)

#define PMULL1(a, x, b, y) \
	vreinterpretq_u8_p128(vmull_p64( \
			(poly64_t)vreinterpret_p64_u8(vget_##x##_u8(a)), \
			(poly64_t)vreinterpret_p64_u8(vget_##y##_u8(b))))

#define PMULL2(a, b) \
	vreinterpretq_u8_p128(vmull_high_p64( \
			vreinterpretq_p64_u8(a), vreinterpretq_p64_u8(b)))

#define FOLD \
	v1 = veorq_u8(v1, PMULL1(v0, low, vfold16, low)); \
	v0 = veorq_u8(v1, PMULL2(v0, vfold16));

#define CRC_SIMD_BODY \
	uintptr_t skipS = (uintptr_t)data & 15; \
	uintptr_t skipE = -(uintptr_t)(data + length) & 15; \
	uint8x16_t vramp = vcombine_u8(vcreate_u8(0x0706050403020100), vcreate_u8(0x0f0e0d0c0b0a0908)); \
	uint8x16_t vinv = vdupq_n_u8(0xf0); \
	uint8x16_t maskS = vsubq_u8(vramp, vdupq_n_u8(skipS)); \
	uint8x16_t maskE = vsubq_u8(vramp, vdupq_n_u8(skipE)); \
	\
	uintptr_t length2 = skipS + length; \
	const uint8_t *adata = (const uint8_t*)((uintptr_t)data & -16); \
	uint8x16_t v0, v1, v2, v3, vcrc, data0; \
	\
	vcrc = vcombine_u8(vcreate_u8(~crc), vcreate_u8(0)); \
	if (!length) return crc; \
	data0 = vld1q_u8(adata); \
	data0 = vandq_u8(data0, vcgeq_s8(vreinterpretq_s8_u8(maskS), vdupq_n_s8(0))); \
	adata += 16; \
	if (length2 <= 16) { \
		uint8x16_t maskL = vaddq_u8(vramp, vdupq_n_u8(length - 16)); \
		MASK_LH(vcrc, maskL, v0, v1) \
		MASK_L(data0, maskE, v3) \
		v0 = veorq_u8(v0, v3); \
		v1 = vextq_u8(v0, v1, 8); \
	} else { \
		uint8x16_t data1 = vld1q_u8(adata); \
		if (length <= 16) { \
			uint8x16_t maskL = vaddq_u8(vramp, vdupq_n_u8(length - 16)); \
			MASK_LH(vcrc, maskL, v0, v1); \
			MASK_H(data0, maskE, v2) \
			MASK_L(data1, maskE, v3) \
			v0 = veorq_u8(v0, v2); \
			v0 = veorq_u8(v0, v3); \
			v1 = vextq_u8(v0, v1, 8); \
		} else { \
			MASK_LH(vcrc, maskS, v0, v1) \
			v0 = veorq_u8(v0, data0); \
			v1 = veorq_u8(v1, data1); \
			while (length2 > 32) { \
				adata += 16; \
				length2 -= 16; \
				FOLD \
				v1 = vld1q_u8(adata); \
			} \
			if (length2 < 32) { \
				MASK_H(v0, maskE, v2) \
				MASK_L(v0, maskE, v0) \
				MASK_L(v1, maskE, v3) \
				v1 = vorrq_u8(v2, v3); \
			} \
			FOLD \
			v1 = vextq_u8(v0, vdupq_n_u8(0), 8); \
		} \
	}

uint32_t crc32_clmul(const uint8_t *data, size_t length, uint32_t crc) {
	// uint32_t p = 0xedb88320;
	uint64_t i0 = 0x1db710640; // p << 1
	uint64_t i1 = 0x1f7011641; // calc_lo(p, p, 32) << 1 | 1
	uint64_t i2 = 0x163cd6124; // calc_hi(p, p, 32) << 1
	uint64_t i3 = 0x0ccaa009e; // calc_hi(p, p, 64) << 1
	uint64_t i4 = 0x1751997d0; // calc_hi(p, p, 128) << 1

	uint8x16_t vfold4 = vcombine_u8(vcreate_u8(i0), vcreate_u8(i1));
	uint8x16_t vfold8 = vcombine_u8(vcreate_u8(i2), vcreate_u8(0));
	uint8x16_t vfold16 = vcombine_u8(vcreate_u8(i4), vcreate_u8(i3));

	CRC_SIMD_BODY

	v1 = veorq_u8(PMULL1(v0, low, vfold16, high), v1); // xxx0
	v2 = vreinterpretq_u8_u32(vsetq_lane_u32(0, vreinterpretq_u32_u8(v1), 0)); // 0xx0
	v0 = vreinterpretq_u8_u64(vshlq_n_u64(vreinterpretq_u64_u8(v1), 32));  // [0]
	v0 = PMULL1(v0, low, vfold8, low);
	v0 = veorq_u8(v0, v2);   // [1] [2]
	v2 = PMULL1(v0, low, vfold4, high);
	v2 = PMULL1(v2, low, vfold4, low);
	v0 = veorq_u8(v0, v2);   // [2]
	return ~vgetq_lane_u32(vreinterpretq_u32_u8(v0), 2);
}


uint64_t crc64_clmul(const uint8_t *data, size_t length, uint64_t crc) {
	// uint64_t p = 0xc96c5795d7870f42;
	uint64_t i0 = 0x92d8af2baf0e1e84; // p << 1
	uint64_t i1 = 0x9c3e466c172963d5; // calc_lo(p, 1, 64)
	uint64_t i2 = 0xdabe95afc7875f40; // calc_hi(p, 1, 64)
	uint64_t i3 = 0xe05dd497ca393ae4; // calc_hi(p, i2, 64)

	uint8x16_t vfold8 = vcombine_u8(vcreate_u8(i1), vcreate_u8(i0));
	uint8x16_t vfold16 = vcombine_u8(vcreate_u8(i3), vcreate_u8(i2));

	CRC_SIMD_BODY

	v1 = veorq_u8(PMULL1(v0, low, vfold16, high), v1);
	v0 = PMULL1(v1, low, vfold8, low);
	v2 = PMULL1(v0, low, vfold8, high);
	v0 = vextq_u8(vdupq_n_u8(0), v0, 8);
	v0 = veorq_u8(veorq_u8(v1, v0), v2);
	return ~vgetq_lane_u64(vreinterpretq_u64_u8(v0), 1);
}

#undef CRC_SIMD_BODY
#undef FOLD

#elif defined(__SSE4_1__) && defined(__PCLMUL__)
#include <smmintrin.h>
#include <wmmintrin.h>

#define MASK_L(in, mask, r) r = _mm_shuffle_epi8(in, mask);
#define MASK_H(in, mask, r) \
	r = _mm_shuffle_epi8(in, _mm_xor_si128(mask, vsign));
#define MASK_LH(in, mask, low, high) \
	MASK_L(in, mask, low) MASK_H(in, mask, high)

#define FOLD \
	v1 = _mm_xor_si128(v1, _mm_clmulepi64_si128(v0, vfold16, 0x00)); \
	v0 = _mm_xor_si128(v1, _mm_clmulepi64_si128(v0, vfold16, 0x11));

#define CRC_SIMD_BODY(crc2vec) \
	uintptr_t skipS = (uintptr_t)data & 15; \
	uintptr_t skipE = -(uintptr_t)(data + length) & 15; \
	__m128i vramp = _mm_setr_epi32(0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c); \
	__m128i vsign = _mm_set1_epi8(-0x80); \
	__m128i maskS = _mm_sub_epi8(vramp, _mm_set1_epi8(skipS)); \
	__m128i maskE = _mm_sub_epi8(vramp, _mm_set1_epi8(skipE)); \
	\
	uintptr_t length2 = skipS + length; \
	const __m128i *adata = (const __m128i*)((uintptr_t)data & -16); \
	__m128i v0, v1, v2, v3, vcrc, data0; \
	\
	vcrc = crc2vec; \
	if (!length) return crc; \
	data0 = _mm_load_si128(adata); \
	data0 = _mm_blendv_epi8(data0, _mm_setzero_si128(), maskS); \
	adata++; \
	if (length2 <= 16) { \
		__m128i maskL = _mm_add_epi8(vramp, _mm_set1_epi8(length - 16)); \
		MASK_LH(vcrc, maskL, v0, v1) \
		MASK_L(data0, maskE, v3) \
		v0 = _mm_xor_si128(v0, v3); \
		v1 = _mm_alignr_epi8(v1, v0, 8); \
	} else { \
		__m128i data1 = _mm_load_si128(adata); \
		if (length <= 16) { \
			__m128i maskL = _mm_add_epi8(vramp, _mm_set1_epi8(length - 16)); \
			MASK_LH(vcrc, maskL, v0, v1); \
			MASK_H(data0, maskE, v2) \
			MASK_L(data1, maskE, v3) \
			v0 = _mm_xor_si128(v0, v2); \
			v0 = _mm_xor_si128(v0, v3); \
			v1 = _mm_alignr_epi8(v1, v0, 8); \
		} else { \
			MASK_LH(vcrc, maskS, v0, v1) \
			v0 = _mm_xor_si128(v0, data0); \
			v1 = _mm_xor_si128(v1, data1); \
			while (length2 > 32) { \
				adata++; \
				length2 -= 16; \
				FOLD \
				v1 = _mm_load_si128(adata); \
			} \
			if (length2 < 32) { \
				MASK_H(v0, maskE, v2) \
				MASK_L(v0, maskE, v0) \
				MASK_L(v1, maskE, v3) \
				v1 = _mm_or_si128(v2, v3); \
			} \
			FOLD \
			v1 = _mm_srli_si128(v0, 8); \
		} \
	}

uint32_t crc32_clmul(const uint8_t *data, size_t length, uint32_t crc) {
	// uint32_t p = 0xedb88320;
	uint64_t i0 = 0x1db710640; // p << 1
	uint64_t i1 = 0x1f7011641; // calc_lo(p, p, 32) << 1 | 1
	uint64_t i2 = 0x163cd6124; // calc_hi(p, p, 32) << 1
	uint64_t i3 = 0x0ccaa009e; // calc_hi(p, p, 64) << 1
	uint64_t i4 = 0x1751997d0; // calc_hi(p, p, 128) << 1

	__m128i vfold4 = _mm_set_epi64x(i1, i0);
	__m128i vfold8 = _mm_set_epi64x(0, i2);
	__m128i vfold16 = _mm_set_epi64x(i3, i4);

	CRC_SIMD_BODY(_mm_cvtsi32_si128(~crc))

	v1 = _mm_xor_si128(_mm_clmulepi64_si128(v0, vfold16, 0x10), v1); // xxx0
	v2 = _mm_shuffle_epi32(v1, 0xe7); // 0xx0
	v0 = _mm_slli_epi64(v1, 32);  // [0]
	v0 = _mm_clmulepi64_si128(v0, vfold8, 0x00);
	v0 = _mm_xor_si128(v0, v2);   // [1] [2]
	v2 = _mm_clmulepi64_si128(v0, vfold4, 0x10);
	v2 = _mm_clmulepi64_si128(v2, vfold4, 0x00);
	v0 = _mm_xor_si128(v0, v2);   // [2]
	return ~_mm_extract_epi32(v0, 2);
}

uint64_t crc64_clmul(const uint8_t *data, size_t length, uint64_t crc) {
	// uint64_t p = 0xc96c5795d7870f42;
	uint64_t i0 = 0x92d8af2baf0e1e84; // p << 1
	uint64_t i1 = 0x9c3e466c172963d5; // calc_lo(p, 1, 64)
	uint64_t i2 = 0xdabe95afc7875f40; // calc_hi(p, 1, 64)
	uint64_t i3 = 0xe05dd497ca393ae4; // calc_hi(p, i2, 64)
	__m128i vfold8 = _mm_set_epi64x(i0, i1);
	__m128i vfold16 = _mm_set_epi64x(i2, i3);

#ifdef __i386__
	CRC_SIMD_BODY(_mm_set_epi64x(0, ~crc))
#else
	CRC_SIMD_BODY(_mm_cvtsi64_si128(~crc))
#endif

	v1 = _mm_xor_si128(_mm_clmulepi64_si128(v0, vfold16, 0x10), v1);
	v0 = _mm_clmulepi64_si128(v1, vfold8, 0x00);
	v2 = _mm_clmulepi64_si128(v0, vfold8, 0x10);
	v0 = _mm_xor_si128(_mm_xor_si128(v1, _mm_slli_si128(v0, 8)), v2);

#ifdef __i386__
	return ~(((uint64_t)(uint32_t)_mm_extract_epi32(v0, 3) << 32) |
			(uint64_t)(uint32_t)_mm_extract_epi32(v0, 2));
#else
	return ~_mm_extract_epi64(v0, 1);
#endif
}

#undef CRC_SIMD_BODY
#undef FOLD
#endif
