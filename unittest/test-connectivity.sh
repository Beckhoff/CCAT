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

run_iperf_once() {
	ip netns exec ns1 iperf3 \
		--bind-dev=eth0 \
		--client=192.168.1.2 \
		--connect-timeout=1000 \
		--time=10 \
		"$@"
}

run_iperf() {
	local _retries=20

	while ! run_iperf_once "$@"; do
		if test ${_retries} -gt 0; then
			_retries=$((_retries - 1))
			printf 'Waiting for iperf3 server to accept connection...\n' >&2
			if ! pkill -0 --pidfile=/tmp/iperf3.pid; then
				printf 'WARNING: the iperf3 daemon seems dead!\n' >&2
			fi
		else
			printf 'Waiting for iperf3 server failed, giving up!\n' >&2
			exit 1
		fi
	done;
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
