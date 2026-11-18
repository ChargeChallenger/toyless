#include "storage/figure_synthesizer.h"
#include "storage/lfs_port.h"
#include "core/log.h"
#include "excrypt_sha.h"
#include "aes.h"
#include <string.h>
#include <stdlib.h>

#ifdef PICO_BOARD
#include "pico/rand.h"
static void get_random_uid_bytes(uint8_t *out6)
{
    for (size_t i = 0; i < 6; i++) {
        out6[i] = (uint8_t)(get_rand_32() & 0xFF);
    }
}
#else
static void get_random_uid_bytes(uint8_t *out6)
{
    for (size_t i = 0; i < 6; i++) {
        out6[i] = (uint8_t)(rand() & 0xFF);
    }
}
#endif

// Disney Infinity 31-byte key derivation prefix
static const uint8_t s_sha1_prefix[31] = {
    0xAF, 0x62, 0xD2, 0xEC, 0x04, 0x91, 0x96, 0x8C, 0xC5, 0x2A, 0x1A, 0x71, 0x65, 0xF8, 0x65, 0xFE,
    0x28, 0x63, 0x29, 0x20, 0x44, 0x69, 0x73, 0x6e, 0x65, 0x79, 0x20, 0x32, 0x30, 0x31, 0x33
};

// CRC32 table for Disney Infinity block 1 checksum
static const uint32_t s_crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static uint32_t infinity_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)(crc & 0xFF) ^ buf[i];
        crc = (crc >> 8) ^ s_crc32_table[idx];
    }
    return crc;
}

static void derive_aes_key(const uint8_t *uid7, uint8_t *out_key16)
{
    uint8_t digest[20];
    ExCryptSha(s_sha1_prefix, sizeof(s_sha1_prefix), uid7, 7, NULL, 0, digest, sizeof(digest));

    // AES key is the first 16 bytes with 32-bit word endianness swapped
    for (int i = 0; i < 4; i++) {
        out_key16[i * 4 + 0] = digest[i * 4 + 3];
        out_key16[i * 4 + 1] = digest[i * 4 + 2];
        out_key16[i * 4 + 2] = digest[i * 4 + 1];
        out_key16[i * 4 + 3] = digest[i * 4 + 0];
    }
}

bool figure_synthesizer_create(uint32_t toy_id, const uint8_t *custom_uid, uint8_t *out_data320)
{
    if (!out_data320) {
        return false;
    }

    memset(out_data320, 0, FIGURE_DUMP_SIZE);

    // 1. Establish 7-byte UID
    uint8_t uid[7];
    if (custom_uid) {
        memcpy(uid, custom_uid, 7);
    } else {
        uid[0] = 0x04; // NXP manufacturer prefix
        get_random_uid_bytes(&uid[1]);
    }

    // Block 0: UID + standard MIFARE configuration bytes
    memcpy(&out_data320[0], uid, 7);
    const uint8_t block0_tail[4] = { 0x89, 0x44, 0x00, 0xC2 };
    memcpy(&out_data320[7], block0_tail, 4);

    // 2. Set up Sector Trailers (MIFARE keys & access control bits)
    // Sector 0 trailer at 0x30: access bits 0x17878E
    const uint8_t sector0_trailer[16] = {
        0x29, 0x47, 0x79, 0x53, 0x67, 0x26, 0x17, 0x87, 0x8E, 0x00, 0x29, 0x47, 0x79, 0x53, 0x67, 0x26
    };
    memcpy(&out_data320[0x30], sector0_trailer, 16);

    // Sectors 1..4 trailers at 0x70, 0xB0, 0xF0, 0x130: access bits 0x778788
    const uint8_t sector_other_trailer[16] = {
        0x29, 0x47, 0x79, 0x53, 0x67, 0x26, 0x77, 0x87, 0x88, 0x00, 0x29, 0x47, 0x79, 0x53, 0x67, 0x26
    };
    for (int sec = 1; sec <= 4; sec++) {
        memcpy(&out_data320[sec * 0x40 + 0x30], sector_other_trailer, 16);
    }

    // 3. Derive 16-byte AES-128 key from SHA-1(prefix + UID)
    uint8_t aes_key[16];
    derive_aes_key(uid, aes_key);

    // 4. Build Block 1 (Figure metadata)
    uint8_t block1[16] = { 0 };
    block1[0] = 0x00;
    block1[1] = (uint8_t)((toy_id >> 16) & 0xFF);
    block1[2] = (uint8_t)((toy_id >> 8) & 0xFF);
    block1[3] = (uint8_t)(toy_id & 0xFF);
    block1[4] = 0x0D; // Year (2013)
    block1[5] = 0x08; // Month (August)
    block1[6] = 0x12; // Day (18)
    block1[7] = 0x00;
    block1[8] = 0x00;
    block1[9] = 0x01;
    block1[10] = 0xD1;
    block1[11] = 0x1F;

    uint32_t checksum = infinity_crc32(block1, 12);
    block1[12] = (uint8_t)((checksum >> 24) & 0xFF);
    block1[13] = (uint8_t)((checksum >> 16) & 0xFF);
    block1[14] = (uint8_t)((checksum >> 8) & 0xFF);
    block1[15] = (uint8_t)(checksum & 0xFF);

    // 5. Encrypt Block 1 with AES-128-CBC (IV = 0)
    struct AES_ctx ctx;
    uint8_t iv[16] = { 0 };
    AES_init_ctx_iv(&ctx, aes_key, iv);
    AES_CBC_encrypt_buffer(&ctx, block1, 16);
    memcpy(&out_data320[16], block1, 16);

    // 6. Encrypt Blank Block (16 zero bytes) for save/progression blocks (4, 8, 12, 13)
    uint8_t blank_block[16] = { 0 };
    memset(iv, 0, 16);
    AES_init_ctx_iv(&ctx, aes_key, iv);
    AES_CBC_encrypt_buffer(&ctx, blank_block, 16);

    memcpy(&out_data320[4 * 16], blank_block, 16);
    memcpy(&out_data320[8 * 16], blank_block, 16);
    memcpy(&out_data320[12 * 16], blank_block, 16);
    memcpy(&out_data320[13 * 16], blank_block, 16);

    return true;
}

