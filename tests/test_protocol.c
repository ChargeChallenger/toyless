#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "protocol/infinity_protocol.h"
#include "protocol/infinity_prng.h"
#include "protocol/infinity_engine.h"

// ---------------------------------------------------------------------------
// Math & Crypto Unit Tests
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Protocol Engine & 9-Slot Unit Tests
// ---------------------------------------------------------------------------

static void create_dummy_figure(uint8_t *buf320, uint8_t id_byte)
{
    memset(buf320, 0, INFINITY_FIGURE_SIZE);

    // UID in Block 0 (bytes 0..6)
    buf320[0] = 0x04;
    buf320[1] = 0x89;
    buf320[2] = 0x44;
    buf320[3] = id_byte;
    buf320[4] = 0x2B;
    buf320[5] = 0x38;
    buf320[6] = 0x80;

    // Character ID block (Block 1 = offset 16..31)
    for (int i = 0; i < 16; i++) {
        buf320[16 + i] = (uint8_t)(id_byte + i);
    }

    // Save Block 1 (Virtual Block 1 = Storage Block 4 = offset 64..79)
    for (int i = 0; i < 16; i++) {
        buf320[64 + i] = (uint8_t)(0x10 + i);
    }
}

static void test_engine_slots_and_notifications(void)
{
    printf("[TEST] Testing Engine Figure Slots & Notification FIFO...\n");

    infinity_engine_init();
    assert(!infinity_notification_pending());

    for (int i = 0; i < INFINITY_NUM_SLOTS; i++) {
        assert(!infinity_slot_present(i));
    }

    uint8_t fig_hex[INFINITY_FIGURE_SIZE];
    uint8_t fig_p1[INFINITY_FIGURE_SIZE];
    uint8_t fig_p2[INFINITY_FIGURE_SIZE];
    create_dummy_figure(fig_hex, 0xA1);
    create_dummy_figure(fig_p1, 0xB2);
    create_dummy_figure(fig_p2, 0xC3);

    // Place on Slot 0 (Hexagon)
    assert(infinity_load_figure(0, fig_hex));
    assert(infinity_slot_present(0));
    assert(infinity_notification_pending());

    uint8_t notify[7];
    int notify_len = infinity_pop_notification(notify);
    assert(notify_len == 7);
    assert(notify[0] == INFINITY_PREFIX_NOTIFY); // 0xAB
    assert(notify[1] == 0x04);
    assert(notify[2] == INFINITY_PAD_HEX);       // Pad 1
    assert(notify[3] == 0x09);
    assert(notify[4] == 0);                      // Order 0 allocated
    assert(notify[5] == INFINITY_EVENT_ADDED);   // 0x00
    assert(notify[6] == infinity_checksum(notify, 6));
    assert(!infinity_notification_pending());

    // Place on Slot 3 (Player 1)
    assert(infinity_load_figure(3, fig_p1));
    assert(infinity_slot_present(3));
    notify_len = infinity_pop_notification(notify);
    assert(notify_len == 7);
    assert(notify[2] == INFINITY_PAD_P1);        // Pad 2
    assert(notify[4] == 1);                      // Order 1 allocated
    assert(notify[5] == INFINITY_EVENT_ADDED);

    // Place on Slot 6 (Player 2)
    assert(infinity_load_figure(6, fig_p2));
    assert(infinity_slot_present(6));
    notify_len = infinity_pop_notification(notify);
    assert(notify_len == 7);
    assert(notify[2] == INFINITY_PAD_P2);        // Pad 3
    assert(notify[4] == 2);                      // Order 2 allocated
    assert(notify[5] == INFINITY_EVENT_ADDED);

    // Remove figure from Slot 3 (Player 1)
    assert(infinity_remove_figure(3));
    assert(!infinity_slot_present(3));
    notify_len = infinity_pop_notification(notify);
    assert(notify_len == 7);
    assert(notify[2] == INFINITY_PAD_P1);        // Pad 2
    assert(notify[4] == 1);                      // Order 1 removed
    assert(notify[5] == INFINITY_EVENT_REMOVED); // 0x01

    // Place new figure on Slot 4 (Player 1 stack). Order allocator should reuse order 1!
    assert(infinity_load_figure(4, fig_p1));
    notify_len = infinity_pop_notification(notify);
    assert(notify_len == 7);
    assert(notify[2] == INFINITY_PAD_P1);
    assert(notify[4] == 1);                      // Reused order 1
    assert(notify[5] == INFINITY_EVENT_ADDED);

    printf("  -> Figure loading, removing, and order bitmask allocator verified!\n");
}

