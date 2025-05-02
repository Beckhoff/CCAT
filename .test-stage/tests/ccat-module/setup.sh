#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

set -e
set -u

readonly test_device="${1?Missing <test-device>}"
readonly ssh_cmd="ssh Administrator@${test_device}"

family="$(rackctl-config get test-device family)"
readonly family
case "${family}" in
	*)
		# Stop TwinCAT and unload the vfio-pci driver, so we can use
		# our driver instead.
		${ssh_cmd} /bin/sh -eu <<- EOF
			sudo systemctl stop TcSystemServiceUm
			sudo rmmod vfio-pci
		EOF
		;;
esac
