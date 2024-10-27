# SPDX-FileCopyrightText: 2019 Scott Shawcroft for Adafruit Industries
#
# SPDX-License-Identifier: MIT

from __future__ import annotations

"""
`adafruit_bitmap_font.bitmap_font`
====================================================

Loads bitmap glyphs from a variety of font.

* Author(s): Scott Shawcroft

Implementation Notes
--------------------

**Hardware:**

**Software and Dependencies:**

* Adafruit CircuitPython firmware for the supported boards:
  https://github.com/adafruit/circuitpython/releases

"""


__version__ = "0.0.0+auto.0"
__repo__ = "https://github.com/adafruit/Adafruit_CircuitPython_Bitmap_Font.git"


def load_font(filename: str, bitmap_class=None):
    """Loads a font file. Returns None if unsupported."""
    # pylint: disable=import-outside-toplevel, redefined-outer-name, consider-using-with
    if not bitmap_class:
        from . import Bitmap

        bitmap_class = Bitmap
    font_file = open(filename, "rb")
    first_four = font_file.read(4)
    if filename.endswith("bdf") and first_four == b"STAR":
        from . import bdf

        return bdf.BDF(font_file, bitmap_class)

    raise ValueError("Unknown magic number %r" % first_four)
