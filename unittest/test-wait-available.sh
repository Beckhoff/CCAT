#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

wait_for_devices() {
	local _retries="${1}"

	while test "${_retries}" -gt 0; do
		_retries=$((_retries - 1))
		if ! test -e '/dev/ccat_update0'; then
			printf 'CCAT update device node still missing. Waiting...\n' >&2
			sleep 1
			continue
		fi
		if ! grep 'ccat' '/sys/devices/system/clocksource/clocksource0/available_clocksource'; then
			printf 'CCAT clocksource still missing. Waiting...\n' >&2
			sleep 1
			continue
		fi
		if ! grep --files-with-matches 'ccat_gpio' /sys/class/gpio/gpiochip*/label; then
			printf 'CCAT gpiochip still missing. Waiting...\n' >&2
			sleep 1
			continue
		fi
		# Yeah, eth0 is a pretty weak indicator for a ccat_netdev device. However, it
		# works for, now and other parts of our test are already hardcoded to "eth0"
		# aswell.
		if ! ip link show eth0; then
			printf 'CCAT netdev still missing. Waiting...\n' >&2
			sleep 1
			continue
		fi
		return 0
	done
	printf 'Waiting for CCAT devices timed out!\n' >&2
	exit 127
}

set -e
set -u

wait_for_devices "${1?Missing <timeout_sec>}"
