#!/usr/bin/env python

"""
setup.py file for hl_vt100
"""

from pathlib import Path

from setuptools import setup, Extension


hl_vt100_module = Extension('hl_vt100',
                            include_dirs=['src'],
                            define_macros=[('NDEBUG', '1')],
                            sources=['src/vt100_module.c',
                                     'src/hl_vt100.c',
                                     'src/lw_terminal_parser.c',
                                     'src/lw_terminal_vt100.c'])

setup(name='hl_vt100',
      version='0.2',
      url='https://github.com/JulienPalard/vt100-emulator',
      author="Julien Palard",
      author_email='julien@palard.fr',
      description="""Headless vt100 emulator""",
      long_description=(Path(__file__).parent / "README.md").read_text(encoding="UTF-8"),
      long_description_content_type="text/markdown",
      ext_modules=[hl_vt100_module],
      include_package_data=True,
      py_modules=["hl_vt100"],
      classifiers=[
          "Programming Language :: C",
          "Programming Language :: Python",
          "Development Status :: 5 - Production/Stable",
          "License :: OSI Approved :: BSD License",
          "Operating System :: OS Independent",
          "Topic :: System :: Emulators",
          "Topic :: Terminals :: Terminal Emulators/X Terminals"
      ])
