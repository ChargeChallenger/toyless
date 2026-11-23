#include "protocol/infinity_engine.h"
#include "protocol/infinity_prng.h"
#include <string.h>

// Fixed 24-byte response to 0x80 (Wake / Activate) matching real base and Dolphin
static const uint8_t INFINITY_WAKE_RESPONSE[24] = {
    0xAA, 0x15, 0x00, 0x00, 0x0F, 0x01, 0x00, 0x03,
    0x02, 0x09, 0x09, 0x43, 0x20, 0x32, 0x62, 0x36,
    0x36, 0x4B, 0x34, 0x99, 0x67, 0x31, 0x93, 0x8C
};

// Internal engine state (9 logical slots)
static infinity_figure_slot_t s_slots[INFINITY_NUM_SLOTS];
static uint16_t s_order_mask = 0;
static bool s_dirty = false;

// Ring buffer FIFO for placement/removal notifications (0xAB)
#define NOTIFY_QUEUE_SIZE 16
#define NOTIFY_PACKET_LEN 7

static uint8_t s_notify_queue[NOTIFY_QUEUE_SIZE][NOTIFY_PACKET_LEN];
static uint8_t s_notify_head = 0;
static uint8_t s_notify_tail = 0;
static uint8_t s_notify_count = 0;

// Order allocator: finds the first free order index in [0..15]
static uint8_t alloc_order(void) {
    for (uint8_t i = 0; i < 16; i++) {
        if (!(s_order_mask & (1u << i))) {
            s_order_mask |= (1u << i);
            return i;
        }
    }
    return 0xFF; // Full (should not happen with 9 slots max)
}

static void free_order(uint8_t order) {
    if (order < 16) {
        s_order_mask &= ~(1u << order);
    }
}

// Map logical slot index (0..8) to hardware pad identifier (1, 2, 3)
static inline uint8_t slot_to_pad_id(uint8_t slot) {
    if (slot < 3) return INFINITY_PAD_HEX; // 1: Hexagon
    if (slot < 6) return INFINITY_PAD_P1;  // 2: Player 1
    return INFINITY_PAD_P2;               // 3: Player 2
}

// Map logical slot index (0..8) to base address nibble (0x10, 0x20, 0x30)
static inline uint8_t slot_to_base_code(uint8_t slot) {
    if (slot < 3) return INFINITY_BASE_HEX; // 0x10
    if (slot < 6) return INFINITY_BASE_P1;  // 0x20
    return INFINITY_BASE_P2;               // 0x30
}

// Map game-side virtual block index to actual storage block in 320-byte file
static inline uint8_t to_storage_block(uint8_t block) {
    return (block == 0) ? 1 : (block * 4);
}

// Look up which slot contains the figure with the given order_added
static int find_slot_by_order(uint8_t order) {
    for (int i = 0; i < INFINITY_NUM_SLOTS; i++) {
        if (s_slots[i].present && s_slots[i].order_added == order) {
            return i;
        }
    }
    return -1;
}

// Push a 7-byte 0xAB notification packet to the FIFO
static void enqueue_notification(uint8_t pad_id, uint8_t order, uint8_t event) {
    if (s_notify_count >= NOTIFY_QUEUE_SIZE) {
        // Queue full: drop the oldest packet
        s_notify_head = (s_notify_head + 1) % NOTIFY_QUEUE_SIZE;
        s_notify_count--;
    }

    uint8_t *pkt = s_notify_queue[s_notify_tail];
    pkt[0] = INFINITY_PREFIX_NOTIFY; // 0xAB
    pkt[1] = 0x04;                  // Length of payload (pad, 0x09, order, event)
    pkt[2] = pad_id;                // Pad ID: 1, 2, or 3
    pkt[3] = 0x09;                  // Disney protocol constant
    pkt[4] = order;                 // 0..15
    pkt[5] = event;                 // 0x00: ADDED, 0x01: REMOVED
    pkt[6] = infinity_checksum(pkt, 6);

    s_notify_tail = (s_notify_tail + 1) % NOTIFY_QUEUE_SIZE;
    s_notify_count++;
}

