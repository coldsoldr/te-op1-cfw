#!/usr/bin/env python3
"""Render videos of kennel plugins with the OP-1 plugin videomaker (op1video).

    python render_plugins.py --plugin-dir ports/knl                 every plugin there -> videos/<id>.mp4
    python render_plugins.py --plugin-dir ports/knl rings crush     some of them
    python render_plugins.py --plugin-dir ports/knl --list          what the videomaker sees
    python render_plugins.py --plugin-dir ports/knl --doctor        the videomaker's environment check
    python render_plugins.py --plugin-dir ports/knl -o out --layout screen --duration 6 --format gif
                                                                    (unknown arguments go to `op1video render`)

Where things come from:
  the videomaker   $OP1VIDEO_DIR, else a folder named OP-1_Plugin_videomaker or op1-plugin-video next to the
                   current directory, its parent, or the plugin dir's repository
  the SDK bundle   $OP1VIDEO_SDK, else hundekraft-*-source.tar.gz under ./incoming (newest version), else the
                   videomaker downloads it
  scenes           <plugin>/demo.yaml if it exists, else --scene-dir/<id>.yaml, else the automatic demo

--plugin-dir may be given more than once (a plugin folder, or a folder of plugin folders, each with plugin.json).
Never run the videomaker with `python -I`: its rasterising workers must import its package.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

VIDEOMAKER_NAMES = ("OP-1_Plugin_videomaker", "op1-plugin-video", "OP-1_Plugin_videomaker-main")


def find_videomaker(plugin_dirs: list[Path]) -> Path:
    """$OP1VIDEO_DIR, else a folder with one of VIDEOMAKER_NAMES beside any ancestor of the current directory or
    of a plugin dir (a worktree several levels below the projects folder still finds its sibling)."""
    env = os.environ.get("OP1VIDEO_DIR")
    if env and (Path(env) / "op1video.py").exists():
        return Path(env)
    seen: set[Path] = set()
    for start in [Path.cwd().resolve(), *(p.resolve() for p in plugin_dirs)]:
        for base in [start, *start.parents]:
            if base in seen:
                continue
            seen.add(base)
            for name in VIDEOMAKER_NAMES:
                r = base / name
                if (r / "op1video.py").exists():
                    return r
    sys.exit("videomaker not found: set OP1VIDEO_DIR to the OP-1_Plugin_videomaker folder")


def sdk_args() -> list[str]:
    env = os.environ.get("OP1VIDEO_SDK")
    if env:
        return ["--sdk", env]
    incoming = Path.cwd() / "incoming"
    if incoming.is_dir():
        hits = sorted(incoming.rglob("hundekraft-*-source.tar.gz"),
                      key=lambda p: [int(x) for x in re.findall(r"\d+", p.name)])
        if hits:
            return ["--sdk", str(hits[-1])]
    return []


def plugin_folders(plugin_dirs: list[Path]) -> list[Path]:
    out = []
    for d in plugin_dirs:
        if (d / "plugin.json").exists():
            out.append(d)
            continue
        for sub in sorted(d.iterdir()):
            if (sub / "plugin.json").exists():
                out.append(sub)
    return out


def has_screen(folder: Path) -> bool:
    try:
        d = json.loads((folder / "plugin.json").read_text())
    except (OSError, ValueError):
        return False
    return bool(d.get("screen"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("plugins", nargs="*", help="plugin ids (default: every plugin with a screen in --plugin-dir)")
    ap.add_argument("--plugin-dir", action="append", default=[], help="plugin folder or folder of plugin folders (repeatable)")
    ap.add_argument("-o", "--out-dir", default="videos")
    ap.add_argument("--scene-dir", help="folder of <id>.yaml scenes (a plugin's own demo.yaml wins)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--doctor", action="store_true")
    ap.add_argument("--ext", default=None, help="output extension (default from --format, else mp4)")
    args, extra = ap.parse_known_args()
    plugin_dirs = [Path(p) for p in args.plugin_dir] or [Path(".")]
    vm = find_videomaker(plugin_dirs)
    base = [sys.executable, str(vm / "op1video.py"), *sdk_args()]
    for d in plugin_dirs:
        base += ["--plugin-dir", str(d.resolve())]
    if args.doctor:
        return subprocess.call([*base, "doctor"], cwd=vm)
    if args.list:
        return subprocess.call([*base, "list"], cwd=vm)

    folders = {f.name: f for f in plugin_folders(plugin_dirs)}
    ids = args.plugins or [n for n, f in folders.items() if has_screen(f)]
    fmt = "mp4"
    if "--format" in extra:
        fmt = extra[extra.index("--format") + 1]
    ext = args.ext or ("" if fmt == "png" else fmt)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    failed = []
    for pid in ids:
        folder = folders.get(pid)
        if folder is None:
            print(f"{pid}: no plugin.json under {', '.join(map(str, plugin_dirs))}")
            failed.append(pid)
            continue
        cmd = [*base, "render", pid, "-o", str(out / (f"{pid}.{ext}" if ext else pid)), *extra]
        scene = folder / "demo.yaml"
        if not scene.exists() and args.scene_dir:
            scene = Path(args.scene_dir) / f"{pid}.yaml"
        if scene.exists() and "--scene" not in extra:
            cmd += ["--scene", str(scene.resolve())]
        print(f"== {pid}" + (f"  (scene {scene.name})" if scene.exists() else ""), flush=True)
        if subprocess.call(cmd, cwd=vm):
            failed.append(pid)
    print(f"rendered {len(ids) - len(failed)} of {len(ids)} into {out.resolve()}")
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
