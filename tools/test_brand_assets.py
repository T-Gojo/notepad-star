"""Validate the generated brand kit and platform icon containers."""
from pathlib import Path
import struct
import unittest
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[1] / "resources"


class BrandAssets(unittest.TestCase):
    def test_vectors_are_self_contained_and_have_accessible_names(self):
        paths = list((ROOT / "brand").glob("*.svg")) + list((ROOT / "icons").glob("*.svg"))
        self.assertEqual(len(paths), 6)
        for path in paths:
            element = ET.parse(path).getroot()
            self.assertTrue(element.attrib["aria-label"])
            self.assertEqual(element.tag, "{http://www.w3.org/2000/svg}svg")
            self.assertIsNone(element.find(".//{http://www.w3.org/2000/svg}text"))
            self.assertIsNone(element.find(".//{http://www.w3.org/2000/svg}image"))

    def test_raster_exports_are_large_enough_and_transparent(self):
        for name in ("symbol", "symbol-light", "monochrome", "wordmark", "wordmark-dark"):
            with Image.open(ROOT / "brand" / f"notepad-star-{name}.png") as image:
                self.assertEqual(image.mode, "RGBA")
                self.assertGreaterEqual(image.width, 1024)
                self.assertEqual(image.getpixel((0, 0))[3], 0)
                self.assertIsNotNone(image.getbbox())
        with Image.open(ROOT / "icons" / "notepad-star.png") as image:
            self.assertEqual(image.size, (256, 256))
            self.assertEqual(image.getpixel((0, 0))[3], 0)
            self.assertEqual(image.getpixel((128, 128))[3], 255)

    def test_windows_icon_contains_all_required_sizes(self):
        with Image.open(ROOT / "icons" / "notepad-star.ico") as image:
            expected = {(size, size) for size in (16, 24, 32, 48, 64, 128, 256)}
            self.assertEqual(image.ico.sizes(), expected)
            for size in expected:
                self.assertEqual(image.ico.getimage(size).size, size)

    def test_macos_icon_and_preview_are_valid(self):
        icon = (ROOT / "icons" / "notepad-star.icns").read_bytes()
        self.assertEqual(icon[:4], b"icns")
        self.assertEqual(struct.unpack(">I", icon[4:8])[0], len(icon))
        with Image.open(ROOT / "icons" / "notepad-star.icns") as image:
            self.assertEqual(image.size, (1024, 1024))
        with Image.open(ROOT / "brand" / "notepad-star-brand-preview.png") as image:
            self.assertEqual(image.size, (1600, 980))


if __name__ == "__main__":
    unittest.main()
