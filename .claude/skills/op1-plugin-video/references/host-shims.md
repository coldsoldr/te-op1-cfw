# OP1_HOST shims: making device-only plugin code run in the simulator

The kennel SDK builds a plugin for the PC when `OP1_HOST` is defined. SDK calls are covered by the videomaker's
`harness/op1_host.h`. Everything a plugin does *around* the SDK (because the device lets it) is what breaks the
host build. Each pattern below was met in a real port and is listed as symptom, cause, shim. Keep every shim to
the same shape:

```cpp
#ifdef OP1_HOST
  // the host stand-in
#else
  // the device code, textually unchanged
#endif
```

Why that shape: the plugin ships to the device from the same sources, and a `.knl` that differs from what was
tested is a risk nobody can see in a video. After adding shims, rebuild the device target and let its packer's
self-check run; mention in the handover that it did.

If the plugin is not yours (a bundle plugin), do not edit it. Use the videomaker's `op1video/compat.py`
exact-match patch mechanism or `sim/compat/<plugin>.c` instead.

## 1. Raw firmware addresses (keys, shift, tempo, block counter)

Symptom: the simulator exits with an access violation (Windows exit 3221225477, `0xC0000005`) or a segfault as
soon as the screen or the audio callback runs. Cause: `*reinterpret_cast<const volatile uint8_t*>(0x01dd7f78)`
and friends dereference device memory.

```cpp
#ifdef OP1_HOST
inline bool ShiftHeld() { return host_shift != 0; }
#else
inline bool ShiftHeld() { return *reinterpret_cast<const volatile uint8_t*>(kShiftHeld) != 0; }
#endif
```

Tempo from the firmware's increment word: derive it from the harness beat clock instead, since the scene sets a
bpm and tempo-synced effects should follow it.

```cpp
#ifdef OP1_HOST
inline uint32_t TempoIncrement() {
  const op1_beat_t* b = host_beat();               // len = 2646000 * 16384 / bpm
  if (b->len <= 0) return kTempo120;
  const double bpm = 2646000.0 * 16384.0 / static_cast<double>(b->len);
  const double inc = bpm * 1662149.125;            // the firmware's units: 120 BPM = 199457895
  return (inc < 33242983.0 || inc > 664859650.0) ? kTempo120 : static_cast<uint32_t>(inc);
}
#endif
```

A block counter read from firmware memory: count the plugin's own process calls on the host.

```cpp
#ifdef OP1_HOST
uint32_t host_block_counter;                        // ++ at the top of Process()
inline uint32_t BlockCounter() { return host_block_counter; }
#else
inline uint32_t BlockCounter() { return Read<uint32_t>(kBlockCounter); }
#endif
```

## 2. Fixed SDRAM addresses for big state

Symptom: access violation at init, or when the first note plays. Cause: state objects placed at absolute
addresses (`reinterpret_cast<Core*>(0x02000000 + slot * size)`).

Give the memory map one macro that yields a pointer for a device address and route every placement through it:

```cpp
#ifdef OP1_HOST
inline uint8_t* HostPtr(uint32_t address) {
  static uint8_t arena[kUsedEnd - kBase];          // zero-initialised, as the device's SDRAM is cleared
  return arena + (address - kBase);
}
#define SDRAM_PTR(address) (::sdram::HostPtr(address))
#else
#define SDRAM_PTR(address) (reinterpret_cast<uint8_t*>(address))
#endif
```

Then `core = reinterpret_cast<Core*>(SDRAM_PTR(kClouds + i * kSlot));`, and any `Zero(address, bytes)` helper
writes through the same macro. The arena is as big as the map's used range, so size it from the map's constants,
not by hand. Keep static_asserts about the map: they still hold on the host.

## 3. L1 scratchpad placement

Symptom: crash, or silent garbage, in code that keeps voice state in L1 (`0xffb00000`...). Cause: a build flag
like `OP1RINGS_L1_SCRATCH` selects the L1 address. Make the selector also require not-host, and let the static
fallback that already exists for tests take over:

```cpp
#if defined(OP1RINGS_L1_SCRATCH) && !defined(OP1_HOST)
```

## 4. CYCLES and busy waits

Symptom: the assembler reports `junk at end of line, first unrecognized character is '%'` from a line like
`__asm__("%0 = CYCLES;")`, or a link error. Cause: Blackfin inline assembly. Use the harness counter, which is
declared `extern "C"` and lives outside any namespace:

```cpp
#if defined(OP1_HOST)
}  // leave the namespace
extern "C" uint32_t host_cycles;
namespace op1 {
inline uint32_t Cycles() { return ::host_cycles; }
inline void BusyWait(uint32_t start, uint32_t cycles) { ::host_cycles = start + cycles; }
#elif defined(OP1_HOST_SIM)
...
```

