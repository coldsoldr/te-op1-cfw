// Host shim for grids/hardware_config.h: the LED constants only (upstream's
// pulls in avrlib's GPIO/SPI/serial drivers). Values as upstream.

#ifndef GRIDS_HARDWARE_CONFIG_H_
#define GRIDS_HARDWARE_CONFIG_H_

#include "avrlib/base.h"

namespace grids {

enum LedBits {
  LED_CLOCK = 1,
  LED_BD = 8,
  LED_SD = 4,
  LED_HH = 2,
  LED_ALL = LED_CLOCK | LED_BD | LED_SD | LED_HH
};

}  // namespace grids

#endif  // GRIDS_HARDWARE_CONFIG_H_
