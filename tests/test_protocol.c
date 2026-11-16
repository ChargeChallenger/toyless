#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "protocol/infinity_protocol.h"
#include "protocol/infinity_prng.h"

static void test_scramble_descramble(void)
{
    printf("[TEST] Testing Scramble & Descramble...\n");

    const uint32_t test_vectors[] = {
        0x00000000U,
        0xFFFFFFFFU,
        0x12345678U,
        0x87654321U,
        0xAAAAAAAAU,
        0x55555555U,
        0xCAFEBABEU,
        0xDEADBEEFU,
        0xF1EA5EEDU,
        0x0F424100U // Disney character ID example (Mr. Incredible)
    };
    const size_t num_vectors = sizeof(test_vectors) / sizeof(test_vectors[0]);

    for (size_t i = 0; i < num_vectors; i++)
    {
        uint32_t original = test_vectors[i];

        // Test with zero garbage
        uint64_t scrambled = infinity_scramble(original, 0);
        uint32_t descrambled = infinity_descramble(scrambled);
        assert(descrambled == original);

        // Test with various garbage values (garbage should not affect descrambled output)
        uint64_t scrambled_with_garbage = infinity_scramble(original, 0x1337BEEF);
        uint32_t descrambled_from_garbage = infinity_descramble(scrambled_with_garbage);
        assert(descrambled_from_garbage == original);
    }

    printf("  -> Scramble & Descramble round-trip verified for all vectors!\n");
}

static void test_prng(void)
{
    printf("[TEST] Testing PRNG Seed and Generation...\n");

    // Seed PRNG with known value
    infinity_prng_seed(0x12345678U);

    uint32_t val1 = infinity_prng_next();
    uint32_t val2 = infinity_prng_next();
    uint32_t val3 = infinity_prng_next();

    // Verify generated values are non-zero and changing
    assert(val1 != 0);
    assert(val2 != 0);
    assert(val3 != 0);
    assert(val1 != val2 && val2 != val3);

    // Verify determinism: re-seeding with the same seed must produce the exact same sequence
    infinity_prng_seed(0x12345678U);
    assert(infinity_prng_next() == val1);
    assert(infinity_prng_next() == val2);
    assert(infinity_prng_next() == val3);

    printf("  -> PRNG determinism and warmup mixing verified!\n");
}

static void test_checksum(void)
{
    printf("[TEST] Testing Packet Checksums...\n");

    // Official Disney Infinity 0x80 Activate Base Response (24 bytes)
    // The 24th byte (0x8C) is the checksum of the first 23 bytes.
    const uint8_t activate_response[24] = {
        0xAA, 0x15, 0x00, 0x00, 0x0F, 0x01, 0x00, 0x03,
        0x02, 0x09, 0x09, 0x43, 0x20, 0x32, 0x62, 0x36,
        0x36, 0x4B, 0x34, 0x99, 0x67, 0x31, 0x93, 0x8C};

    uint8_t calculated_crc = infinity_checksum(activate_response, 23);
    assert(calculated_crc == activate_response[23]); // Must be exactly 0x8C

    // Blank response: AA 01 <seq> <crc>
    const uint8_t blank_cmd[3] = {0xAA, 0x01, 0x05};
    assert(infinity_checksum(blank_cmd, 3) == ((0xAA + 0x01 + 0x05) & 0xFF));

    // Figure notification: AB 04 <pad> 09 <order> <event> <crc>
    const uint8_t notify_pkt[6] = {0xAB, 0x04, 0x01, 0x09, 0x00, 0x00};
    assert(infinity_checksum(notify_pkt, 6) == ((0xAB + 0x04 + 0x01 + 0x09) & 0xFF));

    printf("  -> Packet checksum calculation verified with official packets!\n");
}

int main(void)
{
    printf("=== Starting Disney Infinity Protocol Math Tests ===\n");
    test_scramble_descramble();
    test_prng();
    test_checksum();
    printf("=== All Protocol Math Tests PASSED Successfully! ===\n");
    return 0;
}