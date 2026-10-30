"""Draw the app icon.

Requires Pillow (pip install pillow). Usage:
    python3 tools/gen_icon.py <design a|b|c|d> resources/windows/app.ico [preview.png]
Writes the Windows .ico (16-256 px). With a third argument, also writes a
preview sheet of all designs at 256 / 48 / 32 / 16 px.
The app uses design b; src/ui/AppIcon.h draws the same picture for Linux and
macOS, so change both together.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

N = 1024
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

GREEN = (38, 194, 110)
RED = (232, 72, 72)
WHITE = (245, 247, 250)


def base(bg_top, bg_bot):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    grad = Image.new("RGBA", (N, N))
    gd = ImageDraw.Draw(grad)
    for y in range(N):
        t = y / (N - 1)
        c = tuple(int(bg_top[i] + (bg_bot[i] - bg_top[i]) * t) for i in range(3))
        gd.line([(0, y), (N, y)], fill=c + (255,))
    mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(mask).rounded_rectangle([24, 24, N - 24, N - 24], radius=200, fill=255)
    img.paste(grad, (0, 0), mask)
    return img


def text_center(d, txt, size, cy, fill, cx=N // 2):
    f = ImageFont.truetype(BOLD, size)
    l, t, r, b = d.textbbox((0, 0), txt, font=f)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), txt, font=f, fill=fill)


def candle(d, x, w, lo, hi, o, c, col):
    d.line([(x, hi), (x, lo)], fill=col, width=int(w * 0.28))
    top, bot = min(o, c), max(o, c)
    d.rounded_rectangle([x - w / 2, top, x + w / 2, bot], radius=w * 0.12, fill=col)


def design_a():
    """IB monogram, navy, green rising candle under the letters."""
    img = base((22, 40, 74), (10, 20, 40))
    d = ImageDraw.Draw(img)
    text_center(d, "IB", 560, 450, WHITE)
    d.rounded_rectangle([200, 770, 824, 800], radius=15, fill=GREEN)
    return img


def design_b():
    """Three candlesticks on charcoal."""
    img = base((40, 44, 54), (18, 20, 26))
    d = ImageDraw.Draw(img)
    candle(d, 300, 150, 760, 380, 700, 470, GREEN)
    candle(d, 512, 150, 660, 300, 360, 600, RED)
    candle(d, 724, 150, 600, 190, 560, 250, GREEN)
    return img


def design_c():
    """IB monogram with a rising chart line across it."""
    img = base((16, 60, 96), (8, 28, 52))
    d = ImageDraw.Draw(img)
    text_center(d, "IB", 520, 470, WHITE)
    pts = [(150, 820), (360, 700), (520, 760), (700, 560), (880, 420)]
    d.line(pts, fill=GREEN, width=56, joint="curve")
    r = 46
    x, y = pts[-1]
    d.ellipse([x - r, y - r, x + r, y + r], fill=GREEN)
    return img


def design_d():
    """IB on a deep red square with a white bar chart."""
    img = base((196, 40, 48), (128, 18, 26))
    d = ImageDraw.Draw(img)
    text_center(d, "IB", 470, 380, WHITE)
    bars = [(250, 120), (390, 200), (530, 160), (670, 260)]
    for x, h in bars:
        d.rounded_rectangle([x, 880 - h, x + 104, 880], radius=16, fill=WHITE + (235,))
    return img


designs = {"a": design_a, "b": design_b, "c": design_c, "d": design_d}


def preview(path):
    cell = 300
    sheet = Image.new("RGBA", (cell * len(designs), 420), (50, 50, 56, 255))
    sd = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype(BOLD, 28)
    for i, (k, make) in enumerate(designs.items()):
        img = make()
        x0 = i * cell
        sheet.alpha_composite(img.resize((256, 256), Image.LANCZOS), (x0 + 22, 20))
        sx = x0 + 22
        for s in (48, 32, 16):
            sheet.alpha_composite(img.resize((s, s), Image.LANCZOS), (sx, 300 + (48 - s)))
            sx += s + 18
        sd.text((x0 + 22, 370), k.upper(), font=lab, fill=WHITE)
    sheet.save(path)


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in designs:
        sys.exit(__doc__)
    img = designs[sys.argv[1]]()
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    if len(sys.argv) > 3:
        preview(sys.argv[3])
