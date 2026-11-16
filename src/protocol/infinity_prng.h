#ifndef INFINITY_PRNG_H
#define INFINITY_PRNG_H

#include <stdint.h>
#include <stddef.h>

// Initialize the 4 PRNG registers and run 23 warmup iterations
void infinity_prng_seed(uint32_t seed);

// Generate the next 32 bit pseudorandom value
uint32_t infinity_prng_next(void);

// Scramble a 32 bit number using the 64 bit DI mask
uint64_t infinity_scramble(uint32_t num, uint32_t garbage);

// Descramble a 64 bit value received in command 0x81 to extract 32 bit seed
uint32_t infinity_descramble(uint64_t num);

// Calculate DI packet checksum
uint8_t infinity_checksum(const uint8_t *data, int len);

#endif