#!/usr/bin/env python3
"""
Render frames written by `PPS --headless --dump-every K --dump-dir DIR` into a GIF (or MP4).

    python tools/render_gif.py frames --size 150x150 --out docs/pps.gif
"""

import argparse
import glob
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

COLORS = {"green": (0.24, 0.78, 0.24), "brown": (0.59, 0.35, 0.16), "blue": (0.24, 0.43, 1.0),
          "yellow": (0.98, 0.86, 0.16), "magenta": (0.9, 0.24, 0.9)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("--size", default="100x100")
    ap.add_argument("--out", default="pps.gif")
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--px", type=int, default=420, help="output width in pixels")
    ap.add_argument("--dot", type=float, default=2.0)
    a = ap.parse_args()
    W, H = (float(v) for v in a.size.split("x"))
    files = sorted(glob.glob(os.path.join(a.frames, "frame_*.csv")))
    if not files:
        raise SystemExit(f"no frame_*.csv in {a.frames}")
    import imageio.v2 as imageio

    images = []
    for f in files:
        d = np.genfromtxt(f, delimiter=",", names=True, dtype=None, encoding="utf-8")
        step = int(os.path.basename(f)[6:12])
        fig = plt.figure(figsize=(a.px / 100, a.px / 100 * H / W), dpi=100)
        ax = fig.add_axes([0, 0, 1, 1])
        ax.set_facecolor((0.04, 0.04, 0.08))
        ax.scatter(d["x"], d["y"], s=a.dot, c=[COLORS[c] for c in d["color"]], linewidths=0)
        ax.set_xlim(0, W)
        ax.set_ylim(0, H)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.text(0.01 * W, 0.97 * H, f"step {step}", color="white", fontsize=8, va="top")
        fig.canvas.draw()
        images.append(np.asarray(fig.canvas.buffer_rgba())[:, :, :3].copy())
        plt.close(fig)
    if a.out.endswith(".mp4"):
        imageio.mimsave(a.out, images, fps=a.fps)
    else:
        imageio.mimsave(a.out, images, fps=a.fps, loop=0)
    print(f"wrote {a.out} ({len(images)} frames)")


if __name__ == "__main__":
    main()
