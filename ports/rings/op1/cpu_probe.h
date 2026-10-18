// CPU probe (op1/cpu_probe.cc, -DOP1_CPU_PROBE, make op1-cpuprobe): where
// the device's time goes while Rings and Clouds play. The white click on a
// Rings page cycles the drawing through three readout pages (seqprobe.svg).

#ifndef OP1RINGS_OP1_CPU_PROBE_H_
#define OP1RINGS_OP1_CPU_PROBE_H_

#include <cstdint>

namespace op1cpu {

// From RingsRender: one call's cycles.
void Rendered(uint32_t start, uint32_t end);
// From the Rings path of Render: the voice's RenderMono and upsampler cycles.
void RingsVoiceRendered(uint32_t voice_cycles, uint32_t upsampler_cycles);

// The screen (op1/screen.cc): Init once, Frame every Update; Document is
// the readout's document while it's on (else null); Draw shows the page.
void Init();
void Frame();
void* Document();
void Draw(void* screen);

// The white click on a Rings page: next page (off after the last).
void Toggle();

}  // namespace op1cpu

#endif  // OP1RINGS_OP1_CPU_PROBE_H_
