#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef USE_PERFCNT
#define USE_PERFCNT 0
#endif

#ifndef WITH_CYCLES
#define WITH_CYCLES 1
#endif

#ifdef _GNU_SOURCE
#include <time.h>
#define TIME_DEF uint64_t time = 0; struct timespec ts0, ts1;
#define TIME_GET clock_gettime(CLOCK_MONOTONIC, &ts0);
#define TIME_DIFF clock_gettime(CLOCK_MONOTONIC, &ts1); \
	time += (ts1.tv_sec - ts0.tv_sec) * 1000000000 + (ts1.tv_nsec - ts0.tv_nsec);
#define TIME_TO_MS 1e-6
#define TIME_FROM_GHZ 1.0
#else
#include <time.h>
#include <sys/time.h>
#define TIME_DEF uint64_t time = 0; struct timeval tv0, tv1;
#define TIME_GET gettimeofday(&tv0, NULL);
#define TIME_DIFF gettimeofday(&tv1, NULL); \
	time += (tv1.tv_sec - tv0.tv_sec) * 1000000 + (tv1.tv_usec - tv0.tv_usec);
#define TIME_TO_MS 1e-3
#define TIME_FROM_GHZ 1e-3
#endif

#if WITH_CYCLES && !USE_PERFCNT
#if defined(__i386__) || defined(__x86_64__) || defined(__e2k__)
#include <x86intrin.h>
static inline uint64_t get_cycles() { return __rdtsc(); }
#else
#undef WITH_CYCLES
#define WITH_CYCLES 0
#endif
#endif

#if USE_PERFCNT
#include "perfcnt.h"
#elif WITH_CYCLES
#define TIMER_DEF TIME_DEF \
	size_t len1; uint64_t cycles = 0;

#define TIMER_INIT len1 = len;

#define TIMER_START TIME_GET \
	cycles -= get_cycles();

#define TIMER_STOP \
	cycles += get_cycles(); TIME_DIFF

#define TIMER_PRINT \
	printf(" %s: %.3fms", type, time * TIME_TO_MS); \
	printf(", %.3f cycles/byte (%.3f GHz)", \
			1.0 * (int64_t)cycles / len1, \
			TIME_FROM_GHZ * (int64_t)cycles / (int64_t)time);
#else
#define TIMER_DEF TIME_DEF
#define TIMER_INIT
#define TIMER_START TIME_GET
#define TIMER_STOP TIME_DIFF
#define TIMER_PRINT \
	printf(" %s: %.3fms", type, time * TIME_TO_MS);
#endif

#include "crc_slice.h"
#include "crc_clsim.h"

#ifndef HAVE_CLMUL
#if ((defined(__SSE4_1__) && defined(__PCLMUL__)) || defined(__aarch64__)) \
		&& !(defined(__e2k__) && __iset__ < 6)
#define HAVE_CLMUL 1
#else
#define HAVE_CLMUL 0
#endif
#endif

#if HAVE_CLMUL
#include "crc_clmul.h"
#define CLSIM_HW
#define crc32_clsim crc32_clmul2
#define crc64_clsim crc64_clmul2
#include "crc_clsim.h"
#undef crc32_clsim
#undef crc64_clsim
#endif

#define POLY32 0xedb88320
#define POLY64 0xc96c5795d7870f42

uint32_t crc32_micro(const uint8_t *s, size_t n, uint32_t c) {
	int j;
	for (c = ~c; n--;)
	for (c ^= *s++, j = 8; j--;)
		c = c >> 1 ^ ((0 - (c & 1)) & POLY32);
	return ~c;
}

uint64_t crc64_micro(const uint8_t *s, size_t n, uint64_t c) {
	int j;
	for (c = ~c; n--;)
	for (c ^= *s++, j = 8; j--;)
		c = c >> 1 ^ ((0 - (c & 1)) & POLY64);
	return ~c;
}

static uint64_t crc32_table[256];

void crc32_simple_init(void) {
	int i, j; uint32_t a, p = POLY32;
	for (i = 0; i < 256; i++) {
		for (a = i, j = 0; j < 8; j++)
			a = a >> 1 ^ (((a & 1) - 1) & p);
		crc32_table[i] = a ^ ~(uint32_t)0 << 24;
	}
}

uint32_t crc32_simple(const uint8_t *s, size_t n, uint32_t crc) {
	size_t i;
	for (i = 0; i < n; i++)
		crc = crc32_table[s[i] ^ (crc & 0xff)] ^ crc >> 8;
	return crc;
}

static uint64_t crc64_table[256];

