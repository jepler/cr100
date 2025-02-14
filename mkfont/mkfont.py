import array
import sys

from adafruit_bitmap_font import bitmap_font, Bitmap


def extract_deposit_bits(*positions):
    data_out = 0
    for p in positions:
        if p[0]:
            for dest in p[1:]:
                data_out |= 1 << dest
    return data_out


class OffsetBitmap:
    def __init__(self, dx, dy, glyph):
        self.dx = dx
        self.dy = dy
        self.glyph = glyph

    def __getitem__(self, pos):
        x, y = pos
        x = x - self.glyph.dx
        y = y - (7 - self.glyph.height) + self.glyph.dy

        if 0 <= x < self.glyph.bitmap.width and 0 <= y < self.glyph.bitmap.height:
            return self.glyph.bitmap[x, y]
        return 0


CHAR_COUNT = 512


def main(bdf, header):
    font = bitmap_font.load_font(bdf, Bitmap)
    width, height, dx, dy = font.get_bounding_box()

    #    if width != 5 or height != 9:
    #        raise SystemExit("sorry, only 5x9 monospace fonts supported")

    output_data = array.array("H", [0] * 9 * CHAR_COUNT)

    font.load_glyphs(range(CHAR_COUNT))

    for i in range(CHAR_COUNT):
        g = font.get_glyph(i)
        if g is None:
            continue
        bitmap = OffsetBitmap(dx, dy, g)
        for j in range(9):
            d = extract_deposit_bits(
                (bitmap[4, j], 0, 1),
                (bitmap[3, j], 2, 3),
                (bitmap[2, j], 4, 5),
                (bitmap[1, j], 6, 7),
                (bitmap[0, j], 8, 9),
            )
            output_data[j * CHAR_COUNT + i] = d << 2
    for x in output_data:
        print(f"0x{x:04x},", file=header)


if __name__ == "__main__":
    main(sys.argv[1], open(sys.argv[2], "w", encoding="utf-8"))
