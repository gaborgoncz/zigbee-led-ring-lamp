#!/usr/bin/env python3
"""Draws docs/wiring.png, the wiring picture in the README.

Nothing here is a photo: the board and every part are drawn in code. Everything
is drawn at twice the size and scaled down, which keeps lines and text smooth.

    python3 docs/make_wiring.py

Needs Pillow. The font path is the macOS Helvetica; change FONT elsewhere.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wiring.png")
FONT = "/System/Library/Fonts/Helvetica.ttc"

S = 2                                  # supersampling factor
WIRE, CASING, LANE = 6, 12, 22
RED, ORANGE, GRAY = (226, 75, 74), (240, 125, 20), (90, 90, 88)
BLUE, GREEN, PURPLE = (55, 138, 221), (99, 153, 34), (142, 68, 173)
TEXT, MUTED = (24, 28, 36), (110, 116, 128)
GOLD, GOLD_EDGE = (222, 186, 90), (150, 120, 50)
PANEL, PANEL_EDGE = (246, 247, 249), (226, 229, 234)
SILVER, SILVER_EDGE, STEEL = (196, 199, 205), (128, 131, 138), (104, 107, 114)
T_GREEN, T_PURPLE, T_TEAL, T_SLATE, T_SALMON, T_RED, T_BLACK = (
    (0, 168, 62), (128, 108, 228), (0, 178, 158), (104, 120, 142), (248, 160, 130), (226, 38, 38), (16, 16, 18))


def font(size, bold=False):
    return ImageFont.truetype(FONT, size * S, index=1 if bold else 0)


kit_font, title_font = font(44, True), font(40)
module_font, legend_font = font(30, True), font(28)
pin_font, note_font, chip_font = font(25), font(24), font(17, True)
tag_font, silk_font, tiny_font = font(21, True), font(17, True), font(12, True)


class Layer:
    """Takes ordinary coordinates and draws at S times the size."""

    def __init__(self, w, h):
        self.size = (w, h)
        self.im = Image.new("RGBA", (w * S, h * S), "white")
        self.d = ImageDraw.Draw(self.im, "RGBA")

    def text(self, xy, text, fnt, fill=TEXT, anchor="la"):
        self.d.text((xy[0] * S, xy[1] * S), text, font=fnt, fill=fill, anchor=anchor)

    def rect(self, box, radius, fill, outline=None, width=0):
        self.d.rounded_rectangle([v * S for v in box], radius=radius * S, fill=fill, outline=outline, width=width * S)

    def circle(self, p, r, fill, outline=None, width=0):
        self.d.ellipse(((p[0] - r) * S, (p[1] - r) * S, (p[0] + r) * S, (p[1] + r) * S),
                       fill=fill, outline=outline, width=width * S)

    def line(self, pts, colour, width=WIRE, radius=14):
        """A line through pts with rounded corners."""
        pts = [(x * S, y * S) for x, y in pts]
        w, d = width * S, self.d
        prev = pts[0]
        for i in range(1, len(pts) - 1):
            a, p, b = pts[i - 1], pts[i], pts[i + 1]
            la, lb = math.dist(a, p), math.dist(p, b)
            if la == 0 or lb == 0:
                continue
            u = ((p[0] - a[0]) / la, (p[1] - a[1]) / la)
            v = ((b[0] - p[0]) / lb, (b[1] - p[1]) / lb)
            r = min(radius * S, la / 2, lb / 2)
            if u == v or r < 2:
                d.line((prev, p), fill=colour, width=w)
                prev = p
                continue
            p_in = (p[0] - u[0] * r, p[1] - u[1] * r)
            p_out = (p[0] + v[0] * r, p[1] + v[1] * r)
            c = (p_in[0] + v[0] * r, p_in[1] + v[1] * r)
            d.line((prev, p_in), fill=colour, width=w)
            a1 = math.degrees(math.atan2(p_in[1] - c[1], p_in[0] - c[0])) % 360
            a2 = math.degrees(math.atan2(p_out[1] - c[1], p_out[0] - c[0])) % 360
            start, end = (a1, a2) if round((a2 - a1) % 360) == 90 else (a2, a1)
            R = r + w / 2
            d.arc((c[0] - R, c[1] - R, c[0] + R, c[1] + R), start, end, fill=colour, width=w)
            for q in (p_in, p_out):        # hide the seam between line and arc
                d.ellipse((q[0] - w / 2, q[1] - w / 2, q[0] + w / 2, q[1] + w / 2), fill=colour)
            prev = p_out
        d.line((prev, pts[-1]), fill=colour, width=w)

    def wire(self, pts, colour):
        """A wire with a white casing, so it reads clearly where it crosses another."""
        self.line(pts, "white", CASING)
        self.line(pts, colour)

    def end(self, p, colour):
        """Where a wire is soldered: a dot in the wire's colour."""
        self.circle(p, 9, colour, (255, 255, 255), 2)

    def save(self, path):
        self.im.resize(self.size, Image.LANCZOS).convert("RGB").save(path, optimize=True)


