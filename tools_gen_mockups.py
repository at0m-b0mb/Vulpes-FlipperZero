#!/usr/bin/env python3
"""Render Flipper-style mock screenshots (128x64, orange backlight) for the README.

These mirror the on-device draw code in views/*.c constant for constant, so a
layout collision shows up here before it ships. Text is positioned by BASELINE
(PIL anchor "ls"/"rs"/"ms") because canvas_draw_str takes y as the baseline;
canvas_draw_str_aligned with AlignCenter vertically becomes anchor "mm".
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os

# The real 10x10 glyphs, so the header icons in these mockups are the same
# pixels fbt compiles into the .fap rather than a stand-in box.
from tools_gen_icons import GLYPHS

S = 6  # upscale factor
W, H = 128, 64
BG = (255, 130, 0)  # flipper backlight orange
FG = (10, 8, 4)  # near-black pixels
OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

f_sec = ImageFont.truetype(MONO, 7 * S - 2)  # FontSecondary
f_pri = ImageFont.truetype(BOLD, 8 * S)  # FontPrimary

# ---------------- primitives, matching canvas_* semantics ----------------


def canvas():
    img = Image.new("RGB", (W * S, H * S), BG)
    return img, ImageDraw.Draw(img)


def L(v):
    return int(round(v * S))


def line(d, x0, y0, x1, y1, col=FG, w=2):
    d.line([L(x0), L(y0), L(x1), L(y1)], fill=col, width=w)


def box(d, x, y, w, h, col=FG):
    """canvas_draw_box: filled, inclusive of (x,y)..(x+w-1, y+h-1)."""
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], fill=col)


def frame(d, x, y, w, h, col=FG, lw=2):
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], outline=col, width=lw)


def dot(d, x, y, col=FG):
    d.rectangle([L(x), L(y), L(x + 1) - 1, L(y + 1) - 1], fill=col)


def circle(d, cx, cy, r, col=FG, lw=2):
    d.ellipse([L(cx - r), L(cy - r), L(cx + r), L(cy + r)], outline=col, width=lw)


def disc(d, cx, cy, r, col=FG):
    d.ellipse([L(cx - r), L(cy - r), L(cx + r), L(cy + r)], fill=col)


def text(d, x, y, s, fnt=f_sec, col=FG, anchor="ls"):
    """y is the BASELINE, matching canvas_draw_str."""
    d.text((L(x), L(y)), s, font=fnt, fill=col, anchor=anchor)


def tw(s, fnt=f_sec):
    """String width in Flipper pixels (mirrors canvas_string_width)."""
    return fnt.getlength(s) / S


def save(img, name):
    p = os.path.join(OUT, name)
    img.save(p)
    print("wrote", p)


# ---------------- shared chrome ----------------

HDR_BASE, RULE_Y = 9, 11


def glyph(d, x, y, name):
    """canvas_draw_icon: blit a 1-bit 10x10 icon at (x, y)."""
    for gy, row in enumerate(GLYPHS[name]):
        for gx, ch in enumerate(row):
            if ch == "#":
                dot(d, x + gx, y + gy)


def header(d, left, right, icon=None):
    """`icon` mirrors canvas_draw_icon at (1,1): the label shifts to x=14."""
    if icon:
        glyph(d, 1, 1, icon)
    text(d, 14 if icon else 2, HDR_BASE, left)
    text(d, 126, HDR_BASE, right, anchor="rs")
    line(d, 0, RULE_Y, 127, RULE_Y)


def footer(d, y, left, right):
    """Inverted strip: box in FG, text knocked out in BG."""
    box(d, 0, y, 128, 64 - y)
    text(d, 3, 62, left, col=BG)
    text(d, 125, 62, right, col=BG, anchor="rs")


# ---------------- splash (views/splash_view.c) ----------------

SP_CX, SP_CY, SP_R, SP_STEPS, SP_TARGET = 64, 24, 19, 16, 5


def sp_vec(k, n=SP_STEPS):
    a = math.radians(k * 360.0 / n)
    return math.sin(a), -math.cos(a)


def draw_splash(k=7, progress=62):
    img, d = canvas()
    circle(d, SP_CX, SP_CY, SP_R)

    dx, dy = sp_vec(k)
    line(d, SP_CX, SP_CY, SP_CX + dx * SP_R, SP_CY + dy * SP_R)
    disc(d, SP_CX, SP_CY, 1)

    dx1, dy1 = sp_vec((k - 1) % SP_STEPS)
    line(d, SP_CX, SP_CY, SP_CX + dx1 * (SP_R - 4), SP_CY + dy1 * (SP_R - 4))

    tdx, tdy = sp_vec(SP_TARGET)
    tx, ty = SP_CX + tdx * (SP_R - 6), SP_CY + tdy * (SP_R - 6)
    age = (k - SP_TARGET) % SP_STEPS
    if age < 6:
        disc(d, tx, ty, 2)
        circle(d, tx, ty, 3 + age)
    else:
        dot(d, tx, ty)

    text(d, 64, 52, "VULPES", f_pri, anchor="ms")
    text(d, 64, 61, "fox hunt for hidden RF", anchor="ms")
    line(d, 0, 63, (128 * progress) // 100, 63)
    return img


# ---------------- menu ----------------


def draw_menu():
    img, d = canvas()
    text(d, 64, 10, "Vulpes", f_pri, anchor="ms")
    line(d, 0, 12, 127, 12)
    items = [
        "Survey - what is out there",
        "Hunt - hot and cold",
        "Bearing - which way",
        "Settings",
        "About",
    ]
    y = 14
    for i, it in enumerate(items):
        if i == 0:
            box(d, 0, y, 128, 12)
            text(d, 4, y + 9, it, col=BG)
        else:
            text(d, 4, y + 9, it)
        y += 12
    return img


# ---------------- survey (views/survey_view.c) ----------------

SV_SPEC_TOP, SV_SPEC_BASE = 16, 40
SV_SPEC_H = SV_SPEC_BASE - SV_SPEC_TOP
SV_READ_BASE, SV_FOOTER_Y = 50, 53
RANGE_DB = 60


def bar_h(margin):
    if margin <= 0:
        return 0
    if margin >= RANGE_DB:
        return SV_SPEC_H
    return (margin * SV_SPEC_H) // RANGE_DB


def draw_survey():
    img, d = canvas()
    header(d, "SURVEY", "433 ISM", icon="wave_10px")

    # A plausible max-hold: mostly floor, three carriers, one of them wide.
    margins = [0] * 64
    for i in range(64):
        margins[i] = (i * 7 + 3) % 5  # a little floor texture
    for centre, peak, width in ((18, 42, 1), (33, 27, 2), (52, 13, 0)):
        for o in range(-width, width + 1):
            margins[centre + o] = max(margins[centre + o], peak - abs(o) * 9)

    line(d, 0, SV_SPEC_BASE + 1, 127, SV_SPEC_BASE + 1)
    for b in range(64):
        h = bar_h(margins[b])
        if h > 0:
            box(d, b * 2, SV_SPEC_BASE - h, 2, h)
        else:
            dot(d, b * 2, SV_SPEC_BASE)

    for i, b in enumerate((18, 33, 52)):
        x = b * 2
        if i == 0:  # selected
            for y in range(SV_SPEC_TOP - 1, SV_SPEC_BASE + 1, 3):
                dot(d, x, y)
                dot(d, x + 1, y)
            box(d, x - 1, SV_SPEC_TOP - 3, 4, 3)
        else:
            dot(d, x, SV_SPEC_TOP - 2)
            dot(d, x + 1, SV_SPEC_TOP - 2)

    text(d, 2, SV_READ_BASE, "433.54 MHz  -55 dBm  +42")
    footer(d, SV_FOOTER_Y, "3 found  214 sw  -97", "OK hunt")
    return img


# ---------------- hunt (views/hunt_view.c) ----------------

HV_HEAT_BASE, HV_MARK_BASE = 24, 33
HV_TRACE_TOP, HV_TRACE_BASE = 35, 51
HV_TRACE_H = HV_TRACE_BASE - HV_TRACE_TOP
HV_FOOTER_Y = 53
HV_ARROW_CX, HV_ARROW_CY = 118, 19


def draw_arrow(d, trend):
    cx, cy = HV_ARROW_CX, HV_ARROW_CY
    if trend == "up":
        line(d, cx, cy - 5, cx - 5, cy + 4)
        line(d, cx, cy - 5, cx + 5, cy + 4)
        line(d, cx - 5, cy + 4, cx + 5, cy + 4)
    elif trend == "down":
        line(d, cx, cy + 5, cx - 5, cy - 4)
        line(d, cx, cy + 5, cx + 5, cy - 4)
        line(d, cx - 5, cy - 4, cx + 5, cy - 4)
    elif trend == "flat":
        line(d, cx - 5, cy - 2, cx + 5, cy - 2)
        line(d, cx - 5, cy + 2, cx + 5, cy + 2)


def draw_hunt(word, trend, mark_line, foot_left, foot_right, trace, peak, boxed=False):
    img, d = canvas()
    header(d, "HUNT", "433.54")

    text(d, 56, HV_HEAT_BASE - 5, word, f_pri, anchor="mm")
    if boxed:
        w = tw(word, f_pri)
        frame(d, 56 - w / 2 - 3, HV_HEAT_BASE - 11, w + 6, 13)

    draw_arrow(d, trend)
    text(d, 64, HV_MARK_BASE, mark_line, anchor="ms")

    line(d, 0, HV_TRACE_BASE + 1, 127, HV_TRACE_BASE + 1)
    for k in range(64):
        v = trace[k]
        x = 126 - k * 2
        y = HV_TRACE_BASE - (v * HV_TRACE_H) // 100
        if y < HV_TRACE_BASE:
            line(d, x, HV_TRACE_BASE, x, y)
        else:
            dot(d, x, HV_TRACE_BASE)

    if peak > 0:
        py = HV_TRACE_BASE - (peak * HV_TRACE_H) // 100
        for x in range(0, 128, 4):
            dot(d, x, py)

    footer(d, HV_FOOTER_Y, foot_left, foot_right)
    return img


def approach_trace(end, span):
    """Newest first, as the view walks it: a climb with a little hand wobble."""
    out = []
    for k in range(64):
        t = (63 - k) / 63.0
        v = end - span * (1 - t)
        v += 3 * math.sin(k * 0.9)
        out.append(max(0, min(100, int(v))))
    return out


# ---------------- signal card (views/hunt_view.c, page 2) ----------------


def draw_signal(kind, duty, period, hint, foot):
    img, d = canvas()
    header(d, "SIGNAL", "433.54")
    text(d, 64, 21, kind, f_pri, anchor="mm")
    text(d, 2, 34, duty)
    text(d, 126, 34, period, anchor="rs")
    line(d, 0, 37, 127, 37)
    y = 46
    for ln in hint.split("\n"):
        text(d, 2, y, ln)
        y += 9
    text(d, 2, 62, foot)
    return img


# ---------------- bearing (views/bearing_view.c) ----------------

BV_CX, BV_CY, BV_R = 34, 38, 23
BV_SPOKE_MIN, BV_SPOKE_MAX = 5, 21
BV_PANEL_X = 64
SECTORS = 12


def bv_vec(i):
    a = math.radians(i * 360.0 / SECTORS)
    return math.sin(a), -math.cos(a)


def draw_bearing(rose, state, panel, best=None, live=None):
    img, d = canvas()
    header(d, "BEARING", "433.54", icon="bearing_10px")

    circle(d, BV_CX, BV_CY, BV_R)

    vals = [v for v in rose if v is not None]
    lo, hi = (min(vals), max(vals)) if vals else (0, 0)
    span = hi - lo

    for i, v in enumerate(rose):
        dx, dy = bv_vec(i)
        if v is None:
            dot(d, BV_CX + dx * BV_R, BV_CY + dy * BV_R)
            continue
        if span > 0:
            ln = BV_SPOKE_MIN + ((v - lo) * (BV_SPOKE_MAX - BV_SPOKE_MIN)) // span
        else:
            ln = (BV_SPOKE_MIN + BV_SPOKE_MAX) // 2
        x, y = BV_CX + dx * ln, BV_CY + dy * ln
        line(d, BV_CX, BV_CY, x, y)
        if best is not None and i == best:
            disc(d, x, y, 3)
        elif live is not None and i == live:
            circle(d, x, y, 2)

    disc(d, BV_CX, BV_CY, 1)
    line(d, BV_CX, BV_CY - BV_R - 3, BV_CX, BV_CY - BV_R - 1)

    for y, s, fnt in panel:
        text(d, BV_PANEL_X, y, s, fnt)
    return img, d


# ---------------- settings ----------------


def draw_settings():
    img, d = canvas()
    rows = [
        ("Survey band", "433 ISM"),
        ("Turn time", "12 s"),
        ("Attenuator", "AUTO"),
        ("Sound", "ON"),
        ("LED", "ON"),
    ]
    y = 0
    for i, (k, v) in enumerate(rows):
        if i == 0:
            box(d, 0, y, 128, 12)
            text(d, 4, y + 9, k, col=BG)
            text(d, 124, y + 9, "< " + v + " >", col=BG, anchor="rs")
        else:
            text(d, 4, y + 9, k)
            text(d, 124, y + 9, v, anchor="rs")
        y += 12
    return img


# ---------------- the sheet ----------------


def contact_sheet(names, cols=3, pad=8):
    ims = [Image.open(os.path.join(OUT, n)) for n in names]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w + (cols + 1) * pad, rows * h + (rows + 1) * pad), (18, 18, 20))
    for i, im in enumerate(ims):
        r, c = divmod(i, cols)
        sheet.paste(im, (pad + c * (w + pad), pad + r * (h + pad)))
    save(sheet, "screens.png")


if __name__ == "__main__":
    save(draw_splash(), "screen_splash.png")
    save(draw_menu(), "screen_menu.png")
    save(draw_survey(), "screen_survey.png")

    save(
        draw_hunt(
            "WARM",
            "up",
            "OK: mark this distance",
            "-71 dBm  +19",
            "84%",
            approach_trace(48, 26),
            52,
        ),
        "screen_hunt_warm.png",
    )
    save(
        draw_hunt(
            "ON TOP",
            "up",
            "~3.5x closer than mark",
            "-38 dBm  +52",
            "ATT -400kHz",
            approach_trace(92, 44),
            95,
            boxed=True,
        ),
        "screen_hunt_ontop.png",
    )
    save(
        draw_hunt(
            "COOL",
            "down",
            "~0.5x further than mark",
            "-89 dBm  +8",
            "71%",
            approach_trace(16, -14),
            46,
        ),
        "screen_hunt_cold.png",
    )

    save(
        draw_signal(
            "PERIODIC",
            "Duty 4%",
            "every 1.0s",
            "Beacons on a clock.\nTracker or telemetry.",
            "498 Hz  37s listened",
        ),
        "screen_signal.png",
    )

    turning = [28, 31, 34, 40, None, None, None, None, None, None, 22, 25]
    img, _ = draw_bearing(
        turning,
        "turning",
        [(21, "TURNING", f_sec), (36, "58%", f_pri), (47, "sector 4/12", f_sec), (58, "+34 dB", f_sec)],
        live=3,
    )
    save(img, "screen_bearing_turn.png")

    done = [24, 29, 36, 46, 38, 27, 21, 19, 20, 22, 23, 25]
    img, d = draw_bearing(
        done,
        "done",
        [
            (21, "LOUDEST AT", f_sec),
            (37, "90", f_pri),
            (48, "from the start", f_sec),
            (58, "peak 21 dB", f_sec),
        ],
        best=3,
    )
    circle(d, BV_PANEL_X + tw("90", f_pri) + 4, 29, 2)  # the degree ring
    save(img, "screen_bearing_done.png")

    flat = [26, 25, 27, 28, 26, 25, 24, 26, 27, 25, 26, 27]
    img, _ = draw_bearing(
        flat,
        "done",
        [
            (22, "TOO FLAT", f_pri),
            (34, "No direction", f_sec),
            (43, "to be had here.", f_sec),
            (55, "peak 3 dB", f_sec),
            (63, "OK: again", f_sec),
        ],
    )
    save(img, "screen_bearing_flat.png")

    save(draw_settings(), "screen_settings.png")

    contact_sheet(
        [
            "screen_survey.png",
            "screen_hunt_warm.png",
            "screen_hunt_ontop.png",
            "screen_signal.png",
            "screen_bearing_turn.png",
            "screen_bearing_done.png",
        ]
    )
