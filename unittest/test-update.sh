#!/bin/bash

set -e

script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
interface=$(dmesg | tac | grep -m1 "ccat_netdev: registered .* as network device\.$" | awk {'printf $(NF-3)'})
mac=$(cat /sys/class/net/${interface}/address | sed -r 's/:/-/g')

if test "${1:-}" = "--dry-run"; then
	readonly dry_run=true
else
	readonly dry_run=false
fi

dev_type=update
dev_count=$(ls /dev/ccat_${dev_type}* | wc -l)

for ((dev_slot = 0; dev_slot < ${dev_count}; dev_slot++)); do
	dev_file=/dev/ccat_${dev_type}${dev_slot}
	ref_file=${script_path}/${dev_type}${dev_slot}.bin.ref-${mac}

	if "${dry_run}"; then
		diff --brief "${dev_file}" "${ref_file}"
	else
		"${script_path}/../scripts/update_ccat.sh" "${dev_file}" "${ref_file}"
	fi
done
