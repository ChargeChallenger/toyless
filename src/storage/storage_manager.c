#include "storage/storage_manager.h"
#include "storage/lfs_port.h"
#include "core/icc.h"
#include "core/log.h"
#include "protocol/infinity_engine.h"
#include <string.h>
#include <stdio.h>

#ifdef PICO_BOARD
#include "pico/stdlib.h"
#endif

#define ACTIVE_SLOTS_FILE "/saves/active_slots.bin"

typedef struct {
    char path[STORAGE_MAX_PATH_LEN];
    bool dirty;
    uint32_t last_dirty_ms;
} slot_storage_entry_t;

static slot_storage_entry_t s_slots[INFINITY_NUM_SLOTS];
static bool s_initialized = false;

static void save_active_slots_file(void)
{
    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs) {
        return;
    }

    char paths[INFINITY_NUM_SLOTS][STORAGE_MAX_PATH_LEN];
    memset(paths, 0, sizeof(paths));

    for (uint8_t i = 0; i < INFINITY_NUM_SLOTS; i++) {
        memcpy(paths[i], s_slots[i].path, STORAGE_MAX_PATH_LEN);
    }

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, ACTIVE_SLOTS_FILE, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        log_printf("[StorageManager] Failed to save active slots file: %d\n", err);
        return;
    }

    lfs_file_write(lfs, &file, paths, sizeof(paths));
    lfs_file_close(lfs, &file);
}

static void flush_slot_save(uint8_t slot)
{
    if (slot >= INFINITY_NUM_SLOTS || s_slots[slot].path[0] == '\0') {
        s_slots[slot].dirty = false;
        return;
    }

    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs) {
        return;
    }

    const uint8_t *slot_data = infinity_slot_data(slot);
    if (!slot_data || !infinity_slot_present(slot)) {
        s_slots[slot].dirty = false;
        return;
    }

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, s_slots[slot].path, LFS_O_WRONLY);
    if (err < 0) {
        log_printf("[StorageManager] Failed to open '%s' for saving: %d\n", s_slots[slot].path, err);
        s_slots[slot].dirty = false;
        return;
    }

    lfs_ssize_t written = lfs_file_write(lfs, &file, slot_data, INFINITY_FIGURE_SIZE);
    lfs_file_close(lfs, &file);

    s_slots[slot].dirty = false;

    if (written == INFINITY_FIGURE_SIZE) {
        log_printf("[StorageManager] Debounced save: flushed slot %d to '%s'\n",
                   slot, s_slots[slot].path);
    } else {
        log_printf("[StorageManager] Error writing slot %d save to '%s'\n",
                   slot, s_slots[slot].path);
    }
}

int storage_manager_init(void)
{
    if (s_initialized) {
        return 0;
    }

    // 1. Initialize and mount LittleFS
    int err = lfs_port_init();
    if (err < 0) {
        log_printf("[StorageManager] LittleFS initialization failed: %d\n", err);
        return err;
    }

    memset(s_slots, 0, sizeof(s_slots));

    // 2. Try to restore active slots from persistent storage
    lfs_t *lfs = lfs_port_get_fs();
    if (lfs) {
        lfs_file_t file;
        int open_err = lfs_file_open(lfs, &file, ACTIVE_SLOTS_FILE, LFS_O_RDONLY);
        if (open_err >= 0) {
            char paths[INFINITY_NUM_SLOTS][STORAGE_MAX_PATH_LEN];
            lfs_ssize_t read_bytes = lfs_file_read(lfs, &file, paths, sizeof(paths));
            lfs_file_close(lfs, &file);

            if (read_bytes == sizeof(paths)) {
                log_printf("[StorageManager] Restoring active slots from file...\n");
                for (uint8_t i = 0; i < INFINITY_NUM_SLOTS; i++) {
                    if (paths[i][0] != '\0') {
                        storage_manager_set_slot_figure(i, paths[i]);
                    }
                }
            }
        } else {
            log_printf("[StorageManager] No active slots file found. Starting with clean empty base.\n");
        }
    }

    s_initialized = true;
    log_printf("[StorageManager] Storage manager initialized successfully.\n");
    return 0;
}

