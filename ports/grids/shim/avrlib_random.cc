// avrlib/random.cc for the host reference build.
#include "avrlib/random.h"

namespace avrlib {

uint16_t Random::rng_state_ = 0x21;

}  // namespace avrlib
