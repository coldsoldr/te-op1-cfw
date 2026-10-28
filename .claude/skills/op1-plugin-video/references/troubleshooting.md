# Troubleshooting: what the error says, what it means, what to do

Start every investigation with `render <id> -v --keep`: `-v` prints the compiler commands and timings, `--keep`
keeps the working folder (the scene script, per-frame JSON records, raw audio) and prints its path. The
videomaker's own `docs/TROUBLESHOOTING.md` covers the stock plugins; this file covers what you meet with your
own plugins and on Windows.

## Build

| What you see | Cause | Fix |
|---|---|---|
| `compiling X failed: (no compiler output, exit 1)` on Windows | gcc's `cc1`/`collect2` picked up another distribution's DLLs (Anaconda's zlib, libwinpthread, libgcc) because that folder is earlier on PATH than MSYS2's `mingw64/bin`. Same gcc works from `msys2_shell.cmd -mingw64`. | Apply the patch (`_env()` in `build.py` prepends the compiler's folder to PATH). Outside the tool, run from the MSYS2 shell or put `C:\msys64\mingw64\bin` first. |
| `no C compiler found` | nothing on PATH | install MSYS2 `mingw-w64-x86_64-gcc` (and `g++` from the same package), or set `CC`/`CXX`. |
| `skipping non-C sources` and then undefined references, or `.cc` files never compiled | unpatched videomaker builds only `.c` | apply the patch (C++ units through g++, `.S` skipped). |
| `'OP1_FX_MASTER' was not declared in this scope` | the SDK defines it only outside `OP1_HOST` | `#ifndef OP1_FX_MASTER / #define OP1_FX_MASTER 2` next to the SDK include. |
| `junk at end of line, first unrecognized character is '%'` from the assembler | Blackfin inline asm (`%0 = CYCLES;`) reached the host compiler | host-shims.md section 4. |
| `multiple definition of 'memset'` (or memcpy/memmove) | the plugin ships its own libc replacements | wrap them in `#ifndef OP1_HOST` (section 10). |
| `undefined reference to host_...` | the plugin uses a harness symbol the videomaker's `sim/host.c` does not provide yet | add it to `sim/host.c` and `op1_host.h`, or use one that exists (`op1_host.h` is the list). |
| `expected code not found (the source bundle changed?)` | a `compat.py` exact-match patch no longer matches | update or drop the patch. |
| `error: 'host_cycles' was not declared` | you referenced it inside a namespace | declare `extern "C" uint32_t host_cycles;` at file scope, use `::host_cycles`. |
| Build not picking up a header change | the header is outside the plugin folder and the videomaker is unpatched | the patch hashes `sources` outside the folder and `include_dirs` trees; or `build <id> --force`. |

## Simulator

| What you see | Cause | Fix |
|---|---|---|
| `the simulator failed for <id>`, exit 3221225477 (`0xC0000005`) or a segfault | a raw firmware address was dereferenced: shift byte, tempo word, block counter, SDRAM state, L1 scratch, firmware function pointers, screen calls by address | host-shims.md sections 1, 2, 3, 7, 11. Find the spot by `-v --keep`, then run the printed `op1sim` command from the kept folder under gdb, or shorten the scene. |
| Simulator ran but the effect restarts constantly / audio stutters | a gap check measured with `host_cycles`, which only advances per GUI tick | section 4, use the nominal interval on the host. |
| Knobs appear to do nothing | knob pointer rejected by an address-range check | section 8. |
| Only one voice sounds / wrong voice | voice index derived from the object address | section 5. |

## Rendering

| What you see | Cause | Fix |
|---|---|---|
| Render runs for minutes with many `python` and an `ffmpeg` process, never finishes (Windows) | the launcher called `main()` unguarded; `spawn` workers re-import it and each starts a new render | apply the patch (`if __name__ == "__main__": main()`); `--jobs 1` as a stopgap; kill the stale processes. |
| Workers die with import errors | the tool was run with `python -I` | never use `-I` with this tool. |
| Screen static: the base artwork shows but no variant, staircase, glyph or mode set ever appears | elements authored `display="none"` that the screen switches on; the unpatched simulator only records hides | apply the patch (`"v":1` recording in `sim/host.c`, `display` dropped in `svgframe.py`). Check: `grep -c '"v":1'` in the kept frame records. |
| `note: screen looked up an id that is not in the SVG` | harmless on its own (device returns NULL too) | if the screen then looks wrong, the SVG in `plugin.json` is the wrong file or ids were renamed. |
| Black screen | synth screens need notes; effect screens need sound passing through; some need a buffer full before a freeze | give the scene `notes`, raise `source.level`, delay presses. |
| Silent audio (check_render reports mean below about −60 dB) | effect fed nothing, or the plugin's output path is a no-op on the host (e.g. a stock-engine call shimmed to silence) | check the scene's `source`; for synths, make sure the host path renders sound, not just the device path. |
| Text differs from the device | firmware font not in the bundle | expected; screens that draw glyphs as SVG paths are exact. |
| `no SVG rasterizer available` | none of librsvg/cairosvg/resvg | `pip install cairosvg` (an Anaconda Python already has cairo); or put `resvg` on PATH. |
| `fps must divide 60` | the OLED updates at 60 Hz | use 60, 30, 20, 15, 12. |

## What the videomaker patch contains (to redo by hand if `git apply` fails)

`assets/videomaker-cpp-windows.patch`, cut against commit 69a5823 of `OP-1_Plugin_videomaker`:

1. `op1video.py`: `main()` guarded by `if __name__ == "__main__":`.
2. `op1video/build.py`: `COMMON_FLAGS` / `C_FLAGS=-std=gnu11` / `CXX_FLAGS=-std=gnu++17 -fno-exceptions -fno-rtti
   -fno-threadsafe-statics -fno-use-cxa-atexit`; `cxx_compiler(cc)` ($CXX, else g++ next to cc); `.cc/.cpp/.cxx`
   wrapped in `unit{i}.cc` with `extern "C" { #include "op1_plugin.h" }` and an `extern "C"` beat accessor; `.S`
   skipped; link with g++ when any C++ unit exists, `-static` on Windows; exe named `op1sim.exe` on Windows;
   `_env(cc)` prepends the compiler's folder to PATH on Windows; fingerprint hashes sources and headers outside
   the plugin folder; `_run` reports `(no compiler output, exit N)`.
3. `op1video/sdk.py`: `Plugin.cxxflags` read from `plugin.json`'s `"cxxflags"`.
4. `sim/host.c`: `host_frame_dump` records `"v":<0|1>` whenever a node's visibility was touched.
5. `op1video/svgframe.py`: on `"v":1`, delete an authored `display="none"`.
6. Docs: `docs/ADDING-PLUGINS.md` (C++ support, device-only code), `docs/INSTALL.md` (native Windows).

The videomaker's unit tests (`python -m unittest tests/test_scene.py tests/test_render.py`, with `OP1VIDEO_SDK`
set) pass with the patch; run them after applying it to a different revision.
