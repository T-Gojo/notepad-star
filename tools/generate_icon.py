"""Generate original Notepad Star vector/raster branding from shared geometry.

Requires Pillow. The wordmark uses original geometric letterforms, not an
embedded or redistributed font. Edit this file to regenerate the complete kit.
"""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1] / "resources"
ICONS = ROOT / "icons"
BRAND = ROOT / "brand"
INK = "#15243f"
BLUE = "#4968ed"
MINT = "#74edda"
PAPER = "#f1f5ff"

NOTE = [
    ("M", 58, 196), ("L", 58, 76), ("Q", 58, 58, 76, 58),
    ("L", 94, 58), ("L", 158, 143), ("L", 158, 107),
    ("L", 188, 107), ("L", 188, 178), ("Q", 188, 196, 170, 196),
    ("L", 156, 196), ("L", 88, 108), ("L", 88, 196), ("Z",),
]
FOLD = [("M", 94, 58), ("L", 116, 88), ("L", 88, 88), ("L", 88, 58), ("Z",)]
STAR = [
    ("M", 183, 24), ("C", 188, 48, 197, 56, 220, 62),
    ("C", 197, 67, 188, 76, 183, 100), ("C", 178, 76, 169, 67, 146, 62),
    ("C", 169, 56, 178, 48, 183, 24), ("Z",),
]
LETTERS = {
    "N": [("M", 0, 44), ("L", 0, 0), ("L", 30, 44), ("L", 30, 0)],
    "O": [("M", 15, 0), ("C", 4, 0, 0, 7, 0, 16), ("L", 0, 28),
          ("C", 0, 38, 4, 44, 15, 44), ("C", 26, 44, 30, 38, 30, 28),
          ("L", 30, 16), ("C", 30, 7, 26, 0, 15, 0), ("Z",)],
    "T": [("M", 0, 0), ("L", 30, 0), ("M", 15, 0), ("L", 15, 44)],
    "E": [("M", 28, 0), ("L", 0, 0), ("L", 0, 44), ("L", 28, 44),
          ("M", 0, 22), ("L", 23, 22)],
    "P": [("M", 0, 44), ("L", 0, 0), ("L", 16, 0),
          ("C", 34, 0, 34, 23, 16, 23), ("L", 0, 23)],
    "A": [("M", 0, 44), ("L", 15, 0), ("L", 30, 44),
          ("M", 6, 29), ("L", 24, 29)],
    "D": [("M", 0, 0), ("L", 0, 44), ("L", 12, 44),
          ("C", 36, 44, 36, 0, 12, 0), ("Z",)],
    "S": [("M", 29, 5), ("C", 19, -4, 1, -1, 1, 11),
          ("C", 1, 26, 30, 17, 30, 32), ("C", 30, 47, 10, 48, 0, 39)],
    "R": [("M", 0, 44), ("L", 0, 0), ("L", 16, 0),
          ("C", 34, 0, 34, 23, 16, 23), ("L", 0, 23),
          ("M", 15, 23), ("L", 31, 44)],
}


def svg_path(commands):
    return " ".join(command[0] + " ".join(str(value) for value in command[1:]) for command in commands)


def contours(commands):
    result = []
    points = []
    current = (0.0, 0.0)
    for command in commands:
        kind, *values = command
        if kind == "M":
            if points:
                result.append(points)
            current = tuple(values)
            points = [current]
        elif kind == "L":
            current = tuple(values)
            points.append(current)
        elif kind in ("Q", "C"):
            start = current
            controls = [tuple(values[index:index + 2]) for index in range(0, len(values), 2)]
            for index in range(1, 97):
                t = index / 96
                u = 1 - t
                if kind == "Q":
                    point = tuple(u * u * start[axis] + 2 * u * t * controls[0][axis] + t * t * controls[1][axis]
                                  for axis in range(2))
                else:
                    point = tuple(u ** 3 * start[axis] + 3 * u * u * t * controls[0][axis] +
                                  3 * u * t * t * controls[1][axis] + t ** 3 * controls[2][axis]
                                  for axis in range(2))
                points.append(point)
            current = controls[-1]
        elif kind == "Z":
            points.append(points[0])
            current = points[0]
        else:
            raise ValueError(f"Unsupported design command: {kind}")
    if points:
        result.append(points)
    return result


def paint_path(draw, commands, color, scale, offset=(0, 0), stroke=None):
    for contour in contours(commands):
        points = [((point[0] + offset[0]) * scale, (point[1] + offset[1]) * scale) for point in contour]
        if stroke is None:
            draw.polygon(points, fill=color)
        else:
            width = round(stroke * scale)
            draw.line(points, fill=color, width=width, joint="curve")
            radius = width / 2
            for x, y in points:
                draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)


def symbol_svg(light=False, mono=False):
    note = PAPER if light else INK
    star = note if mono else MINT if light else BLUE
    folded = note if mono else "#8ca5ff" if light else "#7187e8"
    return "".join(f'<path d="{svg_path(shape)}" fill="{color}"/>'
                   for shape, color in ((NOTE, note), (FOLD, folded), (STAR, star)))


def svg_document(width, height, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-label="{escape(title)}">'
            f'<title>{escape(title)}</title>{body}</svg>\n')


