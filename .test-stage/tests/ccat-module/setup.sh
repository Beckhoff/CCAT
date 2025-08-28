#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

set -e
set -u

readonly test_device="${1?Missing <test-device>}"
readonly ssh_cmd="ssh Administrator@${test_device}"

# Stop TwinCAT and unload the vfio-pci driver, so we can use
# our driver instead.
${ssh_cmd} /bin/sh -eu <<- EOF
	sudo systemctl stop TcSystemServiceUm
	# Sometimes (e.g.: with BHF_CI_LOOP=y) the driver was
	# already unloaded.
	if ! awk '\$1 == "vfio_pci" { exit 1 }' /proc/modules; then
		sudo rmmod vfio_pci
	fi
	# YEAH, this is brutal, but we don't care! TcSysConf "forgets"
	# to cleanup driver_override for our CCAT. So to keep this
	# easy here we just overwrite ALL overrides...
	echo "" | sudo tee /sys/bus/pci/devices/*/driver_override
EOF