Important: `host_cycles` advances once per 60 Hz GUI tick, not per audio block. Code that measures the gap between
process calls (a "restart if we were starved for 5 ms" check) will trip on every block. On the host, use the
nominal period instead of the measured one:

```cpp
#ifdef OP1_HOST
  const uint32_t interval = kNominalPeriod;
#else
  const uint32_t interval = start - in->last_call;
#endif
```

## 5. Voice index from the voice object's address

Symptom: wrong or crashing voice lookup. Cause: `(self - kVoiceObjectBase) / 28` assumes the firmware's voice
array. The harness has its own: `op1_voice_index(self)`, clamp it to the plugin's voice count.

## 6. Pitch buffer, envelope, sine table

Use the harness macros `OP1_PITCH_INC`, `OP1_VOICE_ENV`, `OP1_SINE_TABLE` under `OP1_HOST` instead of the L1 or
ROM addresses. If the plugin already has an `OP1_SIM` path for its own tests, add `OP1_HOST` alongside it with
`#if defined(OP1_SIM) || defined(OP1_HOST)`.

## 7. Calling stock firmware code by address

Symptom: crash when a feature that calls into the stock engine runs (a "stock phase engine" voice, a firmware
helper at `0x01a0bc5c`). The host has no firmware. Make those calls silent or no-ops under `OP1_HOST`, in the same
branch the plugin's simulator build already uses.

## 8. Knob arrays validated by address range

Symptom: knobs read as zero on the host. Cause: a check like "if the pointer is inside the firmware's knob array,
use it" rejects the harness's array. Under `OP1_HOST`, trust the pointer the harness passes.

## 9. Device-only SDK constants

Symptom: `'OP1_FX_MASTER' was not declared in this scope`. The SDK defines some constants only outside
`OP1_HOST`. Add a guarded fallback next to the include:

```cpp
#ifndef OP1_FX_MASTER
#define OP1_FX_MASTER 2
#endif
```

## 10. The plugin's own memset/memcpy/memmove

Symptom: `multiple definition of memset` at link. Cause: a bare-metal plugin ships its own word-at-a-time libc
replacements. Wrap the whole `extern "C" { ... }` block in `#ifndef OP1_HOST`; the host links a real libc.

## 11. Screen calls by firmware address (a screen helper header)

Symptom: access violation on the first GUI tick. Cause: a header that wraps the firmware's SVG node calls by
address (`reinterpret_cast<SetTransformFn>(kSetTransform)(node, m)`). Give the header a host branch that maps each
helper to the SDK macro of the same meaning, and route *every* caller through the helpers (grep for
`reinterpret_cast<` + `Fn>` to find callers that bypass them):

```cpp
#ifdef OP1_HOST
extern "C" { #include "op1_plugin.h" }
inline void* DocumentOf(void* screen) { return screen; }   // the host looks nodes up through the screen itself
inline void  Draw(void* screen) { op1_screen_draw(screen, 1); }
inline void* Find(void* doc, const char* id) { return op1_find_node(doc, id); }
inline void  SetTransform(void* node, const int32_t* m) { op1_node_set_transform(node, m); }
inline void  SetVisible(void* node, bool on) { op1_node_set_visible(node, on ? 1 : 0); }
inline uint8_t* LineStyle(void* s, const char* id) { return op1_node_line_rgba(s, id); }
inline uint8_t* FillStyle(void* s, const char* id) { return op1_node_fill_rgba(s, id); }
#endif
```

`LoadDocument`/`Flush` style helpers that create firmware objects are device-only; leave them out of the host
branch and make sure the screen's init does not need them there.

## 12. C++ plugins and assembly kernels

The patched videomaker compiles `.cc/.cpp/.cxx` with g++ (`-std=gnu++17 -fno-exceptions -fno-rtti
-fno-threadsafe-statics`, plus `cxxflags` from `plugin.json`) and skips `.S` files. Every assembly kernel needs a C
fallback selected by `#if defined(__bfin__) && !defined(<PLUGIN>_NO_ASM)`; on the host `__bfin__` is undefined so
the C path builds. If a kernel has no fallback, write one (it is the reference implementation anyway) rather than
patching the videomaker.

## 13. SVG elements authored hidden

Not a code shim but the same class of problem: a screen that authors its variants as `display="none"` and calls
`SetVisible(1)` on the one it wants. The device honours that; the simulator starts every node visible and, before
the patch, only recorded nodes being hidden. The patch records `"v":1` and the frame builder deletes the authored
`display` attribute. If a screen still looks static, check that `grep -c '"v":1' <kept frame records>` is nonzero
(`render --keep -v` prints the working folder).

## Checklist after shimming

- Device build re-run (`make <plugin>` in its folder, or the packer) and the self-check passes.
- `render <id> --layout screen --scale 2 --duration 4 -v` builds, the simulator runs, and frames show the screen.
- `scripts/check_render.py` reports non-silent audio for synths and effects alike.
- The handover names each shim in one line, so a later reader knows what the host path replaces.
