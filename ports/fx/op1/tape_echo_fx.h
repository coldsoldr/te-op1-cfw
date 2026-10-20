// TAPE ECHO as an engine of the delay category (op1/tape_echo_fx.cc, the
// core in ../src/tape_echo/tape_echo.h): a multi-head tape delay at half
// the effect rate. One page: blue time, green feedback, white age, orange
// mix; blue click: the next head pattern (saved: values[4] bits 0-3; shift:
// back); green click: hold (not saved).

#ifndef OP1FX_OP1_TAPE_ECHO_FX_H_
#define OP1FX_OP1_TAPE_ECHO_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the delay host (delay 0, tape echo 1).
const int32_t kTapeEchoEngineId = 1;

extern const Engine kTapeEchoEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_TAPE_ECHO_FX_H_
