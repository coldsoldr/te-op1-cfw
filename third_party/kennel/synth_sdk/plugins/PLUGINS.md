# Writing kennel plugins

Read [the plugin guide](../../../PLUGIN_GUIDE.md) for standalone `.knl` builds, synth and FX templates, manifest fields, exported services, persistence and compatibility.

Use the headers and `kennel/kn_exports.c` supplied with the target firmware. New plugin IDs use name-based identity and `order: 0`; legacy order numbers are reserved. No original firmware or full firmware rebuild is needed for an ordinary plugin.

For native screen constraints, see [screen art](../docs/screen-art.md). Preserve source copyright lines and component licenses when adapting code; give players the `.knl`, control descriptions, source and license notices.
