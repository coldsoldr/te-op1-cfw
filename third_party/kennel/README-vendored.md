# Vendored kennel plugin SDK (hundekraft 3.0.3)

A subset of the hundekraft 3.0.3 source bundle, copied unchanged on 2026-10-26 from
`incoming/hundekraft/op1-workshop-main/source/hundekraft-3.0.3-source.tar.gz`
(SHA-256 `2f745e3858f9fabf8e653ecfe3511679b47f795ff09f13e9bd5590e26995b1d7`), paths
relative to `ext/kennel/` in that archive. Licence: GPL-3.0-or-later with the OP-1
firmware permission (`LICENSE`, `NOTICE`). Our own plugins live outside this tree,
in `ports/knl/`, and are packed by `tools/knl_pack.py`, which imports `tools/kennel/pack.py`
and `reloc.py` from here without modifying them.

What is here and why:

| Path | Use |
|---|---|
| `synth_sdk/*.h` | the plugin ABI (`op1_plugin.h`), feature services, sequencer/LFO/tape/drum/sampler/input headers |
| `synth_sdk/kennel/kn_exports.c` | the exact symbols a plugin may import from the 3.0.3 core; `knl_pack.py` checks imports against it |
| `synth_sdk/template/` | hundekraft's synth and FX starting points |
| `synth_sdk/harness/` | `svg_check.py` (the packer's turn-back rule) and the Clouds host checks |
| `synth_sdk/plugins/clouds`, `plugins/plate` | reference plugins for the two effects we also have, same ids |
| `synth_sdk/plugins/ORDERS.md`, `PLUGINS.md` | legacy order numbers (new plugins use order 0) |
| `tools/kennel/` | `pack.py` (packer), `reloc.py` (reference loader), `merge.ld`, format tests |
| `tools/op1push.py` | the USB MIDI SysEx file push (host side of devupd) |
| `docs/` | plugin format, feature hooks, screen art, preset identity, inventory, tapes |
| `PLUGIN_GUIDE.md` | the plugin author's guide |

The full bundle (core sources, every plugin, firmware build tools) stays in `incoming/`;
nothing here rebuilds the firmware. Keep this tree in step with the firmware on the device:
a `.knl` built against another core's `OP1_PLUGIN_API` or export table is skipped at boot.
