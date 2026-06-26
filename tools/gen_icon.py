#!/usr/bin/env python
"""
gen_icon.py — Generate VM Manager application icon.

Design: A circular gauge/meter on dark background, inspired by the
web dashboard gauges. Uses the app's signature blue (#58a6ff) and
green (#3fb950) colors with "VM" lettering in the center.

Output: vm_manager.ico (multi-resolution: 16, 32, 48, 256)
         vm_manager.png (source preview)
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os

# ── Color palette (matches the dark dashboard theme) ──────────────
BG      = (13, 17, 23)       # #0d1117  dark background
CARD    = (22, 27, 34)       # #161b22  card background
BORDER  = (48, 54, 61)       # #30363d
ACCENT  = (88, 166, 255)     # #58a6ff  blue accent
GREEN   = (63, 185, 80)      # #3fb950  green
ORANGE  = (255, 159, 50)     # #ff9f32  orange (threshold)
RED     = (248, 81, 73)      # #f85149  red (danger)
MUTED   = (139, 148, 158)    # #8b949e  muted text
TEXT    = (225, 230, 237)    # #e1e6ed  bright text
WHITE   = (255, 255, 255)


def draw_gauge_background(draw, cx, cy, r, bg_color, border_color):
    """Draw the gauge background circle with a subtle border."""
    # Outer glow ring
    draw.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2],
                 fill=None, outline=border_color, width=1)
    # Main background circle
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=bg_color)
    # Inner subtle ring
    draw.ellipse([cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3],
                 fill=None, outline=border_color, width=1)


def draw_gauge_arc(draw, cx, cy, r, pct, color, width):
    """Draw a gauge arc from 135° to 405° (270° sweep), filled to pct%."""
    # Draw the background track (dark)
    steps = 120
    for i in range(steps):
        angle_start = math.radians(135 + i * 270.0 / steps)
        angle_end   = math.radians(135 + (i + 1) * 270.0 / steps)
        x1 = cx + (r - width/2) * math.cos(angle_start)
        y1 = cy - (r - width/2) * math.sin(angle_start)
        x2 = cx + (r - width/2) * math.cos(angle_end)
        y2 = cy - (r - width/2) * math.sin(angle_end)
        x3 = cx + (r + width/2) * math.cos(angle_end)
        y3 = cy - (r + width/2) * math.sin(angle_end)
        x4 = cx + (r + width/2) * math.cos(angle_start)
        y4 = cy - (r + width/2) * math.sin(angle_start)

        # Track color: dark gray
        seg_color = BORDER

        # Active portion: from 135° to 135° + pct * 270°
        seg_angle = 135 + i * 270.0 / steps
        if seg_angle <= 135 + pct * 270.0 / 100.0:
            # Color gradient: blue → green → orange → red based on position
            t = (seg_angle - 135) / 270.0  # 0..1
            if t < 0.5:
                # Blue → Green
                t2 = t / 0.5
                r_c = int(ACCENT[0] + (GREEN[0] - ACCENT[0]) * t2)
                g_c = int(ACCENT[1] + (GREEN[1] - ACCENT[1]) * t2)
                b_c = int(ACCENT[2] + (GREEN[2] - ACCENT[2]) * t2)
            elif t < 0.75:
                # Green → Orange
                t2 = (t - 0.5) / 0.25
                r_c = int(GREEN[0] + (ORANGE[0] - GREEN[0]) * t2)
                g_c = int(GREEN[1] + (ORANGE[1] - GREEN[1]) * t2)
                b_c = int(GREEN[2] + (ORANGE[2] - GREEN[2]) * t2)
            else:
                # Orange → Red
                t2 = (t - 0.75) / 0.25
                r_c = int(ORANGE[0] + (RED[0] - ORANGE[0]) * t2)
                g_c = int(ORANGE[1] + (RED[1] - ORANGE[1]) * t2)
                b_c = int(ORANGE[2] + (RED[2] - ORANGE[2]) * t2)
            seg_color = (r_c, g_c, b_c)

        draw.polygon([(x1, y1), (x2, y2), (x3, y3), (x4, y4)],
                      fill=seg_color)


def draw_gauge_needle(draw, cx, cy, r, pct, color):
    """Draw the gauge needle pointing to pct% position."""
    angle = math.radians(135 + pct * 270.0 / 100.0)
    needle_len = r * 0.65
    tip_x = cx + needle_len * math.cos(angle)
    tip_y = cy - needle_len * math.sin(angle)

    # Needle base circle
    base_r = r * 0.12
    draw.ellipse([cx - base_r, cy - base_r, cx + base_r, cy + base_r],
                 fill=color)

    # Needle line (thick → thin)
    perp_angle = angle + math.pi / 2
    base_w = r * 0.06
    base_x1 = cx + base_w * math.cos(perp_angle)
    base_y1 = cy - base_w * math.sin(perp_angle)
    base_x2 = cx - base_w * math.cos(perp_angle)
    base_y2 = cy + base_w * math.sin(perp_angle)

    draw.polygon([(base_x1, base_y1), (base_x2, base_y2),
                   (tip_x, tip_y)], fill=color, outline=color)


def draw_vm_text(draw, cx, cy, size):
    """Draw 'VM' lettering in the center of the gauge."""
    # We draw simple geometric 'VM' shapes since fonts vary by system
    r = size * 0.22  # half of the text area

    # Letter 'V': two diagonal lines
    v_left  = cx - r * 0.8
    v_right = cx + r * 0.8
    v_top   = cy - r * 0.7
    v_bot   = cy + r * 0.7
    v_mid   = cy + r * 0.7

    # V shape
    draw.line([(v_left, v_top), (cx, v_bot)], fill=WHITE, width=max(1, int(size * 0.04)))
    draw.line([(cx, v_bot), (v_right, v_top)], fill=ACCENT, width=max(1, int(size * 0.04)))

    # M shape (slightly to the right, overlapping V)
    m_left  = cx - r * 0.2
    m_right = cx + r * 0.9
    m_top   = cy - r * 0.6
    m_bot   = cy + r * 0.6
    m_mid_x = cx + r * 0.35
    m_mid_y = cy - r * 0.1

    draw.line([(m_left, m_bot), (m_left, m_top)],  fill=GREEN, width=max(1, int(size * 0.04)))
    draw.line([(m_left, m_top), (m_mid_x, m_mid_y)], fill=GREEN, width=max(1, int(size * 0.04)))
    draw.line([(m_mid_x, m_mid_y), (m_right, m_top)], fill=GREEN, width=max(1, int(size * 0.04)))
    draw.line([(m_right, m_top), (m_right, m_bot)],  fill=GREEN, width=max(1, int(size * 0.04)))


def draw_tick_marks(draw, cx, cy, r, size):
    """Draw tick marks around the gauge."""
    inner_r = r * 0.78
    outer_r = r * 0.88
    major_outer = r * 0.92

    for i in range(0, 101, 5):
        angle = math.radians(135 + i * 270.0 / 100.0)
        is_major = (i % 25 == 0 or i == 100)
        orr = major_outer if is_major else outer_r

        x1 = cx + inner_r * math.cos(angle)
        y1 = cy - inner_r * math.sin(angle)
        x2 = cx + orr * math.cos(angle)
        y2 = cy - orr * math.sin(angle)

        tick_color = MUTED if not is_major else TEXT
        w = max(1, int(size * 0.02)) if is_major else 1
        draw.line([(x1, y1), (x2, y2)], fill=tick_color, width=w)


def draw_pct_text(draw, cx, cy, r, size):
    """Draw percentage label below the gauge."""
    y = cy + r * 0.55
    # Draw "38%" as the gauge reading (aesthetic choice: looks like real data)
    pct_str = "38%"
    # Use simple rectangles to approximate text for the icon
    char_w = r * 0.15
    char_h = r * 0.22
    x_start = cx - char_w * 0.8

    # "3"
    x = x_start
    draw.rectangle([x, y - char_h, x + char_w, y - char_h + 3], fill=ACCENT)   # top bar
    draw.rectangle([x + char_w - 3, y - char_h, x + char_w, y], fill=ACCENT)   # right bar
    draw.rectangle([x, y - char_h/2 - 1, x + char_w, y - char_h/2 + 2], fill=ACCENT)  # middle
    draw.rectangle([x + char_w - 3, y - char_h/2, x + char_w, y], fill=ACCENT)  # right lower
    draw.rectangle([x, y - 3, x + char_w, y], fill=ACCENT)   # bottom

    # "8"
    x = x_start + char_w + 4
    draw.rectangle([x, y - char_h, x + char_w, y - char_h + 3], fill=GREEN)   # top
    draw.rectangle([x, y - 3, x + char_w, y], fill=GREEN)   # bottom
    draw.rectangle([x, y - char_h, x + 3, y], fill=GREEN)   # left
    draw.rectangle([x + char_w - 3, y - char_h, x + char_w, y], fill=GREEN)   # right
    draw.rectangle([x, y - char_h/2 - 1, x + char_w, y - char_h/2 + 2], fill=GREEN)  # middle

    # "%"
    x = x_start + (char_w + 4) * 2
    pct_r = char_h * 0.3
    draw.ellipse([x, y - char_h, x + pct_r * 2, y - char_h + pct_r * 2], fill=None, outline=TEXT, width=2)
    draw.line([(x + pct_r * 2, y - 2), (x + char_w, y - char_h)], fill=TEXT, width=2)


def create_icon(size):
    """Create a single icon frame at the given size."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))

    # Calculate geometry
    margin = size * 0.08
    cx = size / 2
    cy = size / 2
    r = (size - margin * 2) / 2

    draw = ImageDraw.Draw(img)

    # Draw gauge background (dark circle with border)
    draw_gauge_background(draw, cx, cy, r, CARD, BORDER)

    # Draw gauge arc (showing about 38% — "normal operating range")
    pct = 38
    arc_width = r * 0.15
    draw_gauge_arc(draw, cx, cy, r - arc_width * 0.5, pct, ACCENT, arc_width)

    # Draw tick marks (only for larger sizes)
    if size >= 48:
        draw_tick_marks(draw, cx, cy, r - arc_width * 0.5, size)

    # Draw needle at 38%
    if size >= 32:
        draw_gauge_needle(draw, cx, cy, r - arc_width * 0.5, pct, TEXT)

    # Draw "VM" lettering
    if size >= 32:
        draw_vm_text(draw, cx, cy, size)

    # Draw percentage text below center
    if size >= 48:
        draw_pct_text(draw, cx, cy, r, size)

    # For very small sizes, draw a simplified version
    if size <= 16:
        # Simplified: just a colored circle with a dot
        draw.ellipse([margin, margin, size - margin, size - margin],
                     fill=ACCENT, outline=BG, width=max(1, int(size * 0.1)))
        # Inner highlight
        inner_m = size * 0.35
        draw.ellipse([cx - inner_m, cy - inner_m, cx + inner_m, cy + inner_m],
                     fill=GREEN)

    return img


def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src_dir = os.path.join(base_dir, 'src')

    # Generate icon at multiple resolutions
    sizes = [16, 32, 48, 256]
    icons = []
    for s in sizes:
        print(f"  Generating {s}x{s}...")
        icons.append(create_icon(s))

    # Save as .ico
    ico_path = os.path.join(src_dir, 'vm_manager.ico')
    icons[0].save(ico_path, format='ICO', sizes=[(s, s) for s in sizes],
                  append_images=icons[1:])
    print(f"\nIcon saved: {ico_path}")

    # Also save a PNG preview
    png_path = os.path.join(src_dir, 'vm_manager.png')
    icons[-1].save(png_path, format='PNG')  # Save 256x256 as PNG preview
    print(f"Preview: {png_path}")

    # ── Generate resource file (.rc) ─────────────────────────────────
    rc_path = os.path.join(src_dir, 'vm_manager.rc')
    with open(rc_path, 'w', encoding='utf-8') as f:
        f.write('/* VM Manager — Application Icon Resource */\n')
        f.write('MAIN_ICON ICON "vm_manager.ico"\n')
    print(f"Resource: {rc_path}")

    print("\nDone! The icon will be embedded in vm_manager.exe via build.bat.")


if __name__ == '__main__':
    main()
