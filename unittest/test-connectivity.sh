#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

setup_interface() {
	local _interface="${1}"
	local _ns_number="${2}"

	ip link set "${_interface}" netns "ns${_ns_number}"
	ip netns exec "ns${_ns_number}" ip addr add "192.168.1.${_ns_number}"/24 dev "${_interface}"
	ip netns exec "ns${_ns_number}" ip link set "${_interface}" up
	"${CLEANUP}/add" "ip netns exec ns${_ns_number} ip neigh"
	"${CLEANUP}/add" "ip netns exec ns${_ns_number} ip --statistics --statistics link show dev ${_interface}"
	"${CLEANUP}/add" "ip netns exec ns${_ns_number} cat /proc/net/dev"
}

run_iperf() {
	ip netns exec ns1 iperf3 \
		--bind-dev=eth0 \
		--client=192.168.1.2 \
		--connect-timeout=10000 \
		--time=10 \
		"$@"
}

set -e
set -u

eval "$(cleanup init)"

readonly rte0="${1?Missing <rte0>}"

ip netns add ns1
"${CLEANUP}/add" "ip netns del ns1"

ip netns add ns2
"${CLEANUP}/add" "ip netns del ns2"

setup_interface eth0 1
setup_interface "${rte0}" 2

ip netns exec ns2 iperf3 \
	--bind-dev="${rte0}" \
	--daemon \
	--pidfile=/tmp/iperf3.pid \
	--server
"${CLEANUP}/add" "pkill --pidfile=/tmp/iperf3.pid"

run_iperf
run_iperf --reverse

run_iperf --udp
run_iperf --reverse --udp
