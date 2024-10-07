import hl_vt100
import sys

vt100 = hl_vt100.vt100_headless()
vt100.fork(sys.argv[1], sys.argv[1:])
vt100.main_loop()
print(*vt100.getlines(), sep="\n")

print()
for a in vt100.getattrlines()[:8]:
    s = " ".join(f"{c >> 8:02x}" for c in a[:20])
    print(s)
