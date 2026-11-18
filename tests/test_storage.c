#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "storage/figure_synthesizer.h"
#include "storage/lfs_port.h"

// Stubs for host testing
void log_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

lfs_t *lfs_port_get_fs(void)
{
    return NULL;
}

// ANSI colors for test reporting
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED   "\033[0;31m"
#define COLOR_CYAN  "\033[0;36m"
#define COLOR_RESET "\033[0m"

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (cond) { \
        s_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf(COLOR_RED "  [FAIL] %s (line %d)\n" COLOR_RESET, msg, __LINE__); \
    } \
} while(0)

// Test 1: Synthesize figures and verify roundtrip extraction
static void test_synthesis_roundtrip(void)
{
    printf(COLOR_CYAN "Running test_synthesis_roundtrip...\n" COLOR_RESET);

    const uint32_t test_ids[] = {
        0x0F4241, // Mr. Incredible (1.0 Character)
        0x0F4242, // Sulley (1.0 Character)
        0x0F4259, // Elsa (1.0 Character)
        0x0F42A6, // Iron Man (2.0 Character)
        0x1E8481, // Play Set (1.0 Playset)
        0x2DC6C3  // Bolt Ability (Power Disc)
    };

    uint8_t dump[FIGURE_DUMP_SIZE];

    for (size_t i = 0; i < sizeof(test_ids) / sizeof(test_ids[0]); i++) {
        uint32_t expected_id = test_ids[i];

        bool ok = figure_synthesizer_create(expected_id, NULL, dump);
        TEST_ASSERT(ok, "figure_synthesizer_create succeeded");

        // Verify NXP UID prefix in Block 0
        TEST_ASSERT(dump[0] == 0x04, "Block 0 has NXP prefix 0x04");

        // Verify sector 0 trailer access bits at 0x30 (17 87 8E)
        TEST_ASSERT(dump[0x36] == 0x17 && dump[0x37] == 0x87 && dump[0x38] == 0x8E,
                    "Sector 0 trailer access bits match 0x17878E");

        // Verify sector 1 trailer access bits at 0x70 (77 87 88)
        TEST_ASSERT(dump[0x76] == 0x77 && dump[0x77] == 0x87 && dump[0x78] == 0x88,
                    "Sector 1 trailer access bits match 0x778788");

        // Decrypt and extract Toy ID
        uint32_t extracted_id = figure_synthesizer_get_toy_id(dump);
        TEST_ASSERT(extracted_id == expected_id, "Extracted Toy ID matches synthesized ID");
    }
}

// Test 2: Custom UID support
static void test_custom_uid(void)
{
    printf(COLOR_CYAN "Running test_custom_uid...\n" COLOR_RESET);

    const uint8_t custom_uid[7] = { 0x04, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    uint8_t dump[FIGURE_DUMP_SIZE];

    bool ok = figure_synthesizer_create(0x0F42A6, custom_uid, dump);
    TEST_ASSERT(ok, "Creation with custom UID succeeded");
    TEST_ASSERT(memcmp(&dump[0], custom_uid, 7) == 0, "Block 0 matches custom UID");

    uint32_t extracted_id = figure_synthesizer_get_toy_id(dump);
    TEST_ASSERT(extracted_id == 0x0F42A6, "Extracted Toy ID matches with custom UID");
}

// Test 3: CRC corruption detection
static void test_tamper_detection(void)
{
    printf(COLOR_CYAN "Running test_tamper_detection...\n" COLOR_RESET);

    uint8_t dump[FIGURE_DUMP_SIZE];
    figure_synthesizer_create(0x0F4241, NULL, dump);

    // Corrupt one encrypted byte in Block 1
    dump[16] ^= 0x01;

    uint32_t extracted_id = figure_synthesizer_get_toy_id(dump);
    TEST_ASSERT(extracted_id == 0, "Corrupted dump rejected with ID 0");
}

// Test 4: Real dump verification from official pack
static void test_real_dump_compatibility(void)
{
    printf(COLOR_CYAN "Running test_real_dump_compatibility...\n" COLOR_RESET);

    const char *real_dump_path = "/home/dellich/Документы/DisneyInfinityPortalEmulator/Disney-Infinity-NFC/Infinity_Ultimate_NFC_Pack/Archives/V1/Infinity 2.0/1) Characters/The Avengers - Iron Man.bin";

    FILE *f = fopen(real_dump_path, "rb");
    if (!f) {
        printf("  [SKIP] Official Iron Man dump file not found at path\n");
        return;
    }

    uint8_t real_dump[FIGURE_DUMP_SIZE];
    size_t read_bytes = fread(real_dump, 1, FIGURE_DUMP_SIZE, f);
    fclose(f);

    TEST_ASSERT(read_bytes == FIGURE_DUMP_SIZE, "Read full 320 bytes of real Iron Man dump");

    // Extract Toy ID from real official figure
    uint32_t extracted_id = figure_synthesizer_get_toy_id(real_dump);
    TEST_ASSERT(extracted_id == 0x0F42A6, "Official Iron Man dump verified (Toy ID 0x0F42A6)");
}

int main(void)
{
    printf("\n=========================================\n");
    printf("   Toyless Figure Synthesizer Test Suite\n");
    printf("=========================================\n\n");

    test_synthesis_roundtrip();
    test_custom_uid();
    test_tamper_detection();
    test_real_dump_compatibility();

    printf("\n-----------------------------------------\n");
    printf("Results: %d/%d tests passed\n", s_tests_passed, s_tests_run);
    printf("-----------------------------------------\n\n");

    return (s_tests_passed == s_tests_run) ? 0 : 1;
}