uint32_t figure_synthesizer_get_toy_id(const uint8_t *data320)
{
    if (!data320) {
        return 0;
    }

    // 1. Derive AES key from UID in Block 0
    uint8_t aes_key[16];
    derive_aes_key(&data320[0], aes_key);

    // 2. Decrypt Block 1 (bytes 16..31)
    uint8_t decrypted[16];
    memcpy(decrypted, &data320[16], 16);

    struct AES_ctx ctx;
    uint8_t iv[16] = { 0 };
    AES_init_ctx_iv(&ctx, aes_key, iv);
    AES_CBC_decrypt_buffer(&ctx, decrypted, 16);

    // 3. Verify CRC32 of first 12 bytes
    uint32_t expected_crc = ((uint32_t)decrypted[12] << 24) |
                            ((uint32_t)decrypted[13] << 16) |
                            ((uint32_t)decrypted[14] << 8)  |
                            ((uint32_t)decrypted[15]);

    uint32_t calc_crc = infinity_crc32(decrypted, 12);
    if (calc_crc != expected_crc) {
        log_printf("[Synthesizer] CRC mismatch for figure: calc 0x%08X != expected 0x%08X\n",
                   (unsigned int)calc_crc, (unsigned int)expected_crc);
        return 0;
    }

    uint32_t toy_id = ((uint32_t)decrypted[1] << 16) |
                      ((uint32_t)decrypted[2] << 8)  |
                      ((uint32_t)decrypted[3]);

    return toy_id;
}

bool figure_synthesizer_save_file(const char *path, uint32_t toy_id, const uint8_t *custom_uid)
{
    lfs_t *lfs = lfs_port_get_fs();
    if (!lfs || !path) {
        return false;
    }

    uint8_t data320[FIGURE_DUMP_SIZE];
    if (!figure_synthesizer_create(toy_id, custom_uid, data320)) {
        return false;
    }

    lfs_file_t file;
    int err = lfs_file_open(lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        log_printf("[Synthesizer] Failed to open file '%s': %d\n", path, err);
        return false;
    }

    lfs_ssize_t written = lfs_file_write(lfs, &file, data320, FIGURE_DUMP_SIZE);
    lfs_file_close(lfs, &file);

    if (written != FIGURE_DUMP_SIZE) {
        log_printf("[Synthesizer] Failed to write full dump to '%s'\n", path);
        return false;
    }

    log_printf("[Synthesizer] Successfully created and saved figure 0x%06X to '%s'\n",
               (unsigned int)toy_id, path);
    return true;
}
