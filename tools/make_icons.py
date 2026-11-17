#!/usr/bin/env python3
"""Draws Tutti's icons: four overlapping circles in the member colours ("all together")."""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "app", "Resources")
COLOURS = [(255, 107, 74), (74, 168, 255), (126, 217, 87), (255, 201, 74)]


def rounded_square(size, radius, fill):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=fill)
    return img


def petals(size, colours, alpha=230):
    layer = Image.new("RGB", (size, size), (0, 0, 0))
    c = size / 2
    r = size * 0.21
    offset = size * 0.13
    for i, col in enumerate(colours):
        dx, dy = [(-1, -1), (1, -1), (1, 1), (-1, 1)][i]
        disc = Image.new("RGB", (size, size), (0, 0, 0))
        ImageDraw.Draw(disc).ellipse([c + dx * offset - r, c + dy * offset - r, c + dx * offset + r, c + dy * offset + r],
                                     fill=col)
        layer = ImageChops.screen(layer, disc)
    mask = layer.convert("L").point(lambda v: 255 if v > 8 else 0).filter(ImageFilter.GaussianBlur(size / 400))
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(layer, (0, 0), mask.point(lambda v: v * alpha // 255))
    return out


def app_icon(size=1024):
    base = rounded_square(size, int(size * 0.22), (21, 22, 26, 255))
    base.alpha_composite(petals(size, COLOURS))
    return base


def template_icon(size=64):
    """macOS menu bar: black shape on transparent (the system tints it)."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c, r, o = size / 2, size * 0.24, size * 0.15
    for dx, dy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        d.ellipse([c + dx * o - r, c + dy * o - r, c + dx * o + r, c + dy * o + r], outline=(0, 0, 0, 255),
                  width=max(2, size // 14))
    return img


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    app_icon().save(os.path.join(OUT, "icon.png"))
    template_icon(64).save(os.path.join(OUT, "tray-template.png"))
    app_icon(64).save(os.path.join(OUT, "tray-color.png"))
    print("icons written to", OUT)
