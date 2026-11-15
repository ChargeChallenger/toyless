#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "excrypt.h"
#include "usbdsec.h"
#include "xsm3.h"

// ---------------------------------------------------------------------------
// 1. Test SHA-1
// ---------------------------------------------------------------------------
static const struct {
    int len;
    const char *expected_hex;
} sha1_boundary_vectors[] = {
    {0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {1, "6dcd4ce23d88e2ee9568ba546c007c63d9131c1b"},
    {55, "5021b3d42aa093bffc34eedd7a1455f3624bc552"},
    {56, "6b45e3cf1eb3324b9fd4df3b83d89c4c2c4ca896"},
    {57, "e8d6ea5c627fc8676fa662677b028640844dc35c"},
    {63, "0ec86b3f3ac34ad860fa8da56bcca03a54018049"},
    {64, "30b86e44e6001403827a62c58b08893e77cf121f"},
    {65, "826b7e7a7af8a529ae1c7443c23bf185c0ad440c"},
    {119, "293e3964d2b4d4ba9d21991b8388283b4f09b935"},
    {120, "a1298700a534e357b7130c74e277fe5428d43baf"},
    {121, "b9bd07da310bbf697d195fd2c2440d567f33ea95"},
    {128, "2927490ade868795ecdd8febe05214cbd243ef35"},
    {-1, NULL}
};

static void test_sha1(void) {
    printf("[TEST] Running SHA-1 tests...\n");

    // FIPS Vector: "abc" -> a9993e364706816aba3e25717850c26c9cd0d89d
    const uint8_t input_abc[] = "abc";
    uint8_t hash_abc[20];
    ExCryptSha(input_abc, 3, NULL, 0, NULL, 0, hash_abc, 20);

    const uint8_t expected_abc[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    assert(memcmp(hash_abc, expected_abc, 20) == 0);

    // Boundary vectors
    uint8_t buf[256];
    memset(buf, 'A', sizeof(buf));

    for (int i = 0; sha1_boundary_vectors[i].expected_hex != NULL; i++) {
        uint8_t out[20];
        ExCryptSha(buf, sha1_boundary_vectors[i].len, NULL, 0, NULL, 0, out, 20);
        char hex[41];
        for (int j = 0; j < 20; j++) {
            sprintf(hex + j * 2, "%02x", out[j]);
        }
        assert(strcmp(hex, sha1_boundary_vectors[i].expected_hex) == 0);
    }

    printf("  -> SHA-1 tests (FIPS + all boundary lengths) passed!\n");
}

// ---------------------------------------------------------------------------
// 2. Test DES / 3DES ECB & CBC
// ---------------------------------------------------------------------------
static void test_des3(void) {
    printf("[TEST] Running 3DES tests...\n");
    const uint8_t key[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01
    };
    const uint8_t plaintext[16] = {'H', 'e', 'l', 'l', 'o', ' ', '3', 'D', 'E', 'S', ' ', 'W', 'o', 'r', 'l', 'd'};
    uint8_t ciphertext[16];
    uint8_t decrypted[16];

    // Encrypt with UsbdSecXSM3AuthenticationCrypt
    UsbdSecXSM3AuthenticationCrypt(key, plaintext, 16, ciphertext, 1);
    assert(memcmp(plaintext, ciphertext, 16) != 0);

    // Decrypt
    UsbdSecXSM3AuthenticationCrypt(key, ciphertext, 16, decrypted, 0);
    assert(memcmp(plaintext, decrypted, 16) == 0);

    // Test MAC calculation without salt
    uint8_t mac1[8];
    UsbdSecXSM3AuthenticationMac(key, NULL, ciphertext, 16, mac1);
    bool non_zero1 = false;
    for (int i = 0; i < 8; i++) {
        if (mac1[i] != 0) non_zero1 = true;
    }
    assert(non_zero1);

    // Test MAC calculation with salt
    uint8_t salt[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t mac2[8];
    UsbdSecXSM3AuthenticationMac(key, salt, ciphertext, 16, mac2);
    // Salt should be mutated (incremented)
    assert(salt[7] != 8 || salt[0] != 1);
    assert(memcmp(mac1, mac2, 8) != 0);

    printf("  -> 3DES Crypt and MAC tests passed!\n");
}

// ---------------------------------------------------------------------------
// 3. Test Controller Identification Checksum
// ---------------------------------------------------------------------------
static uint8_t calculate_checksum(const uint8_t *packet) {
    uint8_t packet_length = packet[0x4] + 0x5;
    uint8_t checksum = 0x00;
    for (int i = 0x5; i < packet_length; i++) {
        checksum ^= packet[i];
    }
    return checksum;
}

static void test_id_checksum(void) {
    printf("[TEST] Running ID Checksum test...\n");
    const uint8_t ms_controller[0x1D] = {
        0x49, 0x4B, 0x00, 0x00, 0x17, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x00, 0x00, 0x80, 0x02, 0x5E, 0x04, 0x8E, 0x02,
        0x03, 0x00, 0x01, 0x01, 0x16
    };
    uint8_t expected_cs = ms_controller[0x1C];
    uint8_t computed_cs = calculate_checksum(ms_controller);
    assert(computed_cs == expected_cs);
    printf("  -> ID Checksum passed (0x%02X == 0x%02X)!\n", computed_cs, expected_cs);
}

// ---------------------------------------------------------------------------
// 4. Test Full Simulated XSM3 Handshake
// ---------------------------------------------------------------------------
static const uint8_t xsm3_key_0x1D[0x10] = {
    0xE3, 0x5B, 0xFB, 0x1C, 0xCD, 0xAD, 0x32, 0x5B,
    0xF7, 0x0E, 0x07, 0xFD, 0x62, 0x3D, 0xA7, 0xC4
};
static const uint8_t xsm3_key_0x1E[0x10] = {
    0x8F, 0x29, 0x08, 0x38, 0x0B, 0x5B, 0xFE, 0x68,
    0x7C, 0x26, 0x46, 0x2A, 0x51, 0xF2, 0xBC, 0x19
};
static const uint8_t xsm3_root_key_0x23[0x10] = {
    0x82, 0x80, 0x78, 0x68, 0x3A, 0x52, 0x3A, 0x98,
    0x10, 0xF4, 0x0C, 0x12, 0x70, 0x66, 0xDC, 0xBA
};
static const uint8_t xsm3_root_key_0x24[0x10] = {
    0x66, 0x62, 0x1A, 0x78, 0xF8, 0x60, 0x9C, 0x8A,
    0x26, 0x9A, 0x04, 0xAE, 0xD8, 0x5C, 0x1E, 0xC8
};

static void test_xsm3_handshake_simulation(void) {
    printf("[TEST] Running Full XSM3 Handshake Simulation...\n");

    // === Part A: Valid Handshake ===
    // 1. Device init
    xsm3_initialise_state();
    xsm3_set_identification_data(xsm3_id_data_ms_controller);

    // 2. Mock Console prepares request 0x82 (Challenge Init)
    uint8_t console_id[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    uint8_t console_rand[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    };

    uint8_t plain_init[24];
    memcpy(plain_init, console_rand, 16);
    memcpy(plain_init + 16, console_id, 8);

    uint8_t enc_init[24];
    UsbdSecXSM3AuthenticationCrypt(xsm3_key_0x1D, plain_init, 24, enc_init, 1);

    uint8_t mac_init[8];
    UsbdSecXSM3AuthenticationMac(xsm3_key_0x1E, NULL, enc_init, 24, mac_init);

    uint8_t pkt_0x82[34];
    memset(pkt_0x82, 0, sizeof(pkt_0x82));
    pkt_0x82[0] = 0x49; // 'I'
    pkt_0x82[1] = 0x4C; // 'L'
    pkt_0x82[4] = 0x1C;
    memcpy(pkt_0x82 + 5, enc_init, 24);
    memcpy(pkt_0x82 + 5 + 24, mac_init + 4, 4); // last 4 bytes of 8-byte MAC
    pkt_0x82[33] = calculate_checksum(pkt_0x82);

    // Device processes valid 0x82
    bool init_ok = xsm3_do_challenge_init(pkt_0x82);
    assert(init_ok);
    assert(xsm3_challenge_response[0] == 0x49 && xsm3_challenge_response[1] == 0x4C);
    assert(xsm3_challenge_response[4] == 0x28);
    assert(calculate_checksum(xsm3_challenge_response) == xsm3_challenge_response[45]);

    // 3. Mock Console processes 0x83 response and prepares 0x87 verify packet
    uint8_t console_id_hash[20];
    ExCryptSha(console_id, 8, NULL, 0, NULL, 0, console_id_hash, 20);
    uint8_t kv_key1[16], kv_key2[16];
    UsbdSecXSM3AuthenticationCrypt(xsm3_root_key_0x23, console_id_hash, 16, kv_key1, 1);
    UsbdSecXSM3AuthenticationCrypt(xsm3_root_key_0x24, console_id_hash + 4, 16, kv_key2, 1);

    uint8_t rand_console_enc[16], rand_console_swap[16], rand_console_swap_enc[16];
    memcpy(rand_console_swap, console_rand + 8, 8);
    memcpy(rand_console_swap + 8, console_rand, 8);
    UsbdSecXSM3AuthenticationCrypt(kv_key1, console_rand, 16, rand_console_enc, 1);
    UsbdSecXSM3AuthenticationCrypt(kv_key2, rand_console_swap, 16, rand_console_swap_enc, 1);

    uint8_t dec_resp[32];
    UsbdSecXSM3AuthenticationCrypt(rand_console_enc, xsm3_challenge_response + 5, 32, dec_resp, 0);

    uint8_t controller_rand[16];
    memcpy(controller_rand, dec_resp, 16);
    assert(memcmp(dec_resp + 16, console_rand, 16) == 0);

    uint8_t challenge_init_hash[20];
    ExCryptSha(dec_resp, 32, NULL, 0, NULL, 0, challenge_init_hash, 20);

    uint8_t resp_mac[8];
    UsbdSecXSM3AuthenticationMac(rand_console_swap_enc, NULL, xsm3_challenge_response + 5, 32, resp_mac);

    // Update console's view of random_console_data
    uint8_t current_console_rand[16];
    memcpy(current_console_rand, controller_rand + 12, 4);
    memcpy(current_console_rand + 4, console_rand + 12, 4);

    // Prepare 0x87 verify packet
    uint8_t new_rand8[8] = { 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };
    uint8_t enc_verify[8];
    UsbdSecXSM3AuthenticationCrypt(controller_rand, new_rand8, 8, enc_verify, 1);
    memcpy(current_console_rand + 8, new_rand8, 8);

    uint8_t mac_verify[8];
    UsbdSecXSM3AuthenticationMac(challenge_init_hash, current_console_rand, enc_verify, 8, mac_verify);

    uint8_t pkt_0x87[22];
    memset(pkt_0x87, 0, sizeof(pkt_0x87));
    pkt_0x87[0] = 0x49;
    pkt_0x87[1] = 0x4C;
    pkt_0x87[4] = 0x10;
    memcpy(pkt_0x87 + 5, enc_verify, 8);
    memcpy(pkt_0x87 + 5 + 8, mac_verify, 8);
    pkt_0x87[21] = calculate_checksum(pkt_0x87);

    // Device processes valid 0x87
    bool verify_ok = xsm3_do_challenge_verify(pkt_0x87);
    assert(verify_ok);

    // === Part B: Corrupted Packet Rejection Tests ===
    // 1. Rejection of corrupted 0x82
    xsm3_initialise_state();
    xsm3_set_identification_data(xsm3_id_data_ms_controller);
    uint8_t bad_pkt_0x82[34];
    memcpy(bad_pkt_0x82, pkt_0x82, 34);
    bad_pkt_0x82[10] ^= 0x55; // corrupt encrypted ciphertext
    bad_pkt_0x82[33] = calculate_checksum(bad_pkt_0x82);
    assert(xsm3_do_challenge_init(bad_pkt_0x82) == false);

    // 2. Rejection of bad checksum on 0x82
    memcpy(bad_pkt_0x82, pkt_0x82, 34);
    bad_pkt_0x82[33] ^= 0x01; // corrupt checksum
    assert(xsm3_do_challenge_init(bad_pkt_0x82) == false);

    // 3. Rejection of corrupted 0x87
    assert(xsm3_do_challenge_init(pkt_0x82) == true);
    uint8_t bad_pkt_0x87[22];
    memcpy(bad_pkt_0x87, pkt_0x87, 22);
    bad_pkt_0x87[6] ^= 0x01; // corrupt encrypted verify data
    bad_pkt_0x87[21] = calculate_checksum(bad_pkt_0x87);
    assert(xsm3_do_challenge_verify(bad_pkt_0x87) == false);

    printf("  -> XSM3 handshake simulation succeeded and verified!\n");
}

int main(void) {
    printf("=== Starting Toyless Crypto & Protocol Tests ===\n");
    test_sha1();
    test_des3();
    test_id_checksum();
    test_xsm3_handshake_simulation();
    printf("=== All Tests PASSED Successfully! ===\n");
    return 0;
}
