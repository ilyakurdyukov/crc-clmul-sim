/*
 * Copyright (C) 2022, Ilya Kurdyukov
 *
 * CRC32 (PKZIP) and CRC64 (XZ) using CLMUL simulation with shift and xor.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef CLMUL_SIM
#undef CLMUL_SIM
#endif

#if defined(CLSIM_HW) && defined(__e2k__) && __iset__ >= 6
#define clmul32(a, b) (uint32_t)__builtin_e2k_clmull(a, b)
#define clmul32_hi(a, b) (uint32_t)(__builtin_e2k_clmull(a, b) >> 32)
#define clmul64 __builtin_e2k_clmull
#define clmul64_hi __builtin_e2k_clmulh
#elif defined(CLSIM_HW) && defined(__aarch64__)
/* not recommended */
#include <arm_neon.h>
#define clmul32(a, b) (uint32_t)vmull_p64(a, b)
#define clmul32_hi(a, b) (uint32_t)(vmull_p64(a, b) >> 32)
#define clmul64(a, b) (uint64_t)vmull_p64(a, b)
#define clmul64_hi(a, b) (uint64_t)(vmull_p64(a, b) >> 64)
#elif defined(CLSIM_HW) && defined(__PCLMUL__)
/* not recommended */
#include <wmmintrin.h>
#ifdef __i386__
#define clmul64sse(a, b) _mm_clmulepi64_si128(_mm_set_epi64x(0, a), _mm_set_epi64x(0, b), 0)
static inline uint64_t clmul64(uint64_t a, uint64_t b) {
	__m128i x = clmul64sse(a, b);
	return (uint64_t)(uint32_t)_mm_extract_epi32(x, 1) << 32 |
			(uint32_t)_mm_cvtsi128_si32(x);
}
static inline uint64_t clmul64_hi(uint64_t a, uint64_t b) {
	__m128i x = clmul64sse(a, b);
	return (uint64_t)(uint32_t)_mm_extract_epi32(x, 3) << 32 |
			(uint32_t)_mm_extract_epi32(x, 2);
}
#else
#define clmul64sse(a, b) _mm_clmulepi64_si128(_mm_cvtsi64_si128(a), _mm_cvtsi64_si128(b), 0)
#define clmul64(a, b) (uint64_t)_mm_cvtsi128_si64(clmul64sse(a, b))
#define clmul64_hi(a, b) (uint64_t)(_mm_extract_epi64(clmul64sse(a, b), 1))
#endif
#define clmul32(a, b) (uint32_t)_mm_cvtsi128_si32(clmul64sse(a, b))
#define clmul32_hi(a, b) (uint32_t)_mm_extract_epi32(clmul64sse(a, b), 1)
#elif 0
static inline uint32_t clmul32(uint32_t a, uint32_t b) {
	int i; uint32_t c = 0;
	for (i = 0; i < 32; i++)
		c ^= b >> i & 1 ? a << i : 0;
	return c;
}

static inline uint32_t clmul32_hi(uint32_t a, uint32_t b) {
	int i; uint32_t c = 0;
	for (i = 1; i < 32; i++)
		c ^= b >> i & 1 ? a >> (32 - i) : 0;
	return c;
}

static inline uint64_t clmul64(uint64_t a, uint64_t b) {
	int i; uint64_t c = 0;
	for (i = 0; i < 64; i++)
		c ^= b >> i & 1 ? a << i : 0;
	return c;
}

static inline uint64_t clmul64_hi(uint64_t a, uint64_t b) {
	int i; uint64_t c = 0;
	for (i = 1; i < 64; i++)
		c ^= b >> i & 1 ? a >> (64 - i) : 0;
	return c;
}
#else
#define CLMUL_SIM
#endif

uint32_t crc32_clsim(const uint8_t *s, size_t n, uint32_t c) {
	uint32_t x, v;

#ifndef CLMUL_SIM
	// uint32_t p = 0xedb88320;
	uint32_t i0 = 0xdb710640; // p << 1
	uint32_t i1 = 0xf7011641; // calc_lo(p, 1, 32)
	uint32_t i2 = 0xb8bc6765; // calc_hi(p, 1, 32)
	uint32_t i3 = 0xccaa009e; // calc_hi(p, i2, 32)
#define FOLD1(v) \
	x = clmul32(v, i1); \
	c = clmul32_hi(x, i0);
#define FOLD2(o) \
	h o clmul64((uint32_t)f, i3);
#define FOLD3(x, o) \
	h o clmul64(x, i2);
#else
	uint32_t y, u; uint64_t g, tt, yy, uu;
#define FOLD1(v) \
	/* shift = 6, xor = 6 */ \
	y = v^v<<19; u = y^y<<6^y<<9; \
	x = u^y<<2; x = u^x<<10^x<<24; \
	/* shift = 7, xor = 6 */ \
	y = x>>1^x>>4; u = x>>16^y^y>>1; \
	c = x>>12^y>>22^u^u>>6;
#define FOLD2(o) \
	g = (uint32_t)f; \
	/* shift = 8, xor = 7(+1) */ \
	yy = g^g<<1; uu = yy^yy<<1^yy<<9; \
	h o g<<7^yy<<1^yy<<3^uu<<17^uu<<21;
#define FOLD3(x, o) \
	/* shift = 8, xor = 8(+1) */ \
	g = x; yy = g^g<<8; uu = yy^yy<<2; tt = yy^yy<<1; \
	h o g<<9^g<<18^uu^uu<<21^tt<<5^tt<<19;
#endif

	uintptr_t r = (uintptr_t)s & 7;
	const uint64_t *b;
	uint64_t f, h;
	if (!n) return c;
	c = ~c;
	b = (const uint64_t*)((uintptr_t)s & -8);
	if (n <= 8) {
		f = c; n <<= 3; v = f >> 8 >> (n - 8);
		if (r + n < 8)
			f = (f ^ *b >> r * 8) << (64 - n);
		else
			f = (f ^ *(const uint64_t*)s) << (64 - n);
	} else {
		n += r;
		h = c;
		f = (h ^ *b++ >> r * 8) << (r * 8);
		h = h >> 8 >> (56 - r * 8);
		for (; n > 16; n -= 8) {
			FOLD2(^=)
			FOLD3(f >> 32, ^=)
			f = h ^ *b++;
			h = 0;
		}
		h ^= *b++;
		n <<= 3;
		h <<= 64*2 - n;
		h ^= f >> 8 >> (n - 8 - 64);
		f <<= 64*2 - n;
		FOLD2(^=)
		FOLD3(f >> 32, ^=)
		f = h;
		v = 0;
	}
	h = f >> 32;
	FOLD3((uint32_t)f, ^=)
	c = h; v ^= h >> 32;
	FOLD1(c)
	c ^= x ^ v;
	return ~c;
#undef FOLD1
#undef FOLD2
#undef FOLD3
}

