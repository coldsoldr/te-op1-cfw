#!/usr/bin/env python3
"""Sanity-check a rendered plugin video: spaced frames as PNG, audio level, and a static-screen warning.

    python check_render.py videos/rings.mp4                 -> videos/rings_frames/f0.png .. f4.png + a report
    python check_render.py videos/*.mp4 --frames 3 --out-dir /tmp/check

A render can exit 0 and still be wrong: black (the screen drew nothing), static (variant elements never switched
on), or silent (an effect fed nothing). Read the PNGs with the image reader and compare with the plugin's device
screenshots or SVG. Needs ffmpeg and ffprobe on PATH; nothing else.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def probe(video: Path) -> tuple[float, str, bool]:
    r = subprocess.run(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", str(video)],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"ffprobe failed on {video}: {r.stderr.strip()}")
    d = json.loads(r.stdout)
    dur = float(d["format"].get("duration") or 0)
    v = next((s for s in d["streams"] if s["codec_type"] == "video"), {})
    size = f"{v.get('width')}x{v.get('height')}" if v else "?"
    has_audio = any(s["codec_type"] == "audio" for s in d["streams"])
    return dur, size, has_audio


def frames(video: Path, dur: float, n: int, out: Path) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for i in range(n):
        t = dur * (i + 0.5) / n
        p = out / f"f{i}.png"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{t:.3f}", "-i", str(video), "-frames:v", "1", str(p)],
                       check=True)
        paths.append(p)
    return paths


def audio_level(video: Path) -> tuple[float | None, float | None]:
    r = subprocess.run(["ffmpeg", "-v", "info", "-i", str(video), "-af", "volumedetect", "-f", "null", "-"],
                       capture_output=True, text=True)
    mean = re.search(r"mean_volume:\s*(-?[\d.]+) dB", r.stderr)
    peak = re.search(r"max_volume:\s*(-?[\d.]+) dB", r.stderr)
    return (float(mean.group(1)) if mean else None, float(peak.group(1)) if peak else None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("videos", nargs="+", type=Path)
    ap.add_argument("--frames", type=int, default=5)
    ap.add_argument("--out-dir", type=Path, help="where the PNGs go (default: next to each video, <name>_frames/)")
    args = ap.parse_args()
    bad = 0
    for video in args.videos:
        dur, size, has_audio = probe(video)
        out = (args.out_dir / video.stem) if args.out_dir else video.with_name(video.stem + "_frames")
        pngs = frames(video, dur, args.frames, out)
        digests = {hashlib.md5(p.read_bytes()).hexdigest() for p in pngs}
        notes = []
        if len(digests) == 1 and len(pngs) > 1:
            notes.append("STATIC: every sampled frame is identical")
        if has_audio:
            mean, peak = audio_level(video)
            lvl = f"audio mean {mean} dB, peak {peak} dB"
            if mean is not None and mean < -60:
                notes.append("SILENT: mean level below -60 dB")
        else:
            lvl = "no audio stream"
        flag = "  <-- " + "; ".join(notes) if notes else ""
        bad += bool(notes)
        print(f"{video.name}: {dur:.1f}s {size}, {lvl}, frames in {out.resolve()}{flag}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
