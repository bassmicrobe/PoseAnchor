"""Convert the supplied SVG into checked-in PNG/Windows ICO assets.

Maintainer-only dependencies: Node.js + sharp, Python + Pillow. Neither the
Windows build nor the installed app needs these tools. Use --check in asset CI.
"""

import argparse
from io import BytesIO
from pathlib import Path
import subprocess

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src" / "status" / "PoseAnchorStatus.ico"
PNG = ROOT / "assets" / "branding" / "poseanchor-icon.png"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node", default="node", help="Node.js executable")
    parser.add_argument("--check", action="store_true", help="Verify without writing")
    args = parser.parse_args()
    # Render the actual SVG paths; do not approximate the logo with new geometry.
    rendered = subprocess.run(
        [args.node, str(ROOT / "scripts" / "render-icon.cjs")],
        check=True, capture_output=True,
    ).stdout
    with Image.open(BytesIO(rendered)) as source:
        image = source.convert("RGBA")
    png = BytesIO()
    image.save(png, format="PNG", optimize=True)
    ico = BytesIO()
    # Use an independently downsampled image for every ICO frame.
    image.save(
        ico, format="ICO", sizes=[(size, size) for size in SIZES],
    )
    for path, data in ((PNG, png.getvalue()), (OUTPUT, ico.getvalue())):
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                raise SystemExit(f"Stale branding asset: {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
