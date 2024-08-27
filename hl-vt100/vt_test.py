import hl_vt100

print("Starting python test...")
vt100 = hl_vt100.vt100_headless()
vt100.fork("/usr/bin/top", ["/usr/bin/top", "-n", "1"])
vt100.main_loop()
print(*vt100.getlines(), sep="\n")


print()
for a in vt100.getattrlines():
    s = " ".join(f"{c >> 8:02x}" for c in a[:20])
    print(s)
