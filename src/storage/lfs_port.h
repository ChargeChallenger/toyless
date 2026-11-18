#ifndef TOYLESS_LFS_PORT_H
#define TOYLESS_LFS_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include "lfs.h"

// Flash layout for LittleFS on RP2040 (2MB onboard flash)
// 0x000000 - 0x07FFFF (512 KB): Firmware code & rodata
// 0x080000 - 0x1FFFFF (1536 KB): LittleFS filesystem partition
#define LFS_FLASH_OFFSET (512 * 1024)
#define LFS_FLASH_SIZE (1536 * 1024)

#define LFS_SECTOR_SIZE 4096
#define LFS_PAGE_SIZE 256
#define LFS_BLOCK_COUNT (LFS_FLASH_SIZE / LFS_SECTOR_SIZE) // 384 blocks

// Initialize and mount LittleFS on RP2040 onboard flash
// Automatically formats if partition is unformatted or corrupted.
// Ensures /figures and /saves directories exist.
int lfs_port_init(void);

// Get pointer to mounted LittleFS instance
lfs_t *lfs_port_get_fs(void);

// Format LittleFS partition on flash
int lfs_port_format(void);

#endif
