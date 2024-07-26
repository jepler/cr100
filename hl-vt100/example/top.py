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
