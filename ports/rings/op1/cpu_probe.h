// CPU probe (op1/cpu_probe.cc, -DOP1_CPU_PROBE, make op1-cpuprobe): where
// the device's time goes while Rings plays. The white click on a Rings page
// swaps the drawing for a readout of ten numbers (seqprobe.svg).

#ifndef OP1RINGS_OP1_CPU_PROBE_H_
#define OP1RINGS_OP1_CPU_PROBE_H_

#include <cstdint>

namespace op1cpu {

// From RingsRender: one call's cycles; rings: whether it took the Rings path.
void Rendered(uint32_t start, uint32_t end, bool rings);
// From the Rings path of Render (counted per block).
void RingsVoiceRendered();

// The screen (op1/screen.cc): Init once, Frame every Update; Document is
// the readout's document while it's on (else null); Draw shows it.
void Init();
void Frame();
void* Document();
void Draw(void* screen);

// The white click on a Rings page.
void Toggle();

}  // namespace op1cpu

#endif  // OP1RINGS_OP1_CPU_PROBE_H_