# ------------------------------------------------------ the XIAO ESP32-C6 --
LEFT_PADS = ["D0", "D1", "D2", "D3", "D4", "D5", "D6"]
RIGHT_PADS = ["5V", "GND", "3V3", "D10", "D9", "D8", "D7"]
# Each pad's other names, shown as extra tags beside it.
PAD_EXTRAS = {"D0": [("GPIO0", T_SLATE), ("A0", T_SALMON)], "D1": [("GPIO1", T_SLATE), ("A1", T_SALMON)],
              "D2": [("GPIO2", T_SLATE), ("A2", T_SALMON)], "D3": [("GPIO21", T_SLATE)],
              "D4": [("GPIO22", T_SLATE), ("SDA", T_TEAL)], "D5": [("GPIO23", T_SLATE), ("SCL", T_TEAL)],
              "D6": [("GPIO16", T_SLATE), ("TX", T_PURPLE)], "D10": [("GPIO18", T_SLATE), ("MOSI", T_PURPLE)],
              "D9": [("GPIO20", T_SLATE), ("MISO", T_PURPLE)], "D8": [("GPIO19", T_SLATE), ("SCK", T_PURPLE)],
              "D7": [("GPIO17", T_SLATE), ("RX", T_PURPLE)]}
BOARD_W, BOARD_H, PAD_PITCH, PAD_TOP, BOARD_Y = 330, 420, 49, 48, 204


def tag_width(text):
    return tag_font.getlength(text) / S + 22


def tags_extent(names):
    return max(sum(tag_width(t) + 12 for t in [n] + [e[0] for e in PAD_EXTRAS.get(n, [])]) for n in names)


def board_pads(x):
    pads = {n: (x + 17, BOARD_Y + PAD_TOP + i * PAD_PITCH) for i, n in enumerate(LEFT_PADS)}
    pads.update({n: (x + BOARD_W - 17, BOARD_Y + PAD_TOP + i * PAD_PITCH) for i, n in enumerate(RIGHT_PADS)})
    return pads


