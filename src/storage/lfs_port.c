#include "storage/lfs_port.h"
#include "core/log.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

// Static LittleFS state and buffers (zero dynamic allocation)
static lfs_t s_lfs;
static struct lfs_config s_cfg;
static bool s_mounted = false;

static uint8_t s_read_buffer[LFS_PAGE_SIZE];
static uint8_t s_prog_buffer[LFS_PAGE_SIZE];
static uint8_t s_lookahead_buffer[128]; // 128 bytes = 1024 bits (covers 384 blocks)

static int lfs_flash_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    uint32_t flash_addr = XIP_BASE + LFS_FLASH_OFFSET + (block * LFS_SECTOR_SIZE) + off;
    memcpy(buffer, (const void *)flash_addr, size);
    return LFS_ERR_OK;
}

static int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    uint32_t flash_offs = LFS_FLASH_OFFSET + (block * LFS_SECTOR_SIZE) + off;

    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offs, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();

    return LFS_ERR_OK;
}

static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    uint32_t flash_offs = LFS_FLASH_OFFSET + (block * LFS_SECTOR_SIZE);

    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offs, LFS_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();

    return LFS_ERR_OK;
}

static int lfs_flash_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

static void lfs_port_setup_config(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.read = lfs_flash_read;
    s_cfg.prog = lfs_flash_prog;
    s_cfg.erase = lfs_flash_erase;
    s_cfg.sync = lfs_flash_sync;

    s_cfg.read_size = 1;
    s_cfg.prog_size = LFS_PAGE_SIZE;
    s_cfg.block_size = LFS_SECTOR_SIZE;
    s_cfg.block_count = LFS_BLOCK_COUNT;
    s_cfg.cache_size = LFS_PAGE_SIZE;
    s_cfg.lookahead_size = sizeof(s_lookahead_buffer);
    s_cfg.block_cycles = 500;

    s_cfg.read_buffer = s_read_buffer;
    s_cfg.prog_buffer = s_prog_buffer;
    s_cfg.lookahead_buffer = s_lookahead_buffer;
}

int lfs_port_format(void)
{
    log_printf("[Storage] Formatting LittleFS partition...\n");
    lfs_port_setup_config();
    int err = lfs_format(&s_lfs, &s_cfg);
    if (err < 0)
    {
        log_printf("[Storage] LittleFS format failed: %d\n", err);
        return err;
    }
    log_printf("[Storage] LittleFS format succeeded.\n");
    return 0;
}

int lfs_port_init(void)
{
    if (s_mounted)
    {
        return 0;
    }

    lfs_port_setup_config();

    log_printf("[Storage] Mounting LittleFS at offset 0x%X (%u KB)...\n",
               (unsigned int)LFS_FLASH_OFFSET, (unsigned int)(LFS_FLASH_SIZE / 1024));

    int err = lfs_mount(&s_lfs, &s_cfg);
    if (err != LFS_ERR_OK)
    {
        log_printf("[Storage] LittleFS mount failed (%d). Formatting new filesystem...\n", err);
        err = lfs_format(&s_lfs, &s_cfg);
        if (err != LFS_ERR_OK)
        {
            log_printf("[Storage] LittleFS format error: %d\n", err);
            return err;
        }

        err = lfs_mount(&s_lfs, &s_cfg);
        if (err != LFS_ERR_OK)
        {
            log_printf("[Storage] LittleFS mount error after format: %d\n", err);
            return err;
        }
    }

    s_mounted = true;
    log_printf("[Storage] LittleFS mounted successfully.\n");

    // Ensure default directories exist
    lfs_mkdir(&s_lfs, "/figures");
    lfs_mkdir(&s_lfs, "/saves");

    return 0;
}

lfs_t *lfs_port_get_fs(void)
{
    return s_mounted ? &s_lfs : NULL;
}