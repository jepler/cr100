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
