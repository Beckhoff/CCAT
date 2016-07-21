#!/bin/sh -l
set -e
insmod ./ccat.ko
insmod ./ccat_netdev.ko
insmod ./ccat_gpio.ko
insmod ./ccat_sram.ko
insmod ./ccat_systemtime.ko
insmod ./ccat_update.ko
dmesg | grep ccat
