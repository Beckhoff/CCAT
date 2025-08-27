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
	CX52x0)
		# On CX52x0 it is not enough to unload vfio-pci to be able to
		# bind the standalone ccat driver. Instead we have to disable
		# both TcSysConf and the systemservice and restart.
		${ssh_cmd} /bin/sh -eu <<- EOF
			sudo systemctl disable TcSystemServiceUm
			sudo systemctl disable TcSysConf
		EOF
		ssh_reboot "Administrator@${test_device}"
		;;
	*)
		# Stop TwinCAT and unload the vfio-pci driver, so we can use
		# our driver instead.
		${ssh_cmd} /bin/sh -eu <<- EOF
			sudo systemctl stop TcSystemServiceUm
			# Sometimes (e.g.: with BHF_CI_LOOP=y) the driver was
			# already unloaded.
			if lsmod | grep vfio-pci; then
				sudo rmmod vfio-pci
			fi
		EOF
		;;
esac