void storage_manager_task(void)
{
    if (!s_initialized) {
        return;
    }

#ifdef PICO_BOARD
    uint32_t now = to_ms_since_boot(get_absolute_time());
#else
    uint32_t now = 0;
#endif

    for (uint8_t i = 0; i < INFINITY_NUM_SLOTS; i++) {
        if (s_slots[i].dirty && (now - s_slots[i].last_dirty_ms >= STORAGE_DEBOUNCE_MS)) {
            flush_slot_save(i);
        }
    }
}

void storage_manager_mark_dirty(uint8_t slot)
{
    if (slot >= INFINITY_NUM_SLOTS) {
        return;
    }

    s_slots[slot].dirty = true;
#ifdef PICO_BOARD
    s_slots[slot].last_dirty_ms = to_ms_since_boot(get_absolute_time());
#else
    s_slots[slot].last_dirty_ms = 0;
#endif

    log_printf("[StorageManager] Slot %d marked dirty, debounce timer reset (%u ms)\n",
               slot, (unsigned int)STORAGE_DEBOUNCE_MS);
}

bool storage_manager_set_slot_figure(uint8_t slot, const char *path)
{
    if (slot >= INFINITY_NUM_SLOTS || !path || path[0] == '\0') {
        return false;
    }

    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs) {
        return false;
    }

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        log_printf("[StorageManager] Cannot open figure '%s' for slot %d (err %d)\n", path, slot, err);
        return false;
    }

    uint8_t dump[INFINITY_FIGURE_SIZE];
    lfs_ssize_t read_bytes = lfs_file_read(lfs, &file, dump, sizeof(dump));
    lfs_file_close(lfs, &file);

    if (read_bytes != INFINITY_FIGURE_SIZE) {
        log_printf("[StorageManager] Invalid figure dump size in '%s': read %d bytes\n", path, (int)read_bytes);
        return false;
    }

    // If previous figure on this slot was dirty, flush it first
    if (s_slots[slot].dirty && s_slots[slot].path[0] != '\0') {
        flush_slot_save(slot);
    }

    strncpy(s_slots[slot].path, path, STORAGE_MAX_PATH_LEN - 1);
    s_slots[slot].path[STORAGE_MAX_PATH_LEN - 1] = '\0';
    s_slots[slot].dirty = false;

    // Send command to Core 0 via ICC
    portal_cmd_t cmd;
    cmd.type = PORTAL_CMD_SET_FIGURE;
    cmd.slot = slot;
    memcpy(cmd.figure_data, dump, INFINITY_FIGURE_SIZE);
    icc_send_cmd(&cmd);

    save_active_slots_file();
    log_printf("[StorageManager] Loaded figure '%s' into slot %d\n", path, slot);
    return true;
}

bool storage_manager_clear_slot(uint8_t slot)
{
    if (slot >= INFINITY_NUM_SLOTS) {
        return false;
    }

    // If figure was dirty, flush save before removing
    if (s_slots[slot].dirty && s_slots[slot].path[0] != '\0') {
        flush_slot_save(slot);
    }

    s_slots[slot].path[0] = '\0';
    s_slots[slot].dirty = false;

    // Send command to Core 0 via ICC
    portal_cmd_t cmd;
    cmd.type = PORTAL_CMD_REMOVE_FIGURE;
    cmd.slot = slot;
    icc_send_cmd(&cmd);

    save_active_slots_file();
    log_printf("[StorageManager] Cleared figure from slot %d\n", slot);
    return true;
}

const char *storage_manager_get_slot_path(uint8_t slot)
{
    if (slot >= INFINITY_NUM_SLOTS) {
        return "";
    }
    return s_slots[slot].path;
}
