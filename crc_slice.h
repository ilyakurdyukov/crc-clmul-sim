#include <stdint.h>
#include <stddef.h>

#ifndef POLY32
#define POLY32 0xedb88320
#endif

#ifndef POLY64
#define POLY64 0xc96c5795d7870f42
#endif

// 256*4*4 = 4096
static uint32_t crc32_table4[4][256];

void crc32_slice4_init(void) {
	uint32_t a, p = POLY32;
	int i, j, k;

	for (i = 0; i < 256; i++)
		for (a = i, j = 0; j < 4; j++) {
			for (k = 0; k < 8; k++)
				a = (a >> 1) ^ (a & 1 ? p : 0);
			crc32_table4[j][i] = a;
		}
}

uint32_t crc32_slice4(const uint8_t *s, size_t n, uint32_t c) {
	c = ~c;

	if (n >= 4) {
		for (; (uintptr_t)s & 3; n--)
			c = crc32_table4[0][*s++ ^ (c & 0xff)] ^ c >> 8;

#define FOLD \
	c = crc32_table4[3][c & 0xff] ^ \
		crc32_table4[2][c >> 8 & 0xff] ^ \
		crc32_table4[1][c >> 16 & 0xff] ^ \
		crc32_table4[0][c >> 24];

		if (n >= 4) {
			const uint32_t *a, *e;
			uint32_t t;
			a = (const uint32_t*)s;
			e = (const uint32_t*)(s + n - 3);
			c ^= *a++; n &= 3;
			for (; a < e; c ^= t) {
				t = *a++;
				FOLD
			}
			FOLD
#undef FOLD
			s = (const uint8_t*)a;
		}
	}

	for (; n; n--)
		c = crc32_table4[0][*s++ ^ (c & 0xff)] ^ c >> 8;

	return ~c;
}

// 256*4*8 = 8192
static uint64_t crc64_table4[4][256];

void crc64_slice4_init(void) {
	uint64_t a, p = POLY64;
	int i, j, k;

	for (i = 0; i < 256; i++)
		for (a = i, j = 0; j < 4; j++) {
			for (k = 0; k < 8; k++)
				a = (a >> 1) ^ (a & 1 ? p : 0);
			crc64_table4[j][i] = a;
		}
}

uint64_t crc64_slice4(const uint8_t *s, size_t n, uint64_t c) {
	c = ~c;

	if (n >= 4) {
		for (; (uintptr_t)s & 3; n--)
			c = crc64_table4[0][*s++ ^ (c & 0xff)] ^ c >> 8;

#define FOLD x = c; \
	c = crc64_table4[3][x & 0xff] ^ \
		crc64_table4[2][x >> 8 & 0xff] ^ \
		crc64_table4[1][x >> 16 & 0xff] ^ \
		crc64_table4[0][x >> 24] ^ c >> 32;

		if (n >= 4) {
			const uint32_t *a, *e;
			uint32_t x, t;
			a = (const uint32_t*)s;
			e = (const uint32_t*)(s + n - 3);
			c ^= *a++; n &= 3;
			for (; a < e; c ^= t) {
				t = *a++;
				FOLD
			}
			FOLD
#undef FOLD
			s = (const uint8_t*)a;
		}
	}

	for (; n; n--)
		c = crc64_table4[0][*s++ ^ (c & 0xff)] ^ c >> 8;

	return ~c;
}
