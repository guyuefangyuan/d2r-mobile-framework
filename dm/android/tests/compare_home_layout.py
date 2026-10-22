#!/usr/bin/env python3
"""Compare the rendered menu layout of two devices at game-canvas resolution.

The game always draws its UI into a fixed 16:9 canvas and the Android shell scales
that canvas to a 16:9 rectangle inside the screen. Everything outside that rectangle
is the shell background colour. So the meaningful comparison is not "do the two
screenshots look alike" (they have different resolutions and densities) but "after
cropping each screenshot to its own 16:9 rectangle and resampling to the same
1280x720 canvas, does the UI land in the same place".

That is what this tool does, with no manual coordinates:

  1. find the shell background colour (the most common colour along the border),
  2. treat every row/column that is entirely background as letterbox/pillarbox and
     crop it away,
  3. resample both crops to 1280x720,
  4. report the mean absolute difference, the share of clearly different pixels, and
     the horizontal position of the brightest vertical edges (the ornate menu frame),
     which is the part that actually moves when the layout is wrong.

Regression it guards (2026-10-22): a device that runs without the touch-UI overlay
falls back to the stock gamepad UI, which lays the character panel out on the other
side and replaces the tappable "create new character" button with a ✚ key prompt.
See NOTICE.md and docs/ANDROID_PORT.zh-CN.md.

Usage:
    python compare_home_layout.py before.png after.png
    python compare_home_layout.py --json out.json pad.png phone.png

Exit status is 0 when the layouts match within the thresholds, 1 otherwise, so it can
be used as a check. Requires Pillow and numpy.
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image

CANVAS = (1280, 720)
BORDER_TOLERANCE = 2      # per-channel slack when matching the shell background
UNIFORM_SHARE = 0.995     # a row/column is "all background" at this share or above
DEFAULT_MAX_MEAN = 10.0   # mean absolute difference over 0..255
DEFAULT_MAX_DIFF_SHARE = 4.0   # percent of pixels differing by more than 60
DEFAULT_MAX_EDGE_SHIFT = 16    # px, in the 1280-wide canvas


def _runs(uniform, minimum=8):
    """Contiguous [start, end] runs of True values at least `minimum` long."""
    runs = []
    index, total = 0, len(uniform)
    while index < total:
        if uniform[index]:
            end = index
            while end < total and uniform[end]:
                end += 1
            if end - index >= minimum:
                runs.append((index, end - 1))
            index = end
        else:
            index += 1
    return runs


def content_box(image):
    """Return (x0, y0, x1, y1, background) of the 16:9 game picture in a screenshot.

    The shell paints the letterbox/pillarbox with one flat colour, but a device may
    also have a black strip of its own next to it (a cutout or a dead edge column),
    so the picture is not simply "everything that is not the background". Only the
    background bars that sit next to the middle of the screen bound the picture:
    the longest uniform run on each side of the centre that ends towards the centre.
    """
    array = np.asarray(image.convert("RGB")).astype(np.int32)
    height, width, _ = array.shape
    border = []
    for x in (0, width - 1):
        for y in range(0, height, max(1, height // 32)):
            border.append(tuple(array[y, x]))
    for y in (0, height - 1):
        for x in range(0, width, max(1, width // 32)):
            border.append(tuple(array[y, x]))
    counts = {}
    for pixel in border:
        key = tuple(min(255, max(0, int(round(c)))) for c in pixel)
        counts[key] = counts.get(key, 0) + 1
    background = np.array(max(counts.items(), key=lambda kv: kv[1])[0], dtype=np.int32)

    same = np.all(np.abs(array - background) <= BORDER_TOLERANCE, axis=2)

    def bounds(share):
        start, end = 0, len(share) - 1
        middle = len(share) // 2
        for run_start, run_end in _runs(share > UNIFORM_SHARE):
            if run_end < middle:
                start = max(start, run_end + 1)
            elif run_start > middle:
                end = min(end, run_start - 1)
        return start, end

    x0, x1 = bounds(same.mean(axis=0))
    y0, y1 = bounds(same.mean(axis=1))
    return x0, y0, x1, y1, background.tolist()


def normalise(path):
    image = Image.open(path)
    x0, y0, x1, y1, background = content_box(image)
    crop = image.convert("RGB").crop((x0, y0, x1 + 1, y1 + 1)).resize(CANVAS, Image.LANCZOS)
    return np.asarray(crop).astype(np.int32), {
        "path": str(path),
        "source_size": list(image.size),
        "content_box": [x0, y0, x1, y1],
        "content_size": [x1 - x0 + 1, y1 - y0 + 1],
        "border_colour": background,
    }


def column_profile(array):
    return array.mean(axis=2).mean(axis=0)


def best_horizontal_shift(left, right, radius=200, minimum_overlap=400):
    """Integer shift of `right` that best lines its columns up with `left`.

    A displacement is the symptom of a wrong UI layout (a device that runs the stock
    gamepad UI puts the character panel on the other side), so this is compared
    against the picture rather than against a handful of picked columns.
    """
    left_profile, right_profile = column_profile(left), column_profile(right)
    best_shift, best_error = 0, None
    for shift in range(-radius, radius + 1):
        if shift >= 0:
            a, b = left_profile[shift:], right_profile[:len(right_profile) - shift]
        else:
            a, b = left_profile[:len(left_profile) + shift], right_profile[-shift:]
        if len(a) < minimum_overlap:
            continue
        error = float(np.abs(a - b).mean())
        if best_error is None or error < best_error:
            best_shift, best_error = shift, error
    return best_shift, (best_error or 0.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("left", help="screenshot from device A")
    parser.add_argument("right", help="screenshot from device B")
    parser.add_argument("--json", help="write the full result to this file")
    parser.add_argument("--max-mean", type=float, default=DEFAULT_MAX_MEAN,
                        help="maximum mean absolute difference over the whole canvas")
    parser.add_argument("--max-diff-share", type=float, default=DEFAULT_MAX_DIFF_SHARE,
                        help="maximum percent of pixels differing by more than 60")
    args = parser.parse_args()

    left, left_meta = normalise(args.left)
    right, right_meta = normalise(args.right)
    difference = np.abs(left - right).mean(axis=2)
    diff_share = float((difference > 60).mean() * 100)
    mean = float(difference.mean())
    shift, shift_error = best_horizontal_shift(left, right)
    # 只为人工定位用：三分区的平均差，能看出差异集中在画面哪一侧
    thirds = [round(float(difference[:, i * CANVAS[0] // 3:(i + 1) * CANVAS[0] // 3].mean()), 2)
              for i in range(3)]

    problems = []
    if mean > args.max_mean:
        problems.append(f"mean difference {mean:.2f} > {args.max_mean}")
    if diff_share > args.max_diff_share:
        problems.append(f"{diff_share:.2f}% of pixels differ strongly > {args.max_diff_share}%")

    result = {
        "left": left_meta, "right": right_meta,
        "canvas": list(CANVAS),
        "mean_absolute_difference": round(mean, 3),
        "strong_difference_percent": round(diff_share, 3),
        "mean_difference_by_third": {"left": thirds[0], "centre": thirds[1], "right": thirds[2]},
        "best_horizontal_shift_px": shift,
        "residual_after_shift": round(shift_error, 3),
        "thresholds": {"max_mean": args.max_mean, "max_diff_share": args.max_diff_share},
        "problems": problems,
        "passed": not problems,
    }
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=1)
            handle.write("\n")

    for side in ("left", "right"):
        meta = result[side]
        print(f"{side:5s} {meta['path']}  {meta['source_size'][0]}x{meta['source_size'][1]}"
              f"  content {meta['content_size'][0]}x{meta['content_size'][1]}"
              f" at {meta['content_box'][0]},{meta['content_box'][1]}"
              f"  border {meta['border_colour']}")
    print(f"canvas 1280x720  mean diff {mean:.2f}  strong {diff_share:.2f}%"
          f"  best shift {shift}px (residual {shift_error:.2f})")
    if problems:
        print("FAIL: " + "; ".join(problems))
        return 1
    print("PASS: the two devices lay the menu out the same way")
    return 0


if __name__ == "__main__":
    sys.exit(main())