def draw_board(layer, x, gap_l, gap_r):
    """The XIAO ESP32-C6 from above, left edge at x. gap_l / gap_r is the clear
    space between the board and its pin tags, where the wires run."""
    y, cx, box = BOARD_Y, x + BOARD_W / 2, layer.rect

    def tag(text, colour, tx, ty, side):
        w = tag_width(text)
        x0 = tx if side > 0 else tx - w
        box((x0, ty - 17, x0 + w, ty + 17), 6, colour)
        layer.text((x0 + w / 2, ty + 1), text, tag_font, "white", "mm")
        return w

    box((x + 6, y + 8, x + BOARD_W + 6, y + BOARD_H + 8), 22, (0, 0, 0, 40))
    box((x, y, x + BOARD_W, y + BOARD_H), 22, (26, 28, 34), (6, 6, 8), 2)

    for name, (px, py) in board_pads(x).items():
        side = -1 if name in LEFT_PADS else 1
        box((px - 17, py - 15, px + 17, py + 15), 9, GOLD, GOLD_EDGE, 1)
        layer.circle((px, py), 8, (245, 245, 240), GOLD_EDGE, 1)
        colour = T_RED if name in ("5V", "3V3") else T_BLACK if name == "GND" else T_GREEN
        tx = (x - gap_l) if side < 0 else (x + BOARD_W + gap_r)
        for text, colour in [(name, colour)] + PAD_EXTRAS.get(name, []):
            tx += side * (tag(text, colour, tx, py, side) + 12)
        layer.text((px + side * -24, py), "G" if name == "GND" else name, silk_font, (232, 232, 236),
                   "lm" if side < 0 else "rm")

    # USB-C socket, overhanging the top edge.
    box((cx - 70, y - 30, cx + 70, y + 96), 16, SILVER, SILVER_EDGE, 2)
    box((cx - 54, y - 20, cx + 54, y + 4), 10, (150, 153, 160), STEEL, 2)
    box((cx - 42, y - 14, cx + 42, y - 4), 5, (34, 36, 42))

    # BOOT and RESET buttons, either side of the socket's back end.
    for bx, label in ((cx - 48, "BOOT"), (cx + 48, "RESET")):
        box((bx - 20, y + 112, bx + 20, y + 140), 4, SILVER, SILVER_EDGE, 2)
        layer.circle((bx, y + 126), 8, (70, 73, 82), (40, 42, 48), 2)
        layer.text((bx, y + 152), label, tiny_font, (225, 225, 230), "mm")

    # The radio module under its metal shield.
    box((cx - 78, y + 172, cx + 78, y + 318), 8, (206, 208, 214), SILVER_EDGE, 2)
    box((cx - 70, y + 180, cx + 70, y + 310), 6, (188, 191, 198))
    layer.text((cx, y + 236), "ESP32-C6", chip_font, (60, 62, 70), "mm")
    layer.text((cx, y + 258), "Zigbee", chip_font, (60, 62, 70), "mm")

    # Ceramic antenna and the U.FL socket for the external one, at the far end.
    box((cx - 6, y + BOARD_H - 62, cx + 96, y + BOARD_H - 28), 4, (236, 236, 232), (150, 150, 146), 2)
    box((cx + 14, y + BOARD_H - 58, cx + 76, y + BOARD_H - 32), 2, (204, 54, 54))
    box((x + 64, y + BOARD_H - 66, x + 104, y + BOARD_H - 26), 5, GOLD, GOLD_EDGE, 2)
    layer.circle((x + 84, y + BOARD_H - 46), 11, (240, 240, 236), GOLD_EDGE, 2)
    layer.circle((x + 84, y + BOARD_H - 46), 4, GOLD_EDGE)
    layer.text((x + 112, y + BOARD_H - 46), "U.FL", tiny_font, (225, 225, 230), "lm")



# ------------------------------------------------------------- the parts --
def draw_resistor(layer, x0, x1, y, label, value):
    """A resistor lying between x0 and x1, with the bands of 330 ohm. Label below it."""
    layer.line([(x0, y), (x1, y)], (150, 152, 158), 4)
    left, right = (x0 + x1) / 2 - 36, (x0 + x1) / 2 + 36
    layer.rect((left, y - 15, right, y + 15), 10, (222, 200, 160), (150, 128, 90), 2)
    for k, colour in enumerate(((236, 120, 30), (236, 120, 30), (120, 70, 30), (200, 160, 40))):
        xx = left + 12 + k * 13 + (6 if k == 3 else 0)
        layer.rect((xx, y - 15, xx + 7, y + 15), 0, colour)
    layer.text(((x0 + x1) / 2 - 30, y + 42), label, pin_font, TEXT, "rm")
    layer.text(((x0 + x1) / 2 - 18, y + 42), value, note_font, MUTED, "lm")


def draw_electrolytic(layer, cx, y0, y1):
    """An upright electrolytic capacitor between y0 and y1, + at the top."""
    layer.line([(cx, y0), (cx, y1)], (150, 152, 158), 4)
    top, bottom = (y0 + y1) / 2 - 44, (y0 + y1) / 2 + 44
    layer.rect((cx - 26, top, cx + 26, bottom), 8, (40, 60, 120), (20, 30, 70), 2)
    layer.rect((cx + 8, top, cx + 22, bottom), 0, (200, 205, 215))
    layer.text((cx + 15, bottom - 14), "−", tiny_font, (40, 60, 120), "mm")
    layer.rect((cx - 26, top, cx + 26, top + 10), 4, SILVER, SILVER_EDGE, 1)
    layer.text((cx + 40, (y0 + y1) / 2 - 14), "C1", pin_font, TEXT, "lm")
    layer.text((cx + 40, (y0 + y1) / 2 + 14), "1000 µF, 10 V", note_font, MUTED, "lm")


