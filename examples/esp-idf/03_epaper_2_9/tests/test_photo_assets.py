"""Verify committed artwork survives packing/rotation and honest dither labeling."""
import importlib.util
from pathlib import Path
import re
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('render_photo', ROOT / 'tools/render_photo.py')
renderer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)


def include_bytes(name):
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})',
                 (ROOT / 'main' / name).read_text()))


class PhotoAssetsTest(unittest.TestCase):
    def test_native_rotation_and_all_four_levels(self):
        for variant in ('hf', 'lf'):
            image = Image.open(ROOT / f'preview/photo_{variant}_4gray.png')
            self.assertEqual(image.size, (296, 128))
            self.assertEqual(set(image.getdata()), {0, 85, 170, 255})
            raw = include_bytes(f'photo_{variant}_4gray.inc')
            self.assertEqual(len(raw), 9472)
            # Independently invert the board's x'=y, y'=295-x mapping.
            for y in range(128):
                for x in range(296):
                    position = (295 - x) * 128 + y
                    actual = ((raw[position // 4] >> (6-2*(position % 4))) & 3) * 85
                    self.assertEqual(actual, image.getpixel((x, y)))
            self.assertEqual(raw, renderer.pack_gray4(image))

    def test_bw_preview_matches_firmware(self):
        for variant in ('hf', 'lf'):
            image = Image.open(ROOT / f'preview/photo_{variant}.png')
            self.assertEqual(image.mode, '1')
            self.assertEqual(image.size, (296, 128))
            self.assertEqual(image.tobytes(), include_bytes(f'photo_{variant}.inc'))
            # Native label and dither label must not be the same image crop.
            gray = Image.open(ROOT / f'preview/photo_{variant}_4gray.png')
            crop = (8, 109, 76, 124)
            self.assertNotEqual(image.convert('L').crop(crop).tobytes(), gray.crop(crop).tobytes())

    def test_dither_tone_density(self):
        for level, white_count in ((0, 0), (85, 3), (170, 6), (255, 9)):
            tile = renderer.dither(Image.new('L', (3, 3), level))
            self.assertEqual(sum(bool(v) for v in tile.getdata()), white_count)


if __name__ == '__main__':
    unittest.main()
