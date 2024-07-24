from dataclasses import dataclass

class Bitmap:
    def __init__(self, width, height, color_count):
        self.width = width
        self.height = height
        if color_count > 255:
            raise ValueError("Cannot support that many colors")
        self.values = bytearray(width * height)

    def __setitem__(self, index, value):
        if isinstance(index, tuple):
            index = index[0] + index[1] * self.width
        self.values[index] = value

    def __getitem__(self, index):
        if isinstance(index, tuple):
            print(index[0], index[1], self.width, len(self))
            index = index[0] + index[1] * self.width
            print(index)
        return self.values[index]

    def __len__(self):
        return self.width * self.height

    def __repr__(self):
        return f"<Bitmap {self.width}x{self.height}>"


@dataclass
class Glyph:
    bitmap: Bitmap = None
    unknown: int = 0
    width: int = 0
    height: int = 0
    dx: int = 0
    dy: int = 0
    shift_x: int = 0
    shift_y: int = 0