static void test_engine_commands(void)
{
    printf("[TEST] Testing Game Protocol Commands (0x80, 0x81, 0x83, 0xA1, 0xA2, 0xA3, 0xB4)...\n");

    infinity_engine_init();

    uint8_t fig_hex[INFINITY_FIGURE_SIZE];
    uint8_t fig_p1[INFINITY_FIGURE_SIZE];
    create_dummy_figure(fig_hex, 0x11);
    create_dummy_figure(fig_p1, 0x22);

    infinity_load_figure(0, fig_hex); // Hexagon, Order 0
    infinity_load_figure(3, fig_p1);  // Player 1, Order 1

    // Drain initial placement notifications
    uint8_t dummy_notify[7];
    while (infinity_notification_pending()) {
        infinity_pop_notification(dummy_notify);
    }

    uint8_t cmd_buf[32];
    uint8_t resp_buf[32];
    int resp_len;

    // 1. Test 0x80 (Wake / Activate Base)
    cmd_buf[0] = INFINITY_PREFIX_CMD; // 0xFF
    cmd_buf[1] = 0x01;
    cmd_buf[2] = 0x0A;               // Sequence 0x0A
    cmd_buf[3] = INFINITY_CMD_ACTIVATE;
    cmd_buf[4] = infinity_checksum(cmd_buf, 4);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 24);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x15);
    assert(resp_buf[23] == infinity_checksum(resp_buf, 23));

    // WAKE also queues notifications for already placed figures!
    assert(infinity_notification_pending());
    while (infinity_notification_pending()) {
        infinity_pop_notification(dummy_notify);
    }

    // 2. Test 0x81 (Seed PRNG) and 0x83 (PRNG Next)
    uint32_t known_seed = 0xAABBCCDD;
    uint64_t scrambled_seed = infinity_scramble(known_seed, 0);

    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x09;
    cmd_buf[2] = 0x0B;
    cmd_buf[3] = INFINITY_CMD_PRNG_SEED;
    for (int i = 0; i < 8; i++) {
        cmd_buf[4 + i] = (uint8_t)(scrambled_seed >> ((7 - i) * 8));
    }
    cmd_buf[12] = infinity_checksum(cmd_buf, 12);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 4); // Blank response: AA 01 seq crc
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x01);
    assert(resp_buf[2] == 0x0B);
    assert(resp_buf[3] == infinity_checksum(resp_buf, 3));

    // Test 0x83 (Request next random number)
    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x01;
    cmd_buf[2] = 0x0C;
    cmd_buf[3] = INFINITY_CMD_PRNG_NEXT;
    cmd_buf[4] = infinity_checksum(cmd_buf, 4);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 12);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x09);
    assert(resp_buf[2] == 0x0C);
    assert(resp_buf[11] == infinity_checksum(resp_buf, 11));

    // Verify received random number matches our independent PRNG seeded with known_seed!
    infinity_prng_seed(known_seed);
    uint32_t expected_next = infinity_prng_next();
    uint64_t expected_scrambled = infinity_scramble(expected_next, 0);
    uint64_t actual_scrambled = 0;
    for (int i = 0; i < 8; i++) {
        actual_scrambled = (actual_scrambled << 8) | resp_buf[3 + i];
    }
    assert(actual_scrambled == expected_scrambled);

    // 3. Test 0xA1 (Get Present figures)
    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x01;
    cmd_buf[2] = 0x0D;
    cmd_buf[3] = INFINITY_CMD_GET_PRESENT;
    cmd_buf[4] = infinity_checksum(cmd_buf, 4);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    // 2 figures present -> payload is 4 bytes (2 bytes each). Total packet length: 1 + 1 + 1 + 4 + 1 = 8 bytes
    assert(resp_len == 8);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x05); // length = 4 (payload) + 1 (seq)
    assert(resp_buf[2] == 0x0D);
    assert(resp_buf[3] == (INFINITY_BASE_HEX | 0)); // Hexagon, Order 0 (0x10)
    assert(resp_buf[4] == 0x09);
    assert(resp_buf[5] == (INFINITY_BASE_P1 | 1));  // Player 1, Order 1 (0x21)
    assert(resp_buf[6] == 0x09);
    assert(resp_buf[7] == infinity_checksum(resp_buf, 7));

    // 4. Test 0xB4 (Get UID)
    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x02;
    cmd_buf[2] = 0x0E;
    cmd_buf[3] = INFINITY_CMD_GET_UID;
    cmd_buf[4] = 0x00; // Query figure with Order 0
    cmd_buf[5] = infinity_checksum(cmd_buf, 5);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 12);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x09);
    assert(resp_buf[2] == 0x0E);
    assert(resp_buf[3] == 0x00); // Status
    assert(memcmp(&resp_buf[4], fig_hex, 7) == 0); // UID matched
    assert(resp_buf[11] == infinity_checksum(resp_buf, 11));

    // 5. Test 0xA2 (Read Block)
    // Read Virtual Block 0 (which maps to Storage Block 1 = offset 16)
    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x03;
    cmd_buf[2] = 0x0F;
    cmd_buf[3] = INFINITY_CMD_READ_BLOCK;
    cmd_buf[4] = 0x00; // Order 0
    cmd_buf[5] = 0x00; // Virtual Block 0
    cmd_buf[6] = infinity_checksum(cmd_buf, 6);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 21);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x12);
    assert(resp_buf[2] == 0x0F);
    assert(resp_buf[3] == 0x00); // Status Success
    assert(memcmp(&resp_buf[4], &fig_hex[16], 16) == 0);
    assert(resp_buf[20] == infinity_checksum(resp_buf, 20));

    // Read Virtual Block 1 (which maps to Storage Block 4 = offset 64)
    cmd_buf[5] = 0x01; // Virtual Block 1
    cmd_buf[6] = infinity_checksum(cmd_buf, 6);
    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 21);
    assert(memcmp(&resp_buf[4], &fig_hex[64], 16) == 0);

    // 6. Test 0xA3 (Write Block & Dirty Tracking)
    assert(!infinity_figures_dirty());
    assert(!infinity_slot_dirty(0));

    uint8_t new_xp_data[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
                               0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x13;
    cmd_buf[2] = 0x10;
    cmd_buf[3] = INFINITY_CMD_WRITE_BLOCK;
    cmd_buf[4] = 0x00; // Order 0
    cmd_buf[5] = 0x01; // Virtual Block 1 (maps to offset 64)
    cmd_buf[6] = 0x00; // Padding
    memcpy(&cmd_buf[7], new_xp_data, 16);
    cmd_buf[23] = infinity_checksum(cmd_buf, 23);

    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 5);
    assert(resp_buf[0] == INFINITY_PREFIX_RESP);
    assert(resp_buf[1] == 0x02);
    assert(resp_buf[2] == 0x10);
    assert(resp_buf[3] == 0x00); // Status: Success
    assert(resp_buf[4] == infinity_checksum(resp_buf, 4));

    // Verify dirty flag and data update
    assert(infinity_figures_dirty());
    assert(infinity_slot_dirty(0));
    assert(!infinity_slot_dirty(3));
    const uint8_t *slot0_data = infinity_slot_data(0);
    assert(memcmp(&slot0_data[64], new_xp_data, 16) == 0);

    // Clear dirty flags
    infinity_clear_dirty();
    assert(!infinity_figures_dirty());
    assert(!infinity_slot_dirty(0));

    // 7. Test LED and Status blank responses (0x90, 0xB5)
    cmd_buf[0] = INFINITY_PREFIX_CMD;
    cmd_buf[1] = 0x01;
    cmd_buf[2] = 0x11;
    cmd_buf[3] = INFINITY_CMD_LED_FADE;
    cmd_buf[4] = infinity_checksum(cmd_buf, 4);
    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 4);
    assert(resp_buf[2] == 0x11);

    cmd_buf[3] = INFINITY_CMD_STATUS;
    cmd_buf[4] = infinity_checksum(cmd_buf, 4);
    resp_len = infinity_process_command(cmd_buf, resp_buf);
    assert(resp_len == 4);

    printf("  -> All game command handlers (0x80, 0x81, 0x83, 0xA1, 0xA2, 0xA3, 0xB4) verified!\n");
}

int main(void)
{
    printf("=== Starting Disney Infinity Protocol Engine Tests ===\n");
    test_scramble_descramble();
    test_prng();
    test_checksum();
    test_engine_slots_and_notifications();
    test_engine_commands();
    printf("=== All Protocol & Engine Tests PASSED Successfully! ===\n");
    return 0;
}
