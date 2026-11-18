#ifndef TOYLESS_FIGURE_SYNTHESIZER_H
#define TOYLESS_FIGURE_SYNTHESIZER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FIGURE_DUMP_SIZE 320

// Synthesize a complete 320-byte Disney Infinity MIFARE dump for given toy_id.
// If custom_uid is NULL, a random 7-byte UID with 0x04 (NXP prefix) is generated.
// out_data320 must point to a 320-byte buffer.
bool figure_synthesizer_create(uint32_t toy_id, const uint8_t *custom_uid, uint8_t *out_data320);

// Synthesize figure and write directly to a LittleFS file path (e.g. "/figures/iron_man.bin")
bool figure_synthesizer_save_file(const char *path, uint32_t toy_id, const uint8_t *custom_uid);

// Extract toy_id from an existing 320-byte figure dump by decrypting Block 1.
// Returns 0 if decryption or CRC check fails.
uint32_t figure_synthesizer_get_toy_id(const uint8_t *data320);

#endif
