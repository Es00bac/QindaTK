# SPDX-License-Identifier: LGPL-3.0-or-later
"""Regenerate the local XYZ fixture; no network maps or third-party imagery."""
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).parent
out = root / "tiles"
out.mkdir(exist_ok=True)
tiles = []
for ty in range(192, 197):
    for tx in range(104, 109):
        image = Image.new("RGB", (256, 256), "#15232c")
        draw = ImageDraw.Draw(image)
        ox, oy = tx * 256, ty * 256
        for py in range(256):
            world_y = oy + py
            ridge = 27000 + 280 * math.sin(world_y / 170) + 95 * math.sin(world_y / 47)
            if ox < ridge:
                draw.line((0, py, min(255, int(ridge - ox)), py), fill="#203b43")
        for step in range(-5, 7):
            pts = []
            for py in range(-20, 278, 5):
                wy = oy + py
                wx = 26850 + step * 62 + 260 * math.sin(wy / 170) + 80 * math.sin(wy / 45)
                pts.append((int(wx - ox), py))
            draw.line(pts, fill="#34535b", width=1)
        for world_x in range(26500, 28100, 116):
            lx = world_x - ox
            draw.line((lx, 0, lx, 256), fill="#253842", width=2)
        for world_y in range(48800, 50600, 116):
            ly = world_y - oy
            draw.line((0, ly, 256, ly), fill="#253842", width=2)
        river = [(int(27200 + 70 * math.sin(wy / 120) - ox), wy - oy)
                 for wy in range(oy - 20, oy + 278, 5)]
        draw.line(river, fill="#186176", width=7)
        draw.line(river, fill="#23819a", width=3)
        interstate = [(int(27135 + 18 * math.sin(wy / 90) - ox), wy - oy)
                      for wy in range(oy - 20, oy + 278, 5)]
        draw.line(interstate, fill="#546d70", width=8)
        draw.line(interstate, fill="#c09568", width=3)
        highway = [(wx - ox, int(49745 + 14 * math.sin(wx / 145) - oy))
                   for wx in range(ox - 20, ox + 278, 5)]
        draw.line(highway, fill="#556d70", width=7)
        draw.line(highway, fill="#bb9a70", width=3)
        for wx, wy, name in [(27313, 49765, "DENVER"), (27294, 49692, "BOULDER"),
                             (27405, 49515, "FRONT RANGE")]:
            px, py = wx - ox, wy - oy
            if 5 < px < 190 and 10 < py < 245:
                draw.ellipse((px - 3, py - 3, px + 3, py + 3), fill="#d2bc8f")
                draw.text((px + 8, py - 6), name, fill="#c9d7d2")
        path = out / f"{tx}-{ty}.png"
        image.save(path, optimize=True)
        tiles.append({"z": 9, "x": tx, "y": ty, "file": f"tiles/{path.name}"})
manifest = {"tileSize": 256, "minZoom": 9, "maxZoom": 9,
            "attribution": "QindaTK local illustrated basemap fixture", "tiles": tiles}
(root / "tiles.json").write_text(json.dumps(manifest, indent=2) + "\n")
