"""Parhelion's icon (Phase 6): the sun with its 22-degree halo and the two sun dogs on it, on a night-blue ground.

    python Deploy/make_icon.py        # writes Deploy/parhelion.png (512), parhelion_256.png and parhelion.ico,
                                      # and the Quest's launcher icons, Quest/res/mipmap-*/ic_launcher.png (Phase 7)

Drawn in code from circles and gradients, supersampled four times; nothing is taken from elsewhere.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
S = 2048            # drawn at four times 512
C = S / 2


def radial(img, cx, cy, r, color, alpha):
    """A soft glow: a disc blurred to a gradient."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color + (alpha,))
    layer = layer.filter(ImageFilter.GaussianBlur(r * 0.6))
    img.alpha_composite(layer)


def main():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # The ground: a rounded square, night blue to a violet horizon.
    ground = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(ground)
    for y in range(S):
        t = y / S
        col = (int(10 + 40 * t), int(14 + 20 * t), int(40 + 60 * t), 255)
        g.line([(0, y), (S, y)], fill=col)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=int(S * 0.2), fill=255)
    img.paste(ground, (0, 0), mask)
    # The halo: a thin ring, reddish inside, bluish-white outside (the ice crystals' refraction).
    R = S * 0.30
    halo = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    h = ImageDraw.Draw(halo)
    for k, (col, a) in enumerate([((255, 150, 90), 120), ((255, 220, 170), 150), ((210, 230, 255), 110)]):
        rr = R + (k - 1) * S * 0.008
        h.ellipse((C - rr, C - rr, C + rr, C + rr), outline=col + (a,), width=int(S * 0.008))
    halo = halo.filter(ImageFilter.GaussianBlur(S * 0.004))
    img.alpha_composite(halo)
    # A faint horizontal arc through the three, under them (the parhelic circle).
    arc = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(arc).line([(C - R * 1.45, C), (C + R * 1.45, C)], fill=(255, 245, 230, 60), width=int(S * 0.004))
    img.alpha_composite(arc.filter(ImageFilter.GaussianBlur(S * 0.003)))
    # The sun dogs on the ring: bright spots with a short tail outward.
    for side in (-1, 1):
        x = C + side * R
        radial(img, x, C, S * 0.07, (255, 200, 140), 160)
        tail = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        td = ImageDraw.Draw(tail)
        for i in range(40):
            t = i / 40
            w = S * (0.03 * (1 - t))
            xx = x + side * t * S * 0.16
            td.ellipse((xx - w, C - w * 0.35, xx + w, C + w * 0.35), fill=(255, 235, 210, int(90 * (1 - t))))
        img.alpha_composite(tail.filter(ImageFilter.GaussianBlur(S * 0.006)))
        radial(img, x, C, S * 0.022, (255, 250, 235), 255)
    # The sun: a warm glow and a white core.
    radial(img, C, C, S * 0.16, (255, 170, 80), 180)
    radial(img, C, C, S * 0.09, (255, 225, 170), 230)
    core = ImageDraw.Draw(img)
    rc = S * 0.05
    core.ellipse((C - rc, C - rc, C + rc, C + rc), fill=(255, 252, 240, 255))
    # Keep the rounded square's edge clean.
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    big = out.resize((512, 512), Image.LANCZOS)
    big.save(HERE / "parhelion.png")
    out.resize((256, 256), Image.LANCZOS).save(HERE / "parhelion_256.png")
    big.save(HERE / "parhelion.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    # The Quest's launcher icon at Android's five densities (48 dp).
    for name, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        d = HERE.parent / "Quest" / "res" / ("mipmap-" + name)
        d.mkdir(parents=True, exist_ok=True)
        out.resize((size, size), Image.LANCZOS).save(d / "ic_launcher.png")
    print("wrote Deploy/parhelion.png, parhelion_256.png, parhelion.ico and Quest/res/mipmap-*/ic_launcher.png")


if __name__ == "__main__":
    main()
