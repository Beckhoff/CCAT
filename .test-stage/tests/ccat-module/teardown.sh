#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

set -e
set -u

readonly test_device="${1?Missing <test-device>}"
readonly ssh_cmd="ssh Administrator@${test_device}"

readonly rte0="$(rackctl-config get test-device rte0/debian)"

uuid="$(cat ioswitch.uuid)"
readonly uuid
rm -f ioswitch.uuid

rackctl-ioswitch release "${uuid}"

${ssh_cmd} /bin/sh -$- <<- EOF
	    # Print some debug information
	    sudo ip netns exec ns1 ip neigh
	    sudo ip netns exec ns2 ip neigh

	    sudo ip netns exec ns1 ip --statistics --statistics link show dev eth0
	    sudo ip netns exec ns2 ip --statistics --statistics link show dev ${rte0}

	    sudo ip netns exec ns1 cat /proc/net/dev
	    sudo ip netns exec ns2 cat /proc/net/dev

	    # Kill the iperf server and remove the network namespaces
	    sudo pkill --pidfile=/tmp/iperf3.pid

	    sudo ip netns del ns1
	    sudo ip netns del ns2
EOF
