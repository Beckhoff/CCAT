#!/bin/bash -l

set -e

if [ $# -ne 2 ]; then
	echo "Usage: $0 <local_ip> <server_ip>"
	exit -1
fi

./unittest/test-gpio.sh
./unittest/test-network.sh "$1" "$2"
./unittest/test-systemtime.sh
./unittest/test-rw_cdev.sh sram 131072
./unittest/test-update.sh
echo "All tests completed"
