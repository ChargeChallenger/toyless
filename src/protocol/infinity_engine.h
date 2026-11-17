#ifndef INFINITY_ENGINE_H
#define INFINITY_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "protocol/infinity_protocol.h"

// State of single figure slot
typedef struct
{
    uint8_t data[INFINITY_FIGURE_SIZE];
    bool present;
    bool dirty;
    uint8_t order_added;
} infinity_figure_slot_t;

typedef infinity_figure_slot_t infinity_figure_slot;

// Initialize the engine and slot states
void infinity_engine_init(void);

// Process an incoming game command (starts with 0xFF)
// Writes response packet (starts with 0xAA)
// Returns length of response with bytes
int infinity_process_command(const uint8_t *pkt, uint8_t *out);

// Pop the next figure placement/removal event notification (starts with 0xAB)
int infinity_pop_notification(uint8_t *out);

// Check if any notification are pending in queue
bool infinity_notification_pending(void);

// Place a figure into slot from a 320 byte data dump
// Pushes an addition notification (0xAB) to the queue
bool infinity_load_figure(uint8_t slot, const uint8_t *data320);

// Remove a figure from slot
// Pushes a removal notification (0xAB) to the queue
bool infinity_remove_figure(uint8_t slot);

// Query slot presense
bool infinity_slot_present(uint8_t slot);
const uint8_t *infinity_slot_data(uint8_t slot);

// Dirty tracking (set when the game writes to a figure via 0xA3)
bool infinity_figures_dirty(void);
void infinity_clear_dirty(void);
bool infinity_slot_dirty(uint8_t slot);
void infinity_slot_clear_dirty(uint8_t slot);

#endif