uint64_t crc64_clsim(const uint8_t *s, size_t n, uint64_t c) {
	uint64_t x, v, w; const uint64_t *a;

#ifndef CLMUL_SIM
	// uint64_t p = 0xc96c5795d7870f42;
	uint64_t i0 = 0x92d8af2baf0e1e84; // p << 1
	uint64_t i1 = 0x9c3e466c172963d5; // calc_lo(p, 1, 64)
	uint64_t i2 = 0xdabe95afc7875f40; // calc_hi(p, 1, 64)
	uint64_t i3 = 0xe05dd497ca393ae4; // calc_hi(p, i2, 64)
#define FOLD1(v) \
	x = clmul64(v, i1); \
	c = clmul64_hi(x, i0);
#define FOLD2 \
	v = clmul64(c, i3); \
	w = clmul64_hi(c, i3);
#define FOLD3(x, o) \
	v o clmul64(x, i2); \
	w o clmul64_hi(x, i2);
#else
	uint64_t t, y, u;

#define FOLD1(v) \
	/* shift = 11, xor = 12 */ \
	t = v^v<<4; u = t^v<<22; x = t<<2; \
	y = v^v<<6^v<<30^v<<44; \
	t = y<<7; y ^= t^y<<11; u ^= y<<8; \
	x ^= t^y<<9^u^u<<20; \
	/* shift = 12, xor = 11 */ \
	c = x^x>>18; y = c^x>>3^x>>16; \
	u = y^y>>20^y>>26^y>>45; \
	c = x>>36^u>>1^u>>7^c>>13^u>>9^c>>22;

#define FOLD2 \
	/* shift = 9, xor = 11 */ \
	v = c^c<<4; y = v^c<<31; \
	u = y<<12; u = u^(y^u)<<23; v = (v^u)<<7; \
	v = c<<5^c<<24^v^(y^v)<<2^u^u<<8; \
	/* shift = 12, xor = 13 */ \
	y = c^c>>10^c>>14; w = y>>2^y>>48; \
	u = y^y>>3^y>>25; \
	y = c^c>>1^c>>23; w ^= u^u>>18^u>>20; \
	w ^= y^y>>21^y>>51;

#define FOLD3(x, o) \
	/* shift = 10, xor = 10(+1) */ \
	t = x^x<<6; y = t^x<<1; \
	u = y<<10; v o t<<6^u; u ^= y<<1; \
	u = y<<25^u^u<<34; t ^= u; \
	v ^= u<<7^t<<24^t<<37; \
	/* shift = 13, xor = 12(+1) */ \
	y = x^x>>2^x>>28; u = y^y>>9; \
	u = y>>24^u>>1^u>>7^u>>17; \
	y = x^x>>14; u ^= y>>2^y>>19^y>>36; \
	u ^= u>>8; w o u^u>>2;
#endif

	uintptr_t r = (uintptr_t)s & 7, q;
	if (!n) return c;
	c = ~c;
	a = (const uint64_t*)((uintptr_t)s & -8);
	if (r) {
		n -= q = n >= 8 - r ? 8 - r : n;
		q <<= 3; v = c >> q;
		c = (c ^ *a++ >> r * 8) << (64 - q);
		FOLD1(c) c ^= x ^ v;
	}

#if 1
	if (n >= 16) {
		c ^= a[0];
		x = a[1];
		a += 2; n -= 16;
		while (n >= 16) {
			FOLD2
			FOLD3(x, ^=)
			c = a[0] ^ v;
			x = a[1] ^ w;
			a += 2; n -= 16;
		}
		FOLD3(c, =)
		v ^= x;
		FOLD1(v)
		c ^= x ^ w;
	}
	if (n >= 8) {
		c ^= *a++; FOLD1(c)
		c ^= x; n -= 8;
	}
#else
	for (; n >= 8; n -= 8, c ^= x) {
		c ^= *a++; FOLD1(c)
	}
#endif

	if (n) {
		n <<= 3; v = c >> n;
		c = (c ^ *a++) << (64 - n);
		FOLD1(c) c ^= x ^ v;
	}
#undef FOLD1
#undef FOLD2
#undef FOLD3
	return ~c;
}

