#!/bin/sh
PATH=/riscv32_lcc/lcc/bin/:$PATH
lcc mcpclock.c -o mcpclock
chmod +x mcpclock
./mcpclock -a