def symbol_image(size, light=False, mono=False):
    scale = 8
    image = Image.new("RGBA", (256 * scale, 256 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    note = PAPER if light else INK
    star = note if mono else MINT if light else BLUE
    folded = note if mono else "#8ca5ff" if light else "#7187e8"
    for shape, color in ((NOTE, note), (FOLD, folded), (STAR, star)):
        paint_path(draw, shape, color, scale)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def app_icon():
    scale = 8
    canvas = Image.new("RGBA", (256 * scale, 256 * scale), (0, 0, 0, 0))
    gradient = Image.new("RGBA", canvas.size)
    draw = ImageDraw.Draw(gradient)
    for y in range(gradient.height):
        t = min(1, max(0, (y / scale - 8) / 240))
        color = tuple(round(a + (b - a) * t) for a, b in zip((27, 43, 74), (10, 18, 39))) + (255,)
        draw.line((0, y, gradient.width, y), fill=color)
    mask = Image.new("L", canvas.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((8 * scale, 8 * scale, 248 * scale, 248 * scale), 56 * scale, fill=255)
    canvas.paste(gradient, (0, 0), mask)
    draw = ImageDraw.Draw(canvas)
    draw.rounded_rectangle((8 * scale, 8 * scale, 248 * scale, 248 * scale), 56 * scale,
                           outline="#34476a", width=scale)
    for shape, color in ((NOTE, PAPER), (FOLD, "#8ca5ff"), (STAR, MINT)):
        paint_path(draw, shape, color, scale)
    return canvas.resize((1024, 1024), Image.Resampling.LANCZOS)


def wordmark(light=False):
    scale = 4
    image = Image.new("RGBA", (680 * scale, 168 * scale), (0, 0, 0, 0))
    image.alpha_composite(symbol_image(128 * scale, light=light), (20 * scale, 20 * scale))
    draw = ImageDraw.Draw(image)
    paths = [f'<g transform="translate(20 20) scale(.5)">{symbol_svg(light)}</g>']
    x = 182
    for word, color in (("NOTEPAD", PAPER if light else INK), ("STAR", MINT if light else BLUE)):
        for letter in word:
            commands = LETTERS[letter]
            paint_path(draw, commands, color, scale, (x, 61), 6.5)
            paths.append(f'<path transform="translate({x} 61)" d="{svg_path(commands)}" '
                         f'fill="none" stroke="{color}" stroke-width="6.5" stroke-linecap="round" stroke-linejoin="round"/>')
            x += 40
        x += 14
    return image.resize((1360, 336), Image.Resampling.LANCZOS), svg_document(680, 168, "".join(paths), "Notepad Star")


def main():
    ICONS.mkdir(parents=True, exist_ok=True)
    BRAND.mkdir(parents=True, exist_ok=True)
    icon = app_icon()
    icon.resize((256, 256), Image.Resampling.LANCZOS).save(ICONS / "notepad-star.png")
    icon.save(ICONS / "notepad-star-1024.png")
    icon.save(ICONS / "notepad-star.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    icon.save(ICONS / "notepad-star.icns")
    background = ('<defs><linearGradient id="tile" x2="0" y2="1">'
                  '<stop stop-color="#1b2b4a"/><stop offset="1" stop-color="#0a1227"/>'
                  '</linearGradient></defs><rect x="8" y="8" width="240" height="240" rx="56" '
                  'fill="url(#tile)" stroke="#34476a"/>')
    (ICONS / "notepad-star.svg").write_text(svg_document(256, 256, background + symbol_svg(True), "Notepad Star app icon"), encoding="utf-8")
    for name, light, mono in (("symbol", False, False), ("symbol-light", True, False), ("monochrome", False, True)):
        (BRAND / f"notepad-star-{name}.svg").write_text(svg_document(256, 256, symbol_svg(light, mono), "Notepad Star symbol"), encoding="utf-8")
        symbol_image(1024, light, mono).save(BRAND / f"notepad-star-{name}.png")
    word_light, svg_light = wordmark()
    word_dark, svg_dark = wordmark(True)
    word_light.save(BRAND / "notepad-star-wordmark.png")
    word_dark.save(BRAND / "notepad-star-wordmark-dark.png")
    (BRAND / "notepad-star-wordmark.svg").write_text(svg_light, encoding="utf-8")
    (BRAND / "notepad-star-wordmark-dark.svg").write_text(svg_dark, encoding="utf-8")
    board = Image.new("RGB", (1600, 980), "#e9eef6")
    draw = ImageDraw.Draw(board)
    heading = ImageFont.load_default(size=30)
    small = ImageFont.load_default(size=22)
    draw.rounded_rectangle((40, 40, 1560, 410), 28, fill="white")
    board.paste(word_light, (120, 38), word_light)
    draw.text((100, 333), "A folded N. A north-star spark.", fill=INK, font=heading)
    draw.rounded_rectangle((40, 450, 1000, 940), 28, fill="#0f1b33")
    dark = word_dark.resize((900, 222), Image.Resampling.LANCZOS)
    board.paste(dark, (60, 495), dark)
    draw.text((100, 790), "Original vector letterforms", fill="#a6b5d2", font=small)
    draw.text((100, 833), "Midnight / Signal Blue / North Mint", fill="#a6b5d2", font=small)
    draw.rounded_rectangle((1030, 450, 1560, 940), 28, fill="white")
    preview_icon = icon.resize((256, 256), Image.Resampling.LANCZOS)
    board.paste(preview_icon, (1165, 480), preview_icon)
    x = 1090
    for size in (16, 24, 32, 48, 64):
        thumb = icon.resize((size, size), Image.Resampling.LANCZOS)
        board.paste(thumb, (x, 810 - size), thumb)
        draw.text((x, 830), str(size), font=small, fill=INK)
        x += 86
    board.save(BRAND / "notepad-star-brand-preview.png")


if __name__ == "__main__":
    main()
