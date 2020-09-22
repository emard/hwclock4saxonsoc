#!/bin/sh
PATH=/riscv32_lcc/lcc/bin/:$PATH
lcc hwclock.c
chmod +x a.out
./a.out -r