// Build a 4-byte blank response: 0xAA 0x01 <seq> <checksum>
static int build_blank_response(uint8_t seq, uint8_t *out) {
    out[0] = INFINITY_PREFIX_RESP; // 0xAA
    out[1] = 0x01;
    out[2] = seq;
    out[3] = infinity_checksum(out, 3);
    return 4;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void infinity_engine_init(void) {
    memset(s_slots, 0, sizeof(s_slots));
    s_order_mask = 0;
    s_dirty = false;
    s_notify_head = 0;
    s_notify_tail = 0;
    s_notify_count = 0;
}

int infinity_process_command(const uint8_t *pkt, uint8_t *out) {
    if (!pkt || !out) return 0;
    if (pkt[0] != INFINITY_PREFIX_CMD) return 0; // Must be 0xFF

    uint8_t len = pkt[1];
    uint8_t cmd = pkt[2];
    uint8_t seq = pkt[3];

    switch (cmd) {
        case INFINITY_CMD_ACTIVATE: { // 0x80
            memcpy(out, INFINITY_WAKE_RESPONSE, sizeof(INFINITY_WAKE_RESPONSE));
            out[2] = seq;
            out[23] = infinity_checksum(out, 23);
            return sizeof(INFINITY_WAKE_RESPONSE);
        }

        case INFINITY_CMD_PRNG_SEED: { // 0x81
            if (len >= 9) {
                uint64_t scrambled = 0;
                for (int i = 0; i < 8; i++) {
                    scrambled = (scrambled << 8) | pkt[4 + i];
                }
                uint32_t seed = infinity_descramble(scrambled);
                infinity_prng_seed(seed);
            }
            return build_blank_response(seq, out);
        }

        case INFINITY_CMD_PRNG_NEXT: { // 0x83
            uint32_t next_rand = infinity_prng_next();
            uint64_t scrambled = infinity_scramble(next_rand, 0);

            out[0] = INFINITY_PREFIX_RESP; // 0xAA
            out[1] = 0x09;                 // 1 byte seq + 8 bytes payload
            out[2] = seq;
            for (int i = 0; i < 8; i++) {
                out[3 + i] = (uint8_t)(scrambled >> ((7 - i) * 8));
            }
            out[11] = infinity_checksum(out, 11);
            return 12;
        }

        case INFINITY_CMD_LED_FADE:   // 0x90
        case INFINITY_CMD_LED_COLOR:  // 0x92
        case INFINITY_CMD_LED_FLASH:  // 0x93
        case INFINITY_CMD_LED_SPECIAL1: // 0x95
        case INFINITY_CMD_LED_SPECIAL2: // 0x96
        case INFINITY_CMD_STATUS: {   // 0xB5
            return build_blank_response(seq, out);
        }

        case INFINITY_CMD_GET_PRESENT: { // 0xA1
            uint8_t x = 3;
            for (uint8_t i = 0; i < INFINITY_NUM_SLOTS; i++) {
                if (s_slots[i].present) {
                    out[x]     = slot_to_base_code(i) | (s_slots[i].order_added & 0x0F);
                    out[x + 1] = 0x09;
                    x += 2;
                }
            }
            out[0] = INFINITY_PREFIX_RESP;     // 0xAA
            out[1] = (uint8_t)(x - 2);         // length of payload + 1 (seq)
            out[2] = seq;
            out[x] = infinity_checksum(out, x);
            return x + 1;
        }

        case INFINITY_CMD_READ_BLOCK: { // 0xA2
            uint8_t fig_order = pkt[4];
            uint8_t block     = pkt[5];
            uint8_t s_block   = to_storage_block(block);

            out[0] = INFINITY_PREFIX_RESP; // 0xAA
            out[1] = 0x12;                 // 18 bytes (1 seq + 1 status + 16 data)
            out[2] = seq;
            out[3] = 0x00;                 // Status: Success

            int slot_idx = find_slot_by_order(fig_order);
            if (slot_idx >= 0 && s_block < (INFINITY_FIGURE_SIZE / 16)) {
                memcpy(&out[4], &s_slots[slot_idx].data[s_block * 16], 16);
            } else {
                memset(&out[4], 0, 16);
            }
            out[20] = infinity_checksum(out, 20);
            return 21;
        }

        case INFINITY_CMD_WRITE_BLOCK: { // 0xA3
            uint8_t fig_order     = pkt[4];
            uint8_t block         = pkt[5];
            const uint8_t *data16 = &pkt[7];
            uint8_t s_block       = to_storage_block(block);

            int slot_idx = find_slot_by_order(fig_order);
            if (slot_idx >= 0 && s_block < (INFINITY_FIGURE_SIZE / 16)) {
                memcpy(&s_slots[slot_idx].data[s_block * 16], data16, 16);
                s_slots[slot_idx].dirty = true;
                s_dirty = true;
            }

            // Immediate ACK response to game
            out[0] = INFINITY_PREFIX_RESP; // 0xAA
            out[1] = 0x02;                 // 2 bytes (1 seq + 1 status)
            out[2] = seq;
            out[3] = 0x00;                 // Status: Success
            out[4] = infinity_checksum(out, 4);
            return 5;
        }

        case INFINITY_CMD_GET_UID: { // 0xB4
            uint8_t fig_order = pkt[4];

            out[0] = INFINITY_PREFIX_RESP; // 0xAA
            out[1] = 0x09;                 // 9 bytes (1 seq + 1 status + 7 UID)
            out[2] = seq;
            out[3] = 0x00;                 // Status: Success

            int slot_idx = find_slot_by_order(fig_order);
            if (slot_idx >= 0) {
                memcpy(&out[4], s_slots[slot_idx].data, 7);
            } else {
                memset(&out[4], 0, 7);
            }
            out[11] = infinity_checksum(out, 11);
            return 12;
        }

        default:
            return build_blank_response(seq, out);
    }
}

int infinity_pop_notification(uint8_t *out) {
    if (!out || s_notify_count == 0) return 0;

    memcpy(out, s_notify_queue[s_notify_head], NOTIFY_PACKET_LEN);
    s_notify_head = (s_notify_head + 1) % NOTIFY_QUEUE_SIZE;
    s_notify_count--;
    return NOTIFY_PACKET_LEN;
}

bool infinity_notification_pending(void) {
    return s_notify_count > 0;
}

bool infinity_load_figure(uint8_t slot, const uint8_t *data320) {
    if (slot >= INFINITY_NUM_SLOTS || !data320) return false;

    if (s_slots[slot].present) {
        free_order(s_slots[slot].order_added);
    }

    uint8_t order = alloc_order();
    if (order == 0xFF) return false;

    memcpy(s_slots[slot].data, data320, INFINITY_FIGURE_SIZE);
    s_slots[slot].present = true;
    s_slots[slot].dirty = false;
    s_slots[slot].order_added = order;

    enqueue_notification(slot_to_pad_id(slot), order, INFINITY_EVENT_ADDED);
    return true;
}

bool infinity_remove_figure(uint8_t slot) {
    if (slot >= INFINITY_NUM_SLOTS || !s_slots[slot].present) return false;

    uint8_t order = s_slots[slot].order_added;
    enqueue_notification(slot_to_pad_id(slot), order, INFINITY_EVENT_REMOVED);

    free_order(order);
    s_slots[slot].present = false;
    s_slots[slot].dirty = false;
    s_slots[slot].order_added = 0xFF;
    return true;
}

bool infinity_slot_present(uint8_t slot) {
    if (slot >= INFINITY_NUM_SLOTS) return false;
    return s_slots[slot].present;
}

const uint8_t *infinity_slot_data(uint8_t slot) {
    if (slot >= INFINITY_NUM_SLOTS) return NULL;
    return s_slots[slot].data;
}

bool infinity_figures_dirty(void) {
    return s_dirty;
}

void infinity_clear_dirty(void) {
    s_dirty = false;
    for (int i = 0; i < INFINITY_NUM_SLOTS; i++) {
        s_slots[i].dirty = false;
    }
}

bool infinity_slot_dirty(uint8_t slot) {
    if (slot >= INFINITY_NUM_SLOTS) return false;
    return s_slots[slot].dirty;
}

void infinity_slot_clear_dirty(uint8_t slot) {
    if (slot < INFINITY_NUM_SLOTS) {
        s_slots[slot].dirty = false;
    }
}
