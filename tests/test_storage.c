#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "storage/figure_synthesizer.h"
#include "storage/storage_manager.h"
#include "storage/lfs_port.h"
#include "core/icc.h"
#include "protocol/infinity_engine.h"
#include "bd/lfs_rambd.h"

// Stubs and host logging
void log_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    (void)args;
    (void)fmt;
    va_end(args);
}

// Host ICC queue stubs
static portal_cmd_t s_host_cmd_queue[8];
static int s_host_cmd_head = 0;
static int s_host_cmd_tail = 0;

void icc_init(void)
{
    s_host_cmd_head = 0;
    s_host_cmd_tail = 0;
}

bool icc_send_cmd(const portal_cmd_t *cmd)
{
    int next = (s_host_cmd_head + 1) % 8;
    if (next == s_host_cmd_tail) return false;
    s_host_cmd_queue[s_host_cmd_head] = *cmd;
    s_host_cmd_head = next;
    return true;
}

bool icc_poll_cmd(portal_cmd_t *out_cmd)
{
    if (s_host_cmd_head == s_host_cmd_tail) return false;
    *out_cmd = s_host_cmd_queue[s_host_cmd_tail];
    s_host_cmd_tail = (s_host_cmd_tail + 1) % 8;
    return true;
}

bool icc_send_event(const portal_event_t *evt)
{
    (void)evt;
    return true;
}

bool icc_poll_event(portal_event_t *out_evt)
{
    (void)out_evt;
    return false;
}

// In-memory LittleFS for host test suite
static lfs_t s_host_lfs;
static struct lfs_config s_host_lfs_cfg;
static lfs_rambd_t s_rambd;
static struct lfs_rambd_config s_rambd_cfg;
static uint8_t s_rambd_buffer[128 * 1024]; // 128 KB RAM buffer

lfs_t *lfs_port_get_fs(void)
{
    return &s_host_lfs;
}

int lfs_port_init(void)
{
    return 0; // Handled by test setup
}

static void setup_host_littlefs(void)
{
    memset(&s_rambd_cfg, 0, sizeof(s_rambd_cfg));
    s_rambd_cfg.read_size = 16;
    s_rambd_cfg.prog_size = 16;
    s_rambd_cfg.erase_size = 512;
    s_rambd_cfg.erase_count = sizeof(s_rambd_buffer) / 512;
    s_rambd_cfg.buffer = s_rambd_buffer;

    memset(&s_host_lfs_cfg, 0, sizeof(s_host_lfs_cfg));
    s_host_lfs_cfg.context = &s_rambd;
    s_host_lfs_cfg.read = lfs_rambd_read;
    s_host_lfs_cfg.prog = lfs_rambd_prog;
    s_host_lfs_cfg.erase = lfs_rambd_erase;
    s_host_lfs_cfg.sync = lfs_rambd_sync;

    s_host_lfs_cfg.read_size = 16;
    s_host_lfs_cfg.prog_size = 16;
    s_host_lfs_cfg.block_size = 512;
    s_host_lfs_cfg.block_count = sizeof(s_rambd_buffer) / 512;
    s_host_lfs_cfg.cache_size = 64;
    s_host_lfs_cfg.lookahead_size = 16;
    s_host_lfs_cfg.block_cycles = 500;

    int err = lfs_rambd_create(&s_host_lfs_cfg, &s_rambd_cfg);
    assert(err == 0);
    err = lfs_format(&s_host_lfs, &s_host_lfs_cfg);
    assert(err == 0);
    err = lfs_mount(&s_host_lfs, &s_host_lfs_cfg);
    assert(err == 0);

    lfs_mkdir(&s_host_lfs, "/figures");
    lfs_mkdir(&s_host_lfs, "/saves");
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

// Test 5: Storage manager with LittleFS operations
static void test_storage_manager(void)
{
    printf(COLOR_CYAN "Running test_storage_manager...\n" COLOR_RESET);

    // 1. Synthesize figure directly into LittleFS
    bool ok = figure_synthesizer_save_file("/figures/iron_man.bin", 0x0F42A6, NULL);
    TEST_ASSERT(ok, "figure_synthesizer_save_file succeeded in LittleFS");

    // 2. Initialize storage manager (clean empty base)
    int init_res = storage_manager_init();
    TEST_ASSERT(init_res == 0, "storage_manager_init succeeded");
    TEST_ASSERT(strcmp(storage_manager_get_slot_path(3), "") == 0, "Slot 3 starts empty");

    // 3. Set figure on Slot 3 (Player 1)
    ok = storage_manager_set_slot_figure(3, "/figures/iron_man.bin");
    TEST_ASSERT(ok, "storage_manager_set_slot_figure succeeded");
    TEST_ASSERT(strcmp(storage_manager_get_slot_path(3), "/figures/iron_man.bin") == 0,
                "Slot 3 path is /figures/iron_man.bin");

    // Verify ICC received command to place figure on Core 0
    portal_cmd_t cmd;
    ok = icc_poll_cmd(&cmd);
    TEST_ASSERT(ok && cmd.type == PORTAL_CMD_SET_FIGURE && cmd.slot == 3,
                "ICC received PORTAL_CMD_SET_FIGURE for slot 3");

    // Core 0 loads figure into protocol engine
    infinity_load_figure(cmd.slot, cmd.figure_data);
    TEST_ASSERT(infinity_slot_present(3), "Figure is present in infinity engine slot 3");

    // 4. Test debounce write back: simulate game modifying figure
    storage_manager_mark_dirty(3);

    // Run task to flush debounce
    storage_manager_task();

    // 5. Clear figure from Slot 3
    ok = storage_manager_clear_slot(3);
    TEST_ASSERT(ok, "storage_manager_clear_slot succeeded");
    TEST_ASSERT(strcmp(storage_manager_get_slot_path(3), "") == 0, "Slot 3 is now empty");

    ok = icc_poll_cmd(&cmd);
    TEST_ASSERT(ok && cmd.type == PORTAL_CMD_REMOVE_FIGURE && cmd.slot == 3,
                "ICC received PORTAL_CMD_REMOVE_FIGURE for slot 3");
}

int main(void)
{
    printf("\n=========================================\n");
    printf("   Toyless Storage & Synthesizer Suite   \n");
    printf("=========================================\n\n");

    icc_init();
    infinity_engine_init();
    setup_host_littlefs();

    test_synthesis_roundtrip();
    test_custom_uid();
    test_tamper_detection();
    test_real_dump_compatibility();
    test_storage_manager();

    printf("\n-----------------------------------------\n");
    printf("Results: %d/%d tests passed\n", s_tests_passed, s_tests_run);
    printf("-----------------------------------------\n\n");

    return (s_tests_passed == s_tests_run) ? 0 : 1;
}
