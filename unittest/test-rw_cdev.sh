#!/bin/bash

set -e

if [ $# -lt 2 ]; then
	echo -e "Usage: $0 <dev_type> <dev_size> [readonly]\n f.e.: $0 sram 2048\n"
	exit -1
fi

script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
interface=$(dmesg | tac | grep -m1 "ccat_netdev: registered .* as network device\.$" | awk {'printf $(NF-3)'})
mac=$(cat /sys/class/net/${interface}/address | sed -r 's/:/-/g')

dev_type=$1
dev_size=$2
readonly=$3
dev_count=$(ls /dev/ccat_${dev_type}* | wc -l)

for ((dev_slot = 0; dev_slot < ${dev_count}; dev_slot++)); do

	dev_file=/dev/ccat_${dev_type}${dev_slot}
	ref_file=${script_path}/${dev_type}${dev_slot}.bin.ref-${mac}
	pattern[0]=${script_path}/${dev_type}_AA.bin
	pattern[1]=${script_path}/${dev_type}_55.bin
	tmp_file=.test.tmp~
	tmp_file_ref=.test_ref.tmp~

	run_test() {
		echo "test: $@"
		dd if=$2 of=${tmp_file_ref} "${@:3}"
		if [ "$1" = "write" ]; then
			dd if=$2 of=${dev_file} "${@:3}"
		fi
		dd if=${dev_file} of=${tmp_file} "${@:3}"
		diff -s ${tmp_file} ${tmp_file_ref}
		rm -f ${tmp_file} ${tmp_file_ref}
		echo
	}

	echo "$0: simple read"
	run_test "read" ${ref_file}

	echo "$0: test different blocksizes"
	for i in 1 2 3 4 5 6 7 8 16 32 64 128 256 512 1024 2048 4096; do
		run_test "read" ${ref_file} bs=$i
	done

	echo "$0: test different countsizes"
	for i in 1 2 4 8 16 32 64 128 256 512; do
		run_test "read" ${ref_file} count=$i bs=8
	done

	echo "$0: test different skip"
	for i in 1 2 4 8 16 32 64 128 256; do
		run_test "read" ${ref_file} skip=$i bs=8
	done

	if [ "x${readonly}y" == "xreadonlyy" ]; then
		echo "[readonly] -> skip write tests"
		exit 0
	fi

	# before running write tests, install cleanup trap to try to
	# restore device content on failure.
	cleanup() {
		echo "$0: cleaning up..."
		run_test "write" ${ref_file}
	}
	trap cleanup INT TERM EXIT

	echo "$0: test write patterns AA and 55"
	for i in 1 2; do
		use_pattern=${pattern[$(($i % 2))]}
		run_test "write" ${use_pattern} status=progress
	done

	echo "$0: test write with different blocksizes"
	for i in 1 2 3 4 5 6 7 8 16 32 64 128 256 512 1024 2048 4096; do
		use_pattern=${pattern[$(($i % 2))]}
		run_test "write" ${use_pattern} bs=$i status=progress
	done

	echo "$0: test write with different count"
	for i in 1 2 4 8 16; do
		use_pattern=${pattern[$(($i % 2))]}
		run_test "write" ${use_pattern} count=$i bs=7 status=progress
	done

	echo "$0: test write with different seek"
	for i in 1 2 3 4 5 6 7; do
		use_pattern=${pattern[$(($i % 2))]}
		dd if=${use_pattern} of=${dev_file} seek=$i count=1 bs=7 status=progress
		run_test "read" ${use_pattern} skip=$i count=1 bs=7
	done
	cleanup
done