static void crc64_simple_init(void) {
	int i, j; uint64_t a, p = POLY64;
	for (i = 0; i < 256; i++) {
		for (a = i, j = 0; j < 8; j++)
			a = a >> 1 ^ (((a & 1) - 1) & p);
		crc64_table[i] = a ^ ~(uint64_t)0 << 56;
	}
}

uint64_t crc64_simple(const uint8_t *s, size_t n, uint64_t crc) {
	size_t i;
	for (i = 0; i < n; i++)
		crc = crc64_table[s[i] ^ (crc & 0xff)] ^ crc >> 8;
	return crc;
}

static inline uint32_t crc32_multmodp(uint32_t p, uint32_t a, uint32_t b) {
	uint32_t x = 0;
	do {
		x ^= b & (int32_t)a >> 31;
		b = b >> 1 ^ ((0 - (b & 1)) & p);
	} while ((a <<= 1));
	return x;
}

// M##i = calc_hi(POLY, POLY, i * N * 8 - 32)

#define CRC32_PARALLEL4(fn, T, N, POLY, M1, M2, M3) \
	for (; n >= N*4; n -= N*4) { \
		const uint8_t *e = s + N; \
		uint32_t c1 = ~0, c2 = ~0, c3 = ~0; \
		for (; s < e; s += sizeof(T)) { \
			c = fn(c, *(T*)s); \
			c1 = fn(c1, *(T*)(s + N)); \
			c2 = fn(c2, *(T*)(s + N * 2)); \
			c3 = fn(c3, *(T*)(s + N * 3)); \
		} \
		c = crc32_multmodp(POLY, M3, ~c); \
		c1 = crc32_multmodp(POLY, M2, ~c1); \
		c2 = crc32_multmodp(POLY, M1, ~c2); \
		c ^= c1 ^ c2 ^ c3; s += N * 3; \
	}

#ifdef __ARM_FEATURE_CRC32
#include <arm_acle.h>
uint32_t crc32_arm(const uint8_t *s, size_t n, uint32_t c) {
	c = ~c;
#ifdef __aarch64__
	if (n >= 8) {
		const uint8_t *e;
		uintptr_t x = -(uintptr_t)s & 7;
		if (x & 1) c = __crc32b(c, *s);
		if (x & 2) c = __crc32h(c, *(uint16_t*)(s + (x & 1)));
		if (x & 4) c = __crc32w(c, *(uint32_t*)(s + (x & 3)));
		n -= x; s += x;
		e = s + n - 7;
		for (; s < e; s += 8) c = __crc32d(c, *(uint64_t*)s);
		if (n & 4) c = __crc32w(c, *(uint32_t*)s);
		if (n & 2) c = __crc32h(c, *(uint16_t*)(s + (n & 4)));
		if (n & 1) c = __crc32b(c, s[n & 6]);
	}
#else
	if (n >= 4) {
		const uint8_t *e;
		uintptr_t x = -(uintptr_t)s & 3;
		if (x & 1) c = __crc32b(c, *s);
		if (x & 2) c = __crc32h(c, *(uint16_t*)(s + (x & 1)));
		n -= x; s += x;
		e = s + n - 3;
		for (; s < e; s += 4) c = __crc32w(c, *(uint32_t*)s);
		if (n & 2) c = __crc32h(c, *(uint16_t*)s);
		if (n & 1) c = __crc32b(c, s[n & 2]);
	}
#endif
	else
		for (; n; n--) c = __crc32b(c, *s++);
	return ~c;
}

uint32_t crc32_arm_long(const uint8_t *s, size_t n, uint32_t c) {
	c = ~c;
#ifdef CRC32_PARALLEL4
#ifdef __aarch64__
	CRC32_PARALLEL4(__crc32d, uint64_t, 4096,
			0xedb88320, 0x09fe548f, 0x83852d0f, 0xe4b54665)
#else
	CRC32_PARALLEL4(__crc32w, uint32_t, 4096,
			0xedb88320, 0x09fe548f, 0x83852d0f, 0xe4b54665)
#endif
#endif
	return crc32_arm(s, n, ~c);
}
#endif

#ifdef __SSE4_2__
#include <nmmintrin.h>
uint32_t crc32_micro2(const uint8_t *s, size_t n, uint32_t c) {
	int j;
	for (c = ~c; n--;)
	for (c ^= *s++, j = 8; j--;)
		c = c >> 1 ^ ((0 - (c & 1)) & 0x82f63b78);
	return ~c;
}

