"""Render ARTBox's original geometric box mark without external graphics tools."""
import sys
sys.dont_write_bytecode = True

import binascii
import json
import os
from pathlib import Path
import struct
import zlib

from environment import ROOT, environment


def inside(x, y, points):
    result = False
    previous = points[-1]
    for current in points:
        ax, ay = previous
        bx, by = current
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            result = not result
        previous = current
    return result


def main():
    os.environ.update(environment())
    # The open box is drawn on a 1024-unit square. iOS supplies the icon mask.
    shapes = [
        ((255, 255, 255), [(236, 388), (482, 506), (482, 794), (236, 670)]),
        ((211, 233, 255), [(542, 506), (788, 388), (788, 670), (542, 794)]),
        ((255, 255, 255), [(224, 326), (456, 214), (512, 330), (280, 444)]),
        ((211, 233, 255), [(512, 330), (568, 214), (800, 326), (744, 444)]),
    ]
    pixels = bytearray()
    for y in range(1024):
        pixels.append(0)  # PNG filter: none
        for x in range(1024):
            sums = [0, 0, 0]
            for dy in (0.25, 0.75):
                for dx in (0.25, 0.75):
                    color = (25, 107, 223)
                    for candidate, polygon in shapes:
                        if inside(x + dx, y + dy, polygon):
                            color = candidate
                    for channel in range(3):
                        sums[channel] += color[channel]
            pixels.extend((value + 2) // 4 for value in sums)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1024, 1024, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b"")
    assets = ROOT / "app/Assets.xcassets"
    icon = assets / "AppIcon.appiconset"
    icon.mkdir(parents=True, exist_ok=True)
    (icon / "ARTBox.png").write_bytes(png)
    (assets / "Contents.json").write_text(json.dumps({"info": {"author": "ARTBox", "version": 1}}, indent=2) + "\n")
    (icon / "Contents.json").write_text(json.dumps({
        "images": [{"filename": "ARTBox.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
        "info": {"author": "ARTBox", "version": 1}}, indent=2) + "\n")
    print("Generated original ARTBox icon")


if __name__ == "__main__":
    main()
