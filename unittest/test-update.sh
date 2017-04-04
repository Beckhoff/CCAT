#!/bin/bash

set -e

script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
interface=$(dmesg | tac | grep -m1 "ccat_netdev: registered .* as network device\.$" | awk {'printf $(NF-3)'})
mac=$(cat /sys/class/net/${interface}/address | sed -r 's/:/-/g')

dev_type=update
dev_count=$(ls /dev/ccat_${dev_type}* | wc -l)

for ((dev_slot = 0; dev_slot < ${dev_count}; dev_slot++)); do
	dev_file=/dev/ccat_${dev_type}${dev_slot}
	ref_file=${script_path}/${dev_type}${dev_slot}.bin.ref-${mac}

	${script_path}/../scripts/update_ccat.sh ${dev_file} ${ref_file}
done
