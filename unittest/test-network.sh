#!/bin/bash -l
set -e

local_ip=$1
remote_ip=$2

echo "searching for ccat net device"
net_id=$(dmesg | grep -oE "ccat.*: registered eth[0-9]+ as network device" | grep -oE "eth[0-9]+")

echo "activating ccat net device"
ip link set dev ${net_id} up
sleep 1
ip addr add ${local_ip} dev ${net_id}
sleep 1

echo "pinging test cx"
ping ${remote_ip} -c 4

retry() {
	local -ri max_attempts=$1
	shift
	local -r cmd="$@"
	local -i attempt=1
	until $cmd; do
		if [ "$attempt" -eq "$max_attempts" ]; then
			return $?
		else
			sleep 10
			attempt=attempt+1
		fi
	done
	return $?
}

echo testing tcp throughput
retry 12 iperf3 -c ${remote_ip}
if [ $? -ne 0 ]; then
	echo "tcp test failed"
	exit
fi

echo testing udp throughput
retry 12 iperf3 -uc ${remote_ip} -b 0
if [ $? -ne 0 ]; then
	echo "udp test failed"
	exit
fi
