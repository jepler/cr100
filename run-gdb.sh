#!/bin/sh
exec gdb-multiarch --eval-command "target extended-remote :3333" build/cr100.elf "$@"