# The ring as sold: 32 mm across, 16 mm hole, marked "6812-8D-32mm" on the back. Its four pads
# sit in one row near the edge, in this order when the back faces you.
RING_PADS = ["DO", "GND", "5V", "DI"]


def draw_ring_back(layer, cx, cy):
    """The back of the ring, pads at the bottom. Returns {pad: (x, y)}."""
    outer, inner, pitch, pad_y = 180, 90, 44, cy + 132
    layer.circle((cx + 5, cy + 7), outer, (0, 0, 0, 40))
    layer.circle((cx, cy), outer, (26, 28, 34), (6, 6, 8), 2)
    layer.circle((cx, cy), inner, "white", (6, 6, 8), 2)
    layer.circle((cx, cy), (outer + inner) / 2 + 22, None, (52, 56, 64), 2)   # copper ring under the mask
    layer.text((cx, cy - 128), "6812-8D-32mm", silk_font, (232, 232, 236), "mm")
    layer.circle((cx - 132, cy + 92), 11, "white", (150, 152, 158), 2)        # mounting hole
    layer.circle((cx + 128, cy - 96), 7, GOLD, GOLD_EDGE, 1)                 # test pad
    at = {}
    for k, name in enumerate(RING_PADS):
        p = (cx + (k - 1.5) * pitch, pad_y)
        layer.rect((p[0] - 16, pad_y - 19, p[0] + 16, pad_y + 19), 3, SILVER, SILVER_EDGE, 1)
        layer.text((p[0], pad_y - 36), name, silk_font, (232, 232, 236), "mm")
        at[name] = p
    return at


def draw_ring_front(layer, cx, cy, count=8):
    """The ring from the front, smaller: an LED on every step."""
    outer, inner = 90, 45
    layer.circle((cx + 4, cy + 5), outer, (0, 0, 0, 40))
    layer.circle((cx, cy), outer, (26, 28, 34), (6, 6, 8), 2)
    layer.circle((cx, cy), inner, "white", (6, 6, 8), 2)
    mid = (outer + inner) / 2
    glow = [(255, 170, 70), (255, 140, 60), (255, 110, 80), (250, 90, 120),
            (200, 90, 200), (120, 120, 250), (90, 180, 240), (150, 220, 150)]
    for k in range(count):
        a = -math.pi / 2 + k * 2 * math.pi / count
        px, py = cx + mid * math.cos(a), cy + mid * math.sin(a)
        layer.rect((px - 13, py - 13, px + 13, py + 13), 3, (240, 240, 236), (150, 150, 146), 2)
        layer.circle((px, py), 8, glow[k], (255, 255, 255), 1)


def draw_antenna(layer, x, y, socket):
    """A flexible 2.4 GHz antenna lying under the board, on a short lead from the U.FL socket."""
    layer.line([socket, (socket[0], y + 18), (x + 6, y + 18)], (30, 30, 34), 4, 8)
    layer.rect((x, y, x + 230, y + 36), 6, (40, 42, 48), (10, 10, 12), 2)
    layer.rect((x + 12, y + 8, x + 218, y + 28), 3, (206, 160, 60))
    layer.text((x + 115, y + 19), "2.4 GHz antenna", tiny_font, (40, 30, 10), "mm")


