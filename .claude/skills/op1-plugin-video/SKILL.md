---
name: op1-plugin-video
description: Render videos, GIFs and stills of OP-1 kennel (hundekraft .knl) plugin screens with their real audio, using the user's OP-1 plugin videomaker (op1video, repo OP-1_Plugin_videomaker). Use this whenever the user wants to see, demo, preview, record or share what a kennel plugin's screen does (mp4, gif, webm, card video, screen capture, a still frame, "a video of my plugin"), wants a plugin that targets the Blackfin to build and run on a PC (OP1_HOST host build, simulator, "it crashes in the sim"), or mentions op1video, the videomaker, demo scenes, or hundekraft plugin videos. Also use it when the plugin is written in C++ or has assembly kernels, since the skill covers getting such plugins through the host build.
---

# OP-1 plugin videos with op1video

op1video compiles a kennel plugin's own C/C++ sources together with a small host simulator, plays a scripted
scene through it (notes, knob turns, pushes, shift), records what the plugin's screen code draws on every 60 Hz
tick, rasterises the plugin's SVG with those changes, and muxes the frames with the audio the plugin produced.
Nothing is redrawn by hand, so what you get is the device's own behaviour, minus the firmware (no tape, no master
FX, a simple ADSR instead of the OP-1's envelope page, no firmware font).

The hard part is never the rendering: it is getting a plugin that was written for the device to compile and run on
a PC. This skill is the fast path through that.

## Workflow

1. **Find the tools.** The videomaker is a separate repository, usually a sibling of the plugin repo
   (`OP-1_Plugin_videomaker` or `op1-plugin-video`), or in `$OP1VIDEO_DIR`. The hundekraft source bundle
   (`hundekraft-3.0.3-source.tar.gz`) is in `$OP1VIDEO_SDK`, under the repo's `incoming/`, or the tool downloads
   it. `scripts/render_plugins.py` in this skill does both lookups; read its docstring.
2. **Check the videomaker has the C++/Windows fixes.** Run `grep -c CXX_FLAGS op1video/build.py` in its folder.
   Zero means it only builds `.c` sources and, on Windows, hangs or fails silently. Apply
   `assets/videomaker-cpp-windows.patch` (`git apply --check` first; it was cut against commit 69a5823 of that
   repo). What the patch does is listed in `references/troubleshooting.md` so you can redo it by hand on a
   diverged tree.
3. **Check the environment** with `python op1video.py doctor` from the videomaker folder. It needs a C and C++
   compiler, ffmpeg, and an SVG rasteriser (librsvg, `pip install cairosvg`, or `resvg`). On Windows use MSYS2's
   gcc/g++ natively; WSL usually lacks ffmpeg and the Python packages. Never run the tool with `python -I`: its
   worker processes must be able to import the package.
4. **List, then render one plugin small and short** before anything else:
   ```
   python op1video.py --sdk <bundle> --plugin-dir <folder of plugin folders> list
   python op1video.py --sdk <bundle> --plugin-dir <...> render <id> --layout screen --scale 2 --duration 4 -o out.mp4 -v
   ```
   `--plugin-dir` takes a plugin folder or a folder of plugin folders (each with a `plugin.json`). The first
   render compiles the plugin; `-v` shows the compiler commands and is worth keeping on until it builds.
5. **When the build or the simulator fails**, triage with `references/troubleshooting.md` (error text to cause to
   fix) and fix the plugin with `references/host-shims.md` (the catalogue of `OP1_HOST` fallbacks for every kind of
   device-only code: raw firmware addresses, SDRAM, L1 scratch, CYCLES, block counters, asm kernels, libc
   clashes). The rule for every shim: the device path stays textually unchanged, the host path sits in
   `#ifdef OP1_HOST`, and after the change the device build is re-run and its self-check passes.
6. **Look at the result, do not trust the exit code.** `scripts/check_render.py out.mp4` extracts spaced frames
   and reports the audio level; read the frames with the image reader. A render that finishes can still be black
   (a screen that draws nothing), static (variants authored `display="none"` that never switched on), or silent
   (an effect fed nothing). Compare against the plugin's device screenshots or its SVG.
