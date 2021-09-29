#!/bin/bash -l

set -e

./unittest/test-gpio.sh
./unittest/test-systemtime.sh
./unittest/test-rw_cdev.sh sram 131072
./unittest/test-update.sh
echo "All tests completed"
