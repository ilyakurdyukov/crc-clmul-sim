#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crc_slice.h"
#include "crc_clsim.h"

#include "perfcnt.h"

static void snapshot_clock(void *p)
{
	clock_gettime(CLOCK_MONOTONIC, p);
}
static int perf_fd;
static void snapshot_perf(void *p)
{
	perf_measure(perf_fd, p);
}
static void (*snapshot)(void *) = snapshot_clock;

static long long delta_clock(void *p1, void *p0, int full)
{
	struct timespec *ts1 = p1, *ts0 = p0;
	long nsecdiff = ts1->tv_nsec - ts0->tv_nsec;
	return (ts1->tv_sec - ts0->tv_sec) * 1000000000 + nsecdiff;
}
static struct {
	uint64_t cycles;
	uint64_t instructions;
} cnt_min = { -1, -1 };

static void update_min(uint64_t *x, uint64_t v)
{
	if (*x > v) *x = v;
}
static long long delta_perf(void *p1, void *p0, int full)
{
	struct perf_counters *c1 = p1, *c0 = p0;
	if (full) {
		update_min(&cnt_min.cycles, c1->cycles - c0->cycles);
		update_min(&cnt_min.instructions,
			   c1->instructions - c0->instructions);
	}
	return c1->nsec - c0->nsec;
}
static long long (*delta)(void *, void *, int) = delta_clock;

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
		for (; s < e; s += 8) c = _mm_crc32_u64(c, *(uint64_t*)s);
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
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x55;

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
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x55;

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
	for (i = 0; i < n + 16; i++) buf[i] = i * 0x55;

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
	int use_perfcnt = 0;
	const char *type = "crc64_simple";
	long long timesum = 0;
	union {
		struct timespec ts;
		struct perf_counters cnt;
	} u1, u0;

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
		} else if (argc > 1 && !strcmp(argv[1], "-p")) {
			use_perfcnt = 1;
			argc -= 1; argv += 1;
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

	if (use_perfcnt) {
		if ((perf_fd = perf_setup()) < 0) {
			perf_hint();
			return 2;
		}
		snapshot = snapshot_perf;
		delta = delta_perf;
	}

	buf = malloc(nbuf);
	if (!buf) return 2;

	if (!f)
		for (n = 0; n < nbuf; n++) buf[n] = n * 0x55;

	if (crc64_fn) {
		uint64_t crc = 0;
		if (crc64_check(crc64_fn)) return 3;
		do {
			if (f) n = fread(buf, 1, nbuf, f);
			else len -= n = len > nbuf ? nbuf : len;
			if (!n) break;
			snapshot(&u0);
			crc = crc64_fn(buf, n, crc);
			snapshot(&u1);
			timesum += delta(&u1, &u0, n == nbuf);
		} while (n == nbuf);
		printf("%016llx", (long long)crc);
	} else {
		uint32_t crc = 0;
		if (crc32_check_fn(crc32_fn)) return 3;
		do {
			if (f) n = fread(buf, 1, nbuf, f);
			else len -= n = len > nbuf ? nbuf : len;
			if (!n) break;
			snapshot(&u0);
			crc = crc32_fn(buf, n, crc);
			snapshot(&u1);
			timesum += delta(&u1, &u0, n == nbuf);
		} while (n == nbuf);
		printf("%08x", crc);
	}

	if (verbose > 0)
		printf(" %s: %.3fms\n", type, timesum * 1e-6);
	else
		printf("\n");

	if (use_perfcnt) {
		uint64_t c = cnt_min.cycles, i = cnt_min.instructions;
		double div = nbuf;
		printf("%g cycles/B, %g insns/B, %g IPC\n",
		       c / div,
		       i / div,
		       1. * i / c);
	}

	if (f && f != stdin) fclose(f);
	free(buf);
}