uint32_t crc32_intel(const uint8_t *s, size_t n, uint32_t c) {
	c = ~c;
#ifndef __i386__
	if (n >= 8) {
		const uint8_t *e;
		uintptr_t x = -(uintptr_t)s & 7;
		if (x & 1) c = _mm_crc32_u8(c, *s);
		if (x & 2) c = _mm_crc32_u16(c, *(uint16_t*)(s + (x & 1)));
		if (x & 4) c = _mm_crc32_u32(c, *(uint32_t*)(s + (x & 3)));
		n -= x; s += x;
		e = s + n - 7;
		// Avoid redundant zero extensions in the loop
		// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=106453
		{
			uint64_t xc = c;
			for (; s < e; s += 8) xc = _mm_crc32_u64(xc, *(uint64_t*)s);
			c = xc;
		}
		if (n & 4) c = _mm_crc32_u32(c, *(uint32_t*)s);
		if (n & 2) c = _mm_crc32_u16(c, *(uint16_t*)(s + (n & 4)));
		if (n & 1) c = _mm_crc32_u8(c, s[n & 6]);
	}
#else
	if (n >= 4) {
		const uint8_t *e;
		uintptr_t x = -(uintptr_t)s & 3;
		if (x & 1) c = _mm_crc32_u8(c, *s);
		if (x & 2) c = _mm_crc32_u16(c, *(uint16_t*)(s + (x & 1)));
		n -= x; s += x;
		e = s + n - 3;
		for (; s < e; s += 4) c = _mm_crc32_u32(c, *(uint32_t*)s);
		if (n & 2) c = _mm_crc32_u16(c, *(uint16_t*)s);
		if (n & 1) c = _mm_crc32_u8(c, s[n & 2]);
	}
#endif
	else
		for (; n; n--) c = _mm_crc32_u8(c, *s++);
	return ~c;
}

uint32_t crc32_intel_long(const uint8_t *s, size_t n, uint32_t c) {
	c = ~c;
#ifdef CRC32_PARALLEL4
#ifndef __i386__
	CRC32_PARALLEL4(_mm_crc32_u64, uint64_t, 4096,
			0x82f63b78, 0x35d73a62, 0x28461564, 0x43eefc9f)
#else
	CRC32_PARALLEL4(_mm_crc32_u32, uint32_t, 4096,
			0x82f63b78, 0x35d73a62, 0x28461564, 0x43eefc9f)
#endif
#endif
	return crc32_intel(s, n, ~c);
}

static int crc32_check2(uint32_t (*crc32_fn)(const uint8_t*, size_t, uint32_t)) {
	uint8_t buf1[64+15*2], *buf;
	int i, j, n = 64;
	uint32_t init = 0x01234567, crc1, crc2;

	buf = (uint8_t*)(((uintptr_t)buf1 + 15) & -16);
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x76543210u >> 24;

	for (i = 0; i < 16; i++)
	for (j = 0; j < n; j++) {
		crc1 = crc32_micro2(buf + i, j, init);
		crc2 = crc32_fn(buf + i, j, init);
		if (crc1 != crc2) {
			printf("!!! mismatch at (s=%i,n=%i)\n", i, j);
			return 1;
		}
	}
	return 0;
}
#endif

static int crc32_check(uint32_t (*crc32_fn)(const uint8_t*, size_t, uint32_t)) {
	uint8_t buf1[64+15*2], *buf;
	int i, j, n = 64;
	uint32_t init = 0x01234567, crc1, crc2;

	buf = (uint8_t*)(((uintptr_t)buf1 + 15) & -16);
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x76543210u >> 24;

	for (i = 0; i < 16; i++)
	for (j = 0; j < n; j++) {
		crc1 = crc32_micro(buf + i, j, init);
		crc2 = crc32_fn(buf + i, j, init);
		if (crc1 != crc2) {
			printf("!!! mismatch at (s=%i,n=%i)\n", i, j);
			return 1;
		}
	}
	return 0;
}

