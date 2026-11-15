#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef PICO_BUILD
#include "pico/rand.h"
#else
static inline uint32_t get_rand_32(void) {
    return ((uint32_t)(rand() & 0xFFFF) << 16) | (uint32_t)(rand() & 0xFFFF);
}
#endif

#include "xsm3.h"
#include "excrypt.h"
#include "usbdsec.h"

// Disable debugging printf by default
#define XSM3_printf(...) ((void)0)

// Identification data taken from an official wired controller
const uint8_t xsm3_id_data_ms_controller[0x1D] = {
    0x49, 0x4B, 0x00, 0x00, 0x17, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x00, 0x00, 0x80, 0x02, 0x5E, 0x04, 0x8E, 0x02,
    0x03, 0x00, 0x01, 0x01, 0x16
};

// Static global keys from KeyVault (shared across every retail system)
static const uint8_t xsm3_key_0x1D[0x10] __attribute__((aligned(8))) = {
    0xE3, 0x5B, 0xFB, 0x1C, 0xCD, 0xAD, 0x32, 0x5B,
    0xF7, 0x0E, 0x07, 0xFD, 0x62, 0x3D, 0xA7, 0xC4
};
static const uint8_t xsm3_key_0x1E[0x10] __attribute__((aligned(8))) = {
    0x8F, 0x29, 0x08, 0x38, 0x0B, 0x5B, 0xFE, 0x68,
    0x7C, 0x26, 0x46, 0x2A, 0x51, 0xF2, 0xBC, 0x19
};

// Retail root keys for generating 0x23/0x24 keys from console ID
static const uint8_t xsm3_root_key_0x23[0x10] __attribute__((aligned(8))) = {
    0x82, 0x80, 0x78, 0x68, 0x3A, 0x52, 0x3A, 0x98,
    0x10, 0xF4, 0x0C, 0x12, 0x70, 0x66, 0xDC, 0xBA
};
static const uint8_t xsm3_root_key_0x24[0x10] __attribute__((aligned(8))) = {
    0x66, 0x62, 0x1A, 0x78, 0xF8, 0x60, 0x9C, 0x8A,
    0x26, 0x9A, 0x04, 0xAE, 0xD8, 0x5C, 0x1E, 0xC8
};

// Response to give to the given challenge command
uint8_t xsm3_challenge_response[0x30] __attribute__((aligned(8)));
// Console ID fetched from challenge init packet
uint8_t xsm3_console_id[0x8] __attribute__((aligned(8)));

