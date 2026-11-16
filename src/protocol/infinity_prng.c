#include "protocol/infinity_prng.h"

// DI PRNG internal state registers
static uint32_t random_a = 0xF1EA5EED;
static uint32_t random_b = 0;
static uint32_t random_c = 0;
static uint32_t random_d = 0;

// Secret 64 bit bit-permutation mask discovered from DI hardware
static const uint64_t SCRAMBLE_MASK = 0x8E55AA1B3999E8AAULL;

// Helper: 32 bit left bitwise rotation
static inline uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
} 

uint32_t infinity_prng_next(void) {
    uint32_t a = random_a;
    uint32_t b = random_b;
    uint32_t c = random_c;
    uint32_t ret = rotl32(b, 27);

    uint32_t temp = a + ((ret ^ 0xFFFFFFFFU) + 1U);
    b ^= rotl32(c, 17);
    a = random_d;
    c += a;
    ret = b + temp;
    a += temp;

    random_c = a;
    random_a = b;
    random_b = c;
    random_d = ret;
    return ret;
}

void infinity_prng_seed(uint32_t seed) {
    random_a = 0xF1EA5EED;
    random_b = seed;
    random_c = seed;
    random_d = seed;

    for (int i = 0; i < 23; i++) {
        infinity_prng_next();
    }
}

uint64_t infinity_scramble(uint32_t num, uint32_t garbage) {
    uint64_t mask = SCRAMBLE_MASK;
    uint64_t ret = 0;

    for (int i = 0; i < 64; i++) {
        ret <<= 1;
        if ((mask & 1ULL) != 0) {
            ret |= (num & 1U);
            num >>= 1;
        } else {
            ret |= (garbage & 1U);
            garbage >>= 1;
        }
        mask >>= 1;
    }
    return ret;
}

uint32_t infinity_descramble(uint64_t num) {
    uint64_t mask = SCRAMBLE_MASK;
    uint32_t ret = 0;

    for (int i = 0; i < 64; i++) {
        if ((mask >> 63) & 1ULL) {
            ret = (ret << 1) | (uint32_t)(num & 0x01ULL);
        }
        num >>= 1;
        mask <<= 1;
    }
    return ret;
}

uint8_t infinity_checksum(const uint8_t *data, int len) {
    if (!data || len <= 0) return 0;
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}