# ----------------------------------------------------------- the picture --
def main():
    # Two wires leave the right side of the board and one the left.
    gap = 3 * LANE + 30
    bx = int(40 + tags_extent(LEFT_PADS) + gap)
    br = bx + BOARD_W
    pads = board_pads(bx)
    tags_r = br + gap + tags_extent(RIGHT_PADS)
    rcx, rcy = int(tags_r + 60 + 180), 400   # back of the ring
    fcx = rcx + 180 + 150                    # front of the ring, smaller
    W = int(fcx + 260)

    bottom = BOARD_Y + BOARD_H
    ant_y = bottom + 30
    rail_5v = ant_y + 36 + 54              # 5V and GND run under the board with C1 between them
    rail_gnd = rail_5v + 170
    rail_din = rail_gnd + 60               # the data line, with R1 on it
    H = int(rail_din + 70 + 120)
    layer = Layer(W, H)

    layer.text((40, 26), "LED lamp: wiring", kit_font)
    layer.text((40, 84), "Zigbee lamp with an 8-LED SK6812 / WS2812B ring", note_font, MUTED)
    layer.text((bx + BOARD_W / 2, 116), "XIAO ESP32-C6", title_font, TEXT, "ma")
    layer.text((rcx, 150), "LED ring, back", module_font, TEXT, "ma")
    layer.text((fcx, 150), "front", module_font, TEXT, "ma")

    # The wires rise to the pads from below. The ring's pad order means GND has to cross 5V
    # once, whatever the routing; it does so right under the ring.
    lane_l = bx - 20
    lane_r = [br + 20 + k * LANE for k in range(2)]           # GND nearest the board, then 5V
    tmp = Layer(1, 1)
    ring = draw_ring_back(tmp, rcx, rcy)                       # pad positions only
    c_x = (lane_r[1] + ring["GND"][0]) / 2 - 60
    r_x = bx + BOARD_W / 2
    runs = [
        (RED, [pads["5V"], (lane_r[1], pads["5V"][1]), (lane_r[1], rail_5v), (ring["5V"][0], rail_5v), ring["5V"]]),
        (GRAY, [pads["GND"], (lane_r[0], pads["GND"][1]), (lane_r[0], rail_gnd), (ring["GND"][0], rail_gnd), ring["GND"]]),
        (GREEN, [pads["D0"], (lane_l, pads["D0"][1]), (lane_l, rail_din), (ring["DI"][0], rail_din), ring["DI"]]),
        (RED, [(c_x, rail_5v), (c_x, rail_5v + 40)]),
        (GRAY, [(c_x, rail_gnd), (c_x, rail_gnd - 40)]),
    ]
    draw_ring_back(layer, rcx, rcy)
    for colour, pts in runs:
        layer.wire(pts, colour)

    draw_antenna(layer, bx + 50, ant_y, (bx + 84, bottom - 46))
    draw_board(layer, bx, gap, gap)
    draw_ring_front(layer, fcx, 300)
    layer.text((fcx, 412), "8 LEDs, 32 mm", note_font, MUTED, "mm")
    draw_electrolytic(layer, c_x, rail_5v + 20, rail_gnd - 20)
    draw_resistor(layer, r_x - 60, r_x + 60, rail_din, "R1", "330 Ω")

    for colour, pts in runs[:3]:
        layer.end(pts[0], colour)
        layer.end(pts[-1], colour)
    layer.circle((c_x, rail_5v), 8, RED)
    layer.circle((c_x, rail_gnd), 8, GRAY)
    layer.text((ring["DO"][0] - 24, rcy + 200), "DO stays free", note_font, MUTED, "rm")

    # Legend, under the front view.
    legend = [(RED, "5V (USB)"), (GRAY, "GND"), (GREEN, "Data (D0 to DI)")]
    lx, ly = rcx + 210, 470
    layer.rect((lx, ly, W - 40, ly + 78 + len(legend) * 42), 14, PANEL, PANEL_EDGE, 2)
    layer.text((lx + 24, ly + 16), "Wires", module_font)
    for i, (colour, label) in enumerate(legend):
        yy = ly + 78 + i * 42
        layer.line([(lx + 24, yy), (lx + 60, yy)], colour, 9)
        layer.text((lx + 74, yy + 1), label, legend_font, TEXT, "lm")

    layer.text((40, H - 104), "The drawings are simplified and show only the pins you connect. The ring is shown from the back,", note_font, MUTED)
    layer.text((40, H - 72), "pads in the order printed on it. Put R1 and C1 close to the ring. Power comes from USB through the 5V pin.",
               note_font, MUTED)
    layer.save(OUT)
    print(OUT, W, H)


if __name__ == "__main__":
    main()
