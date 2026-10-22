"""Replace the borrowed Diablo II launcher artwork with a neutral original icon.

The upstream tree ships a Diablo II: Resurrected key-art crop as the Android and
iOS launcher icon. This repository only publishes the compatibility framework, so
that artwork is removed and every icon slot is regenerated from geometric shapes:
no game art, no third-party assets, reproducible by running this script.
"""
import os
from PIL import Image, ImageDraw

# 仓库根目录：本脚本在 <root>/dm/android/tools/ 下，往上四层。
# Repository root: this script lives in <root>/dm/android/tools/, four levels down.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BG = (27, 21, 18, 255)        # weathered leather, matches the in-game UI palette
GOLD = (216, 177, 115, 255)   # aged gold
RIM = (138, 108, 68, 255)


def draw(size, rounded=False):
    scale = 8  # supersample, then downscale for clean edges
    n = size * scale
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0) if rounded else BG)
    d = ImageDraw.Draw(im)
    if rounded:
        d.rounded_rectangle([0, 0, n - 1, n - 1], radius=int(n * 0.22), fill=BG)
    cx = cy = n / 2
    # outer ring
    r1 = n * 0.34
    d.ellipse([cx - r1, cy - r1, cx + r1, cy + r1], outline=GOLD, width=max(2, int(n * 0.045)))
    # inner ring
    r2 = n * 0.22
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], outline=RIM, width=max(1, int(n * 0.02)))
    # four cardinal ticks
    w = max(2, int(n * 0.035))
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        x0 = cx + dx * r1 * 0.78 - (w / 2 if dy else 0)
        y0 = cy + dy * r1 * 0.78 - (w / 2 if dx else 0)
        x1 = x0 + (w if dy else w * 1.6)
        y1 = y0 + (w if dx else w * 1.6)
        d.rectangle([x0, y0, x1, y1], fill=GOLD)
    # centre gem
    r3 = n * 0.11
    d.ellipse([cx - r3, cy - r3, cx + r3, cy + r3], fill=GOLD)
    return im.resize((size, size), Image.LANCZOS)


def main():
    targets = [
        ("dm/android/artwork/app-icon.png", 512, True),
        ("dm/android/res/drawable-nodpi/ic_launcher_foreground.png", 432, False),
        ("dm/android/res/mipmap-mdpi/ic_launcher.png", 48, False),
        ("dm/android/res/mipmap-hdpi/ic_launcher.png", 72, False),
        ("dm/android/res/mipmap-xhdpi/ic_launcher.png", 96, False),
        ("dm/android/res/mipmap-xxhdpi/ic_launcher.png", 144, False),
        ("dm/android/res/mipmap-xxxhdpi/ic_launcher.png", 192, False),
        ("dm/ios/Assets.xcassets/AppIcon.appiconset/AppIcon1024.png", 1024, False),
    ]
    for rel, size, rounded in targets:
        path = os.path.join(ROOT, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        draw(size, rounded).save(path)
        print(f"{rel} -> {size}x{size} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
