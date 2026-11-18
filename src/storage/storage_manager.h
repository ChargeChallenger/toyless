#ifndef TOYLESS_STORAGE_MANAGER_H
#define TOYLESS_STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol/infinity_protocol.h"

#define STORAGE_DEBOUNCE_MS 1500
#define STORAGE_MAX_PATH_LEN 64

// Initialize the storage manager on Core 1:
// 1. Mounts LittleFS (via lfs_port_init)
// 2. Restores active figures onto slots from /saves/active_slots.bin (if present)
// 3. Otherwise starts with a clean empty base
int storage_manager_init(void);

// Main periodic task called from Core 1 loop.
// Handles debounce timers and flushes dirty figure saves to LittleFS.
void storage_manager_task(void);

// Mark a slot as dirty when the game writes to it (received via PORTAL_EVT_FIGURE_DIRTY)
void storage_manager_mark_dirty(uint8_t slot);

// Assign a figure file from LittleFS to a slot and load it onto the portal.
// Sends PORTAL_CMD_SET_FIGURE to Core 0 via ICC.
bool storage_manager_set_slot_figure(uint8_t slot, const char *path);

// Clear a figure from a slot.
// Sends PORTAL_CMD_REMOVE_FIGURE to Core 0 via ICC.
bool storage_manager_clear_slot(uint8_t slot);

// Get the file path currently assigned to a slot (or empty string if slot is empty)
const char *storage_manager_get_slot_path(uint8_t slot);

#endif