static int crc64_check(uint64_t (*crc64_fn)(const uint8_t*, size_t, uint64_t)) {
	uint8_t buf1[64+15*2], *buf;
	int i, j, n = 64;
	uint64_t init = 0x0123456789abcdef, crc1, crc2;

	buf = (uint8_t*)(((uintptr_t)buf1 + 15) & -16);
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x76543210u >> 24;

	for (i = 0; i < 16; i++)
	for (j = 0; j < n; j++) {
		crc1 = crc64_micro(buf + i, j, init);
		crc2 = crc64_fn(buf + i, j, init);
		if (crc1 != crc2) {
			printf("!!! mismatch at (s=%i,n=%i)\n", i, j);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	uint32_t (*crc32_fn)(const uint8_t*, size_t, uint32_t) = NULL;
	uint64_t (*crc64_fn)(const uint8_t*, size_t, uint64_t) = NULL;
	int (*crc32_check_fn)(uint32_t (*crc32_fn)(const uint8_t*, size_t, uint32_t)) = crc32_check;
	uint8_t *buf;
	size_t n, len = 100 * 1000000, nbuf = 1 << 20;
	FILE *f = NULL;
	int verbose = 1;
	const char *type = "crc64_simple";
	TIMER_DEF

	while (argc > 1) {
		if (argc > 2 && !strcmp(argv[1], "-i")) {
			if (f) return 1;
			if (!strcmp(argv[2], "-")) f = stdin;
			else if (!(f = fopen(argv[2], "rb"))) return 1;
			argc -= 2; argv += 2;
		} else if (argc > 2 && !strcmp(argv[1], "-t")) {
			type = argv[2];
			argc -= 2; argv += 2;
		} else if (argc > 2 && !strcmp(argv[1], "-n")) {
			nbuf = atol(argv[2]);
			argc -= 2; argv += 2;
		} else if (argc > 2 && !strcmp(argv[1], "-l")) {
			len = atol(argv[2]);
			argc -= 2; argv += 2;
		} else if (argc > 2 && !strcmp(argv[1], "-v")) {
			verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else return 1;
	}

	if (!type) return 1;

	if (!strcmp(type, "crc64_micro")) {
		crc64_fn = crc64_micro;
	} else if (!strcmp(type, "crc64_simple")) {
		crc64_fn = crc64_simple; crc64_simple_init();
	} else if (!strcmp(type, "crc64_slice4")) {
		crc64_fn = crc64_slice4; crc64_slice4_init();
	} else if (!strcmp(type, "crc64_clsim")) {
		crc64_fn = crc64_clsim;
#if HAVE_CLMUL
	} else if (!strcmp(type, "crc64_clmul")) {
		crc64_fn = crc64_clmul;
#endif
#ifdef CLSIM_HW
	} else if (!strcmp(type, "crc64_clmul2")) {
		crc64_fn = crc64_clmul2;
#endif

	} else if (!strcmp(type, "crc32_micro")) {
		crc32_fn = crc32_micro;
	} else if (!strcmp(type, "crc32_simple")) {
		crc32_fn = crc32_simple; crc32_simple_init();
	} else if (!strcmp(type, "crc32_slice4")) {
		crc32_fn = crc32_slice4; crc32_slice4_init();
	} else if (!strcmp(type, "crc32_clsim")) {
		crc32_fn = crc32_clsim;
#if HAVE_CLMUL
	} else if (!strcmp(type, "crc32_clmul")) {
		crc32_fn = crc32_clmul;
#endif
#ifdef CLSIM_HW
	} else if (!strcmp(type, "crc32_clmul2")) {
		crc32_fn = crc32_clmul2;
#endif
#ifdef __ARM_FEATURE_CRC32
	} else if (!strcmp(type, "crc32_arm")) {
		crc32_fn = crc32_arm;
	} else if (!strcmp(type, "crc32_arm_long")) {
		crc32_fn = crc32_arm_long;
#endif
#ifdef __SSE4_2__
	} else if (!strcmp(type, "crc32_intel")) {
		crc32_fn = crc32_intel;
		crc32_check_fn = crc32_check2;
	} else if (!strcmp(type, "crc32_intel_long")) {
		crc32_fn = crc32_intel_long;
		crc32_check_fn = crc32_check2;
#endif

	} else return 1;

	TIMER_INIT

	buf = malloc(nbuf);
	if (!buf) return 2;

	if (!f)
		for (n = 0; n < nbuf; n++) buf[n] = n * 0x76543210u >> 24;

	if (crc64_fn) {
		uint64_t crc = 0;
		if (crc64_check(crc64_fn)) return 3;
		do {
			if (f) n = fread(buf, 1, nbuf, f);
			else len -= n = len > nbuf ? nbuf : len;
			if (!n) break;
			TIMER_START
			crc = crc64_fn(buf, n, crc);
			TIMER_STOP
		} while (n == nbuf);
		printf("%016llx", (long long)crc);
	} else {
		uint32_t crc = 0;
		if (crc32_check_fn(crc32_fn)) return 3;
		do {
			if (f) n = fread(buf, 1, nbuf, f);
			else len -= n = len > nbuf ? nbuf : len;
			if (!n) break;
			TIMER_START
			crc = crc32_fn(buf, n, crc);
			TIMER_STOP
		} while (n == nbuf);
		printf("%08x", crc);
	}

	if (verbose > 0) {
		TIMER_PRINT
	}
	printf("\n");

	if (f && f != stdin) fclose(f);
	free(buf);
}

