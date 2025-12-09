#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

set -e
set -u

readonly test_device="${1?Missing <test-device>}"
readonly ssh_cmd="ssh Administrator@${test_device}"

readonly rte0="$(rackctl-config get test-device rte0/debian)"

${ssh_cmd} sudo /bin/sh -$- <<- EOF
		# unload ccat so twincat can access it again
		rmmod ccat_update
		rmmod ccat_systemtime
		rmmod ccat_sram
		rmmod ccat_gpio
		rmmod ccat_netdev
		rmmod ccat
		# Turn TwinCAT back on again so the generic_teardown doesn't fail
		systemctl --now enable TcSysConf TcSystemServiceUm
EOF
