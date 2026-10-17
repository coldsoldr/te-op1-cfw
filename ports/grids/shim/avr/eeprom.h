// Host shim for avr-libc <avr/eeprom.h>: an erased EEPROM (0xff), writes
// ignored. The harness never calls Init()/SaveSettings().

#ifndef SHIM_AVR_EEPROM_H_
#define SHIM_AVR_EEPROM_H_

#include <stdint.h>

inline uint8_t eeprom_read_byte(const uint8_t*) { return 0xff; }
inline void eeprom_write_byte(uint8_t*, uint8_t) { }

#endif  // SHIM_AVR_EEPROM_H_
