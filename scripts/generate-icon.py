"""Regenerate the checked-in PoseAnchor Windows icon.

The runtime/build does not depend on Python; this script only keeps the binary
ICO asset reproducible for maintainers.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


SIZE = 1024
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src" / "status" / "PoseAnchorStatus.ico"


def rounded_line(draw: ImageDraw.ImageDraw, points, fill, width):
    draw.line(points, fill=fill, width=width, joint="curve")
    radius = width // 2
    for x, y in (points[0], points[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=fill)


def render() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle((28, 28, 996, 996), radius=220, fill=255)

    background = Image.new("RGBA", (SIZE, SIZE))
    pixels = background.load()
    for y in range(SIZE):
        amount = y / (SIZE - 1)
        left = (7, 20, 42)
        right = (15, 53, 86)
        color = tuple(round(a + (b - a) * amount) for a, b in zip(left, right))
        for x in range(SIZE):
            pixels[x, y] = (*color, 255)
    image.alpha_composite(Image.composite(background, Image.new("RGBA", image.size), mask))

    # A restrained cyan halo helps the white anchor remain legible at 16 px.
    glow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.ellipse((210, 70, 814, 674), outline=(51, 214, 255, 150), width=54)
    glow = glow.filter(ImageFilter.GaussianBlur(42))
    image.alpha_composite(glow)

    draw = ImageDraw.Draw(image)
    cyan = (91, 226, 255, 255)
    white = (246, 251, 255, 255)

    # Lighthouse tracking rays.
    rounded_line(draw, [(340, 222), (190, 134)], cyan, 34)
    rounded_line(draw, [(318, 292), (142, 292)], cyan, 34)
    rounded_line(draw, [(684, 222), (834, 134)], cyan, 34)
    rounded_line(draw, [(706, 292), (882, 292)], cyan, 34)

    # Anchor ring, stem and stock.
    draw.ellipse((421, 116, 603, 298), outline=white, width=52)
    draw.ellipse((477, 172, 547, 242), fill=cyan)
    rounded_line(draw, [(512, 288), (512, 705)], white, 64)
    rounded_line(draw, [(350, 390), (674, 390)], white, 54)

    # Broad lower curve and flukes stay recognizable in the smallest icon sizes.
    draw.arc((238, 388, 786, 872), start=0, end=180, fill=white, width=70)
    draw.polygon([(244, 610), (112, 516), (154, 716)], fill=white)
    draw.polygon([(780, 610), (912, 516), (870, 716)], fill=white)

    return image


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    image = render()
    preview = image.resize((256, 256), Image.Resampling.LANCZOS)
    preview.save(
        OUTPUT,
        format="ICO",
        sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
               (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print(OUTPUT)


if __name__ == "__main__":
    main()
