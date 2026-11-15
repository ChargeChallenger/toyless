#ifndef XSM3_H_
#define XSM3_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Identification data taken from an official wired controller. (Serial number is static.)
extern const uint8_t xsm3_id_data_ms_controller[0x1D];

// The response data from the previously completed challenge.
extern uint8_t xsm3_challenge_response[0x30];

// The console ID fetched from the console after request 0x82.
extern uint8_t xsm3_console_id[0x8];

// Clears the state of the XSM3 internal variables.
void xsm3_initialise_state(void);

// Sets the identification data to use.
void xsm3_set_identification_data(const uint8_t id_data[0x1D]);

// Imports the console-specific keys (GetKey index 0x23 and 0x24 for wired controllers) into the current state.
void xsm3_import_kv_keys(const uint8_t key1[0x10], const uint8_t key2[0x10]);

// Initialises the XSM3 state using information from the challenge init packet (0x82) and places a response in xsm3_challenge_response.
// Returns true on successful validation of checksum and incoming MAC, false otherwise.
bool xsm3_do_challenge_init(uint8_t challenge_packet[0x22]);

// Completes a verify challenge passed from request 0x87 and places the response data in xsm3_challenge_response.
// Returns true on successful validation of checksum and incoming MAC, false otherwise.
bool xsm3_do_challenge_verify(uint8_t challenge_packet[0x16]);

#endif // XSM3_H_
