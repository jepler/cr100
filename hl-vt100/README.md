# vt100 emulator

`vt100-emulator` is a headless
[vt100](https://fr.wikipedia.org/wiki/VT100) emulator, a bit like any
terminal you may use daily (like urxvt, xterm, ...) but those you're
using are NOT headless, they have a graphical interface to interact
with you, human). Here, `vt100-emulator` is only the underlying a `C`
and `Python` API to an actual emulator, so you can do everything you
want with it, like interfacing over TCP, HTTP, automatically testing
your implementation `malloc` against `top` while running `top` in the
headless terminal, whatever pleases you.

For copyright information, please see the file LICENSE in this
directory or in the files of the source tree.


# INSTALL

## Python module

    pip install hl-vt100


## Python module from source

The simpliest way is just to run `pip install .` from within the repo,
but if you want build artifacts, you can build one in an isolated
environment using:

    pip install build
    python -m build

Or just create an `sdist` the quick way:
n
    python setup.py sdist

In both case it will provide a build artifact in the `dist/` directory
that you can also `pip install`.


# Usage using the Python wrapper (same methods in C)

```python
import hl_vt100


def dump(vt100):
    print("╭" + "─" * vt100.width + "╮")
    for line in vt100.getlines():
        print(f"│{line:{vt100.width}}│")
    print("╰" + "─" * vt100.width + "╯")


def main():
    vt100 = hl_vt100.vt100_headless()
    vt100.changed_callback = lambda: dump(vt100)
    vt100.fork('top', ['top'])
    vt100.main_loop()


if __name__ == '__main__':
    main()
```

# Usage using the C library

```c
#include <stdio.h>
#include <stdlib.h>
#include "hl_vt100.h"


void changed(struct vt100_headless *vt100)
{
    const char **lines;

    lines = vt100_headless_getlines(vt100);
    for (unsigned int y = 0; y < vt100->term->height; ++y)
    {
        write(1, "|", 1);
        write(1, lines[y], vt100->term->width);
        write(1, "|\n", 2);
    }
    write(1, "\n", 1);
}

int main(int ac, char **av)
{
    struct vt100_headless *vt100;
    char *argv[] = {"top", NULL};

    vt100 = new_vt100_headless();
    vt100_headless_fork(vt100, argv[0], argv);
    vt100->changed = changed;
    vt100_headless_main_loop(vt100);
    return EXIT_SUCCESS;
}
```

# Code overview

lw_terminal_parser, lw_terminal_vt100, and hl_vt100 are three modules used to emulate a vt100 terminal:

```
                                  -------------
                                  |           |
                                  | Your Code |
                                  |           |
                                  -------------
                                    |      ^
 vt100 = vt100_headless_init()      |      |
 vt100->changed = changed;          |      | hl_vt100 raises 'changed'
 vt100_headless_fork(vt100, ...     |      | when the screen has changed.
                                    |      | You get the content of the screen
                                    |      | calling vt100_headless_getlines.
                                    V      |
                                  -------------              -------------
 Read from PTY master and write | |           |     PTY      |           |
 to lw_terminal_vt100_read_str  | |  hl_vt100 |<------------>|  Program  |
                                V |           |Master   Slave|           |
                                  -------------              -------------
                                   |        |^ hl_vt100 gets lw_terminal_vt100's
                                   |        || lines by calling
                                   |        || lw_terminal_vt100_getlines
                                   |        ||
                                   |        ||
                                   V        V|
                              ----------------------
 Got data from              | |                    | Recieve data from callbacks
 lw_terminal_vt100_read_str | | lw_terminal_vt100  | And store an in-memory
 give it to                 | |                    | state of the vt100 terminal
 lw_terminal_parser_read_strV ----------------------
                                 |              ^
                                 |              |
                                 |              |
                                 |              |
                                 |              |
                                 |              | Callbacks
                                 |              |
                                 |              |
                                 |              |
                                 |              |
                                 |              |
                                 V              |
                              ----------------------
 Got data from                |                    |
 lw_terminal_pasrser_read_str | lw_terminal_parser |
 parses, and call callbacks   |                    |
                              ----------------------
```

## lw_terminal_parser

`lw_terminal_parser` parses terminal escape sequences, calling callbacks
when a sequence is sucessfully parsed, read `example/parse.c`.

Provides:

 * `struct lw_terminal *lw_terminal_parser_init(void);`
 * `void lw_terminal_parser_destroy(struct lw_terminal* this);`
 * `void lw_terminal_parser_default_unimplemented(struct lw_terminal* this, char *seq, char chr);`
 * `void lw_terminal_parser_read(struct lw_terminal *this, char c);`
 * `void lw_terminal_parser_read_str(struct lw_terminal *this, char *c);`


## lw_terminal_vt100

Hooks into a `lw_terminal_parser` and keep an in-memory state of the
screen of a vt100.

Provides:

 * `struct lw_terminal_vt100 *lw_terminal_vt100_init(void *user_data, void (*unimplemented)(struct lw_terminal* term_emul, char *seq, char chr));`
 * `char lw_terminal_vt100_get(struct lw_terminal_vt100 *vt100, unsigned int x, unsigned int y);`
 * `const char **lw_terminal_vt100_getlines(struct lw_terminal_vt100 *vt100);`
 * `void lw_terminal_vt100_destroy(struct lw_terminal_vt100 *this);`
 * `void lw_terminal_vt100_read_str(struct lw_terminal_vt100 *this, char *buffer);`


## hl_vt100

Forks a program, plug its io to a pseudo terminal and emulate a vt100
using `lw_terminal_vt100`.

Provides:

 * `void vt100_headless_fork(struct vt100_headless *this, const char *progname, char *const argv[]);`
 * `struct vt100_headless *vt100_headless_init(void);`
 * `const char **vt100_headless_getlines(struct vt100_headless *this);`
