#ifndef USBDSEC_H_
#define USBDSEC_H_

#include <stddef.h>
#include <stdint.h>

void UsbdSecXSM3AuthenticationCrypt(const uint8_t *key, const uint8_t *input, size_t length, uint8_t *output, uint8_t encrypt);
void UsbdSecXSM3AuthenticationMac(const uint8_t *key, uint8_t *salt, uint8_t *input, size_t length, uint8_t *output);
void UsbdSecXSMAuthenticationAcr(const uint8_t *console_id, const uint8_t *input, const uint8_t *key, uint8_t *output);

#endif // USBDSEC_H_
