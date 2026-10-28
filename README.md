# te-op1-cfw

Custom firmware work and community plugins for the **original Teenage Engineering OP-1**.

This branch is only a map. The content lives on the branches below.

| Branch | What is there | Start here |
|---|---|---|
| [`hk-plugins`](../../tree/hk-plugins) | **Ready-to-install plugins for the [hundekraft](https://hundekraft.github.io/op1-workshop/) firmware.** Ten `.knl` files (Rings, Marbles, Grids, bitcrusher, drive, tape drive, wavefolder, freq shift, ensemble, tape echo), with a user guide and a screenshot of every plugin's screen. Nothing to build. | [the guide](../../blob/hk-plugins/README.md) |
| [`dev`](../../tree/dev) | **The source.** The Mutable Instruments ports (Rings, Clouds, Marbles, Grids, Braids, Plaits, Streams), the effects, the kennel plugin glue and packer, the firmware analysis notes, the device test log and the builder app. Everything on `hk-plugins` is built from here. | [`docs/HANDOVER.md`](../../blob/dev/docs/HANDOVER.md), [`docs/kennel-plugins.md`](../../blob/dev/docs/kennel-plugins.md) |
| `opus` | Optimisations for the OPUS custom firmware. Not started yet; it will branch from `dev`. | |

## Which one do I want?

- **I have an OP-1 running hundekraft and want the plugins.** Go to [`hk-plugins`](../../tree/hk-plugins). The guide explains installing through the workshop and how each plugin is played.
- **I want to read or build the code, or port something new.** Go to [`dev`](../../tree/dev). Builds run with the Blackfin GCC toolchain under Linux or WSL; `docs/HANDOVER.md` is the entry point and `ports/knl/Makefile` builds the plugins.

## Licences

The Mutable Instruments code is Émilie Gillet's (Rings, Marbles, Warps and Beads under MIT; Grids under GPL v3). The hundekraft kennel SDK is GPL-3.0-or-later with the OP-1 firmware permission, and the plugins built on it follow it. No Teenage Engineering firmware or keys are in this repository.

Not affiliated with Teenage Engineering. Use at your own risk and keep a backup of your tape.