7. **Render the set** with `scripts/render_plugins.py` (every plugin the videomaker sees, or the ids given, into
   one folder; extra arguments go to `render`). Card layout 1920x1080 is the default and takes 10 to 15 s per
   plugin; `--layout screen` for just the OLED, `--format gif` for READMEs, `--layout vertical` for shorts.
8. **Write a scene per plugin** when the automatic demo (chords or an arpeggio, each knob swept in turn) is not a
   good demo. `python op1video.py scene <id> -o <plugin>/demo.yaml` gives a starter; the format is in the
   videomaker's `docs/SCENES.md` (notes, sequences, chords, knob glides, presses for page 2 or latches, shift,
   `source: {type: drums}` or a wav file for effects, captions, presets). Keep moves slow enough to see, give
   reverbs and delays space between plucks, and feed granular effects before freezing them. Check a moment with
   `still <id> --scene demo.yaml --at T`. `render_plugins.py` picks up `<plugin>/demo.yaml` automatically.

## What to tell the user at the end

Where the files are (full paths), which plugins rendered and which did not and why, what you changed in the
plugin sources (every `OP1_HOST` shim, and that the device build still packs), what you changed in the
videomaker, and that nothing is committed unless they asked. Mention the fidelity caveats once: simulator
envelope and sources, no master FX, no firmware font, stroke weight calibrated by `--stroke-scale 0.7`.

## Facts worth knowing before you start

- The SDK compiles plugins for the host when `OP1_HOST` is defined; `op1_plugin.h` then includes
  `harness/op1_host.h`, which the videomaker supplies in `sim/include/harness/`. Every firmware call the plugin
  makes through the SDK (`op1_find_node`, `op1_node_set_visible`, `op1_screen_draw`, knobs, sine table, pitch
  increments) is already covered. Only code that bypasses the SDK needs shims.
- Host stand-ins you can use from plugin code under `OP1_HOST`: `host_shift` (shift held), `host_cycles` (the
  CYCLES counter, advanced per GUI tick, not per audio block), `host_beat()` (beat clock; `len` = 2646000 x 16384
  / bpm), `op1_voice_index(self)`, `OP1_PITCH_INC`, `OP1_VOICE_ENV`, `OP1_SINE_TABLE`, `op1_current_slot()`,
  `op1_fx_scratch(pos)`. The full list is `sim/include/harness/op1_host.h` in the videomaker.
- The SDK defines `OP1_FX_MASTER` (and other device-only constants) only outside `OP1_HOST`; define fallbacks.
- The build is cached by content hash of the plugin's sources, headers, include dirs and the simulator, in the
  videomaker's `.cache/build/<id>-<hash>/`. Editing a shared header outside the plugin folder is picked up.
  `build <id> --force` rebuilds anyway.
- Plugin `plugin.json` keys that matter: `sources` (relative, may leave the folder), `include_dirs`, `cflags`,
  `cxxflags`, `screen: {svg, symbol}`, `defaults`, `page2_defaults`, `kind` (synth, fx, input).
- SVG variant sets that the screen switches on with `SetVisible(1)` must be recorded as `"v":1` by the simulator
  and have their authored `display="none"` dropped by the frame builder; the patch does this. Without it, every
  hidden-by-default element stays hidden forever and the screen looks static.
- Windows: Anaconda's `Library/bin` and `Library/mingw-w64/bin` ahead of MSYS2 on PATH make gcc's `cc1` exit with
  status 1 and no message. The patched build puts the compiler's folder first; outside the tool, run the compiler
  from `msys2_shell.cmd -mingw64` or reorder PATH. Multiprocessing uses `spawn`, so any launcher must guard
  `main()` with `if __name__ == "__main__"`, or every worker re-runs the render and the whole thing hangs.
- Fonts: the OP-1 firmware font is not in the bundle. Screens that draw text through the firmware get a
  substitute; screens that draw glyphs as SVG paths are exact.
