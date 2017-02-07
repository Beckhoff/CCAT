#!/bin/sh -l
set -e
insmod ./ccat.ko
dmesg | grep ccat
