#!/usr/bin/env python3
"""Render the Vulpes GitHub banner + social-preview card.

The motif is the product in one picture: a bearing rose whose spokes are the
measured signal, one of them running long and hot out to a hidden transmitter
that is still radiating. Everything cold and instrument-like except the fox,
which is the one warm thing on the page.

Supersampled, then LANCZOS-downsampled.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

# palette - cold instrument, one warm contact
BG_TOP = (7, 10, 14)
BG_BOT = (13, 18, 26)
INSTR = (86, 214, 196)      # teal: the measurement
FOX = (255, 122, 38)        # fox orange: the contact
HOT = (255, 176, 74)
GRAY = (146, 158, 172)
WHITE = (240, 246, 252)
DIM = (34, 46, 60)

SS = 2  # supersample

# The bearing the fox sits on, and the 12 measured spoke lengths (0..1).
TARGET = 2
SPOKES = [0.30, 0.46, 1.00, 0.52, 0.28, 0.22, 0.18, 0.20, 0.24, 0.26, 0.25, 0.27]


def font(path, px):
    try:
        return ImageFont.truetype(path, px)
    except OSError:
        return ImageFont.truetype(BOLD, px)


def vgradient(w, h):
    img = Image.new("RGB", (w, h), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line(
            [(0, y), (w, y)],
            fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)),
        )
    return img


def sector_vec(i, n=12):
    """Sector i as a unit vector, 0 = up, running clockwise."""
    a = math.radians(i * 360.0 / n)
    return math.sin(a), -math.cos(a)


def build_rose(size, cx, cy, r):
    """The rose: rings, twelve spokes, and the hot bearing running long."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    lw = max(2, int(r * 0.012))

    # range rings
    for k in (0.34, 0.67, 1.0):
        rr = r * k
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=DIM + (255,), width=lw)

    # sector hairlines out to the rim
    for i in range(12):
        dx, dy = sector_vec(i)
        d.line(
            [cx + dx * r * 0.94, cy + dy * r * 0.94, cx + dx * r, cy + dy * r],
            fill=DIM + (255,),
            width=lw,
        )

    # the measured spokes
    for i, mag in enumerate(SPOKES):
        dx, dy = sector_vec(i)
        length = r * mag
        hot = i == TARGET
        col = FOX if hot else INSTR
        d.line(
            [cx, cy, cx + dx * length, cy + dy * length],
            fill=col + (255 if hot else 170,),
            width=lw * (3 if hot else 2),
        )
        tip = r * 0.035 if not hot else r * 0.055
        d.ellipse(
            [
                cx + dx * length - tip,
                cy + dy * length - tip,
                cx + dx * length + tip,
                cy + dy * length + tip,
            ],
            fill=col + (255,),
        )

    d.ellipse([cx - lw * 3, cy - lw * 3, cx + lw * 3, cy + lw * 3], fill=WHITE + (255,))
    return layer


def build_contact(size, cx, cy, r):
    """The fox: a transmitter still radiating, out past the rim on the hot bearing."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    dx, dy = sector_vec(TARGET)
    tx, ty = cx + dx * r * 1.30, cy + dy * r * 1.30
    lw = max(2, int(r * 0.012))

    # carrier arcs, fading outward
    for k in range(1, 6):
        rr = r * 0.09 * k
        d.ellipse(
            [tx - rr, ty - rr, tx + rr, ty + rr],
            outline=HOT + (int(210 * (1 - k / 6.5)),),
            width=lw * 2,
        )

    # the emitter
    d.ellipse([tx - r * 0.05, ty - r * 0.05, tx + r * 0.05, ty + r * 0.05], fill=HOT + (255,))
    return layer, (tx, ty)


def render(path, W, H, layout="wide"):
    w, h = W * SS, H * SS
    img = vgradient(w, h).convert("RGBA")

    if layout == "wide":
        cx, cy, r = int(w * 0.775), int(h * 0.48), int(h * 0.335)
    else:
        cx, cy, r = int(w * 0.50), int(h * 0.255), int(h * 0.185)

    rose = build_rose((w, h), cx, cy, r)
    contact, _ = build_contact((w, h), cx, cy, r)

    img.alpha_composite(rose.filter(ImageFilter.GaussianBlur(5 * SS)))
    img.alpha_composite(rose)
    img.alpha_composite(contact.filter(ImageFilter.GaussianBlur(6 * SS)))
    img.alpha_composite(contact)

    # ---- text ----
    tx = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tx)

    if layout == "wide":
        x0, kicker_y, title_y, title_px = 70 * SS, 92 * SS, 120 * SS, 116 * SS
        anchor = "la"
    else:
        x0, kicker_y, title_y, title_px = w // 2, 318 * SS, 344 * SS, 124 * SS
        anchor = "ma"

    f_kick = font(MONO, 22 * SS)
    f_title = font(BLACK_F, title_px)
    f_tag = font(BOLD, 36 * SS)
    f_sub = font(REG, 23 * SS)
    f_foot = font(MONO, 21 * SS)

    td.text((x0, kicker_y), "FLIPPER ZERO  ·  RF DIRECTION FINDER", font=f_kick, fill=INSTR, anchor=anchor)
    td.text((x0 + 4 * SS, title_y + 4 * SS), "VULPES", font=f_title, fill=FOX + (150,), anchor=anchor)
    td.text((x0, title_y), "VULPES", font=f_title, fill=WHITE, anchor=anchor)

    tag_y = title_y + title_px + 24 * SS
    td.text((x0, tag_y), "Find the hidden transmitter.", font=f_tag, fill=INSTR, anchor=anchor)
    td.text(
        (x0, tag_y + 45 * SS),
        "Hot-and-cold hunting for bugs and trackers on the Sub-GHz bands.",
        font=f_sub,
        fill=GRAY,
        anchor=anchor,
    )

    img.alpha_composite(tx)

    fd = ImageDraw.Draw(img)
    fd.line([(70 * SS, h - 54 * SS), (w - 70 * SS, h - 54 * SS)], fill=DIM, width=2 * SS)
    fd.text(
        (70 * SS, h - 44 * SS),
        "github.com/at0m-b0mb/Vulpes-FlipperZero",
        font=f_foot,
        fill=GRAY,
    )
    fd.text((w - 70 * SS, h - 44 * SS), "MIT · by at0m-b0mb", font=f_foot, fill=GRAY, anchor="ra")

    img.convert("RGB").resize((W, H), Image.LANCZOS).save(path)
    print("wrote", path)


if __name__ == "__main__":
    render(os.path.join(OUT, "banner.png"), 1280, 400, layout="wide")
    render(os.path.join(OUT, "social-preview.png"), 1280, 640, layout="card")
