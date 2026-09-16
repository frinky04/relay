"""Rasterize assets/relay.svg into assets/relay.ico, one crisp render per size.

Requires: pip install resvg-py pillow
"""
import io
import pathlib

import resvg_py
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
SVG = ROOT / "assets" / "relay.svg"
ICO = ROOT / "assets" / "relay.ico"
# Largest first: Pillow drops sizes above the base frame.
SIZES = [256, 128, 64, 48, 40, 32, 24, 20, 16]

frames = [
    Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_path=str(SVG), width=s, height=s)))).convert("RGBA")
    for s in SIZES
]
frames[0].save(ICO, format="ICO", sizes=[(s, s) for s in SIZES], append_images=frames[1:])
print(ICO)