// Buffers to store keys fetched from KV
static uint8_t xsm3_kv_2des_key_1[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_kv_2des_key_2[0x10] __attribute__((aligned(8)));
static bool xsm3_has_kv_keys = false;

// Temporary buffers
static uint8_t xsm3_decryption_buffer[0x30] __attribute__((aligned(8)));
static uint8_t xsm3_identification_data[0x20] __attribute__((aligned(8)));
static uint8_t xsm3_random_console_data[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_random_console_data_enc[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_random_console_data_swap[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_random_console_data_swap_enc[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_random_controller_data[0x10] __attribute__((aligned(8)));
static uint8_t xsm3_challenge_init_hash[0x14] __attribute__((aligned(8)));

void xsm3_initialise_state(void) {
    memset(xsm3_challenge_response, 0, sizeof(xsm3_challenge_response));
    memset(xsm3_console_id, 0, sizeof(xsm3_console_id));
    memset(xsm3_kv_2des_key_1, 0, sizeof(xsm3_kv_2des_key_1));
    memset(xsm3_kv_2des_key_2, 0, sizeof(xsm3_kv_2des_key_2));
    memset(xsm3_decryption_buffer, 0, sizeof(xsm3_decryption_buffer));
    memset(xsm3_identification_data, 0, sizeof(xsm3_identification_data));
    memset(xsm3_random_console_data, 0, sizeof(xsm3_random_console_data));
    memset(xsm3_random_console_data_enc, 0, sizeof(xsm3_random_console_data_enc));
    memset(xsm3_random_console_data_swap, 0, sizeof(xsm3_random_console_data_swap));
    memset(xsm3_random_console_data_swap_enc, 0, sizeof(xsm3_random_console_data_swap_enc));
    memset(xsm3_random_controller_data, 0, sizeof(xsm3_random_controller_data));
    memset(xsm3_challenge_init_hash, 0, sizeof(xsm3_challenge_init_hash));
    xsm3_has_kv_keys = false;
}

static uint8_t xsm3_calculate_checksum(const uint8_t *packet) {
    uint8_t packet_length = packet[0x4] + 0x5;
    uint8_t checksum = 0x00;
    for (int i = 0x5; i < packet_length; i++) {
        checksum ^= packet[i];
    }
    return checksum;
}

static bool xsm3_verify_checksum(const uint8_t *packet) {
    uint8_t packet_length = packet[0x4] + 0x5;
    return (xsm3_calculate_checksum(packet) == packet[packet_length]);
}

void xsm3_set_identification_data(const uint8_t id_data[0x1D]) {
    if (!xsm3_verify_checksum(id_data)) {
        XSM3_printf("[ Checksum failed when setting identification data! ]\n");
    }

    id_data += 0x5;

    // Serial number (0xC), unknown (0x2), category node (0x1)
    memcpy(xsm3_identification_data, id_data, 0xF);
    // VID (0x2)
    memcpy(xsm3_identification_data + 0x10, id_data + 0xF, sizeof(uint16_t));
    // PID (0x2)
    memcpy(xsm3_identification_data + 0x12, id_data + 0x11, sizeof(uint16_t));
    // Unknown (0x1)
    memcpy(xsm3_identification_data + 0x14, id_data + 0x13, sizeof(uint8_t));
    // Unknown (0x1)
    memcpy(xsm3_identification_data + 0x15, id_data + 0x16, sizeof(uint8_t));
    // Unknown (0x2)
    memcpy(xsm3_identification_data + 0x16, id_data + 0x14, sizeof(uint16_t));
}

void xsm3_import_kv_keys(const uint8_t key1[0x10], const uint8_t key2[0x10]) {
    xsm3_has_kv_keys = true;
    memcpy(xsm3_kv_2des_key_1, key1, sizeof(xsm3_kv_2des_key_1));
    memcpy(xsm3_kv_2des_key_2, key2, sizeof(xsm3_kv_2des_key_2));
}

static void xsm3_generate_kv_keys(const uint8_t console_id[0x8]) {
    xsm3_has_kv_keys = true;
    uint8_t console_id_hash[0x14];
    ExCryptSha(console_id, 0x8, NULL, 0, NULL, 0, console_id_hash, 0x14);
    UsbdSecXSM3AuthenticationCrypt(xsm3_root_key_0x23, console_id_hash, 0x10, xsm3_kv_2des_key_1, 1);
    UsbdSecXSM3AuthenticationCrypt(xsm3_root_key_0x24, console_id_hash + 0x4, 0x10, xsm3_kv_2des_key_2, 1);
}

bool xsm3_do_challenge_init(uint8_t challenge_packet[0x22]) {
    uint8_t incoming_packet_mac[0x8];
    uint8_t response_packet_mac[0x8];
    int i;

    if (!xsm3_verify_checksum(challenge_packet)) {
        XSM3_printf("[ Checksum failed when validating challenge init! ]\n");
        return false;
    }

    UsbdSecXSM3AuthenticationCrypt(xsm3_key_0x1D, challenge_packet + 0x5, 0x18, xsm3_decryption_buffer, 0);
    memcpy(xsm3_random_console_data, xsm3_decryption_buffer, 0x10);
    memcpy(xsm3_console_id, xsm3_decryption_buffer + 0x10, 0x8);

    UsbdSecXSM3AuthenticationMac(xsm3_key_0x1E, NULL, challenge_packet + 5, 0x18, incoming_packet_mac);
    if (memcmp(incoming_packet_mac + 4, challenge_packet + 0x5 + 0x18, 0x4) != 0) {
        XSM3_printf("[ MAC failed when validating challenge init! ]\n");
        return false;
    }

    if (!xsm3_has_kv_keys) xsm3_generate_kv_keys(xsm3_console_id);

    memcpy(xsm3_random_console_data_swap, xsm3_random_console_data + 0x8, 0x8);
    memcpy(xsm3_random_console_data_swap + 0x8, xsm3_random_console_data, 0x8);
    UsbdSecXSM3AuthenticationCrypt(xsm3_kv_2des_key_1, xsm3_random_console_data, 0x10, xsm3_random_console_data_enc, 1);
    UsbdSecXSM3AuthenticationCrypt(xsm3_kv_2des_key_2, xsm3_random_console_data_swap, 0x10, xsm3_random_console_data_swap_enc, 1);

    // Hardware ROSC TRNG entropy generation
    for (i = 0; i < 0x10; i += 4) {
        uint32_t r = get_rand_32();
        memcpy(xsm3_random_controller_data + i, &r, 4);
    }

    memset(xsm3_challenge_response, 0, sizeof(xsm3_challenge_response));
    memset(xsm3_decryption_buffer, 0, sizeof(xsm3_decryption_buffer));

    xsm3_challenge_response[0] = 0x49; // Packet magic 'IL'
    xsm3_challenge_response[1] = 0x4C;
    xsm3_challenge_response[4] = 0x28; // Packet length (40 bytes payload)

    memcpy(xsm3_decryption_buffer, xsm3_random_controller_data, 0x10);
    memcpy(xsm3_decryption_buffer + 0x10, xsm3_random_console_data, 0x10);
    ExCryptSha(xsm3_decryption_buffer, 0x20, NULL, 0, NULL, 0, xsm3_challenge_init_hash, 0x14);

    UsbdSecXSM3AuthenticationCrypt(xsm3_random_console_data_enc, xsm3_decryption_buffer, 0x20, xsm3_challenge_response + 0x5, 1);
    UsbdSecXSM3AuthenticationMac(xsm3_random_console_data_swap_enc, NULL, xsm3_challenge_response + 0x5, 0x20, response_packet_mac);
    UsbdSecXSMAuthenticationAcr(xsm3_console_id, xsm3_identification_data, response_packet_mac, xsm3_challenge_response + 0x5 + 0x20);
    xsm3_challenge_response[0x5 + 0x28] = xsm3_calculate_checksum(xsm3_challenge_response);

    memcpy(xsm3_random_console_data, xsm3_random_controller_data + 0xC, 0x4);
    memcpy(xsm3_random_console_data + 0x4, xsm3_random_console_data + 0xC, 0x4);

    return true;
}

bool xsm3_do_challenge_verify(uint8_t challenge_packet[0x16]) {
    uint8_t incoming_packet_mac[0x8];

    if (!xsm3_verify_checksum(challenge_packet)) {
        XSM3_printf("[ Checksum failed when validating challenge verify! ]\n");
        return false;
    }

    UsbdSecXSM3AuthenticationCrypt(xsm3_random_controller_data, challenge_packet + 0x5, 0x8, xsm3_decryption_buffer, 0);
    memcpy(xsm3_random_console_data + 0x8, xsm3_decryption_buffer, 0x8);

    UsbdSecXSM3AuthenticationMac(xsm3_challenge_init_hash, xsm3_random_console_data, challenge_packet + 0x5, 0x8, incoming_packet_mac);
    if (memcmp(incoming_packet_mac, challenge_packet + 0x5 + 0x8, 0x8) != 0) {
        XSM3_printf("[ MAC failed when validating challenge verify! ]\n");
        return false;
    }

    memset(xsm3_challenge_response, 0, sizeof(xsm3_challenge_response));
    memset(xsm3_decryption_buffer, 0, sizeof(xsm3_decryption_buffer));

    xsm3_challenge_response[0] = 0x49;
    xsm3_challenge_response[1] = 0x4C;
    xsm3_challenge_response[4] = 0x10;

    UsbdSecXSMAuthenticationAcr(xsm3_console_id, xsm3_identification_data, xsm3_random_console_data + 0x8, xsm3_decryption_buffer);
    UsbdSecXSM3AuthenticationCrypt(xsm3_random_console_data_enc, xsm3_decryption_buffer, 0x8, xsm3_challenge_response + 0x5, 1);
    UsbdSecXSM3AuthenticationMac(xsm3_random_console_data_swap_enc, xsm3_random_console_data, xsm3_challenge_response + 0x5, 0x8, xsm3_challenge_response + 0x5 + 0x8);
    xsm3_challenge_response[0x5 + 0x10] = xsm3_calculate_checksum(xsm3_challenge_response);

    return true;
}
