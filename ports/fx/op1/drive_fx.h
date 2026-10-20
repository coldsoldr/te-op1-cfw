// DRIVE as an engine of the dirt category (op1/drive_fx.cc, the DSP in
// ../src/drive/drive.h): an oversampled soft-clip overdrive. One page:
// blue drive, green tone (centre flat), white bias, orange mix; no clicks
// of its own.

#ifndef OP1FX_OP1_DRIVE_FX_H_
#define OP1FX_OP1_DRIVE_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the dirt host (punch 0, drive 1, tape 2, fold 3, crush 4).
const int32_t kDriveEngineId = 1;

extern const Engine kDriveEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_DRIVE_FX_H_
