#!/bin/bash -l

if [ $# -ne 2]; then
	echo "Usage: $0 <local_ip> <server_ip>"
	exit -1
fi

./unittest/test-network.sh "$1" "$2" &&
./unittest/test-systemtime.sh &&
./unittest/test-update.sh &&
echo "All tests completed"
