#!/bin/bash
# Use this script to update your CCAT FPGA configuration in flash.
# You should provide the path to your *.rbf as first parameter to the
# script. As a first step a backup of the current FPGAs flash content is
# read and saved as *.rbf.~ccat_update_backup. Then the contents of *.rbf
# is written to the FPGA and read back. If the compare of the readback
# data and the original file fails, the script tries to restore the
# backup. If that restore fails, too, keep calm and try it manually. As
# long as the FPGA stays powered on you have a chance to recover!
#
# Copyright (C) 2014-2015  Beckhoff Automation GmbH
# Author: Patrick Bruenn <p.bruenn@beckhoff.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

if [ $# -ne 2 ]; then
	echo "Usage: $0 <device> <rbf>"
	exit -1
fi

rbf=$2
tmp=$rbf.~ccat_update_tmp
backup=$rbf.~ccat_update_backup
update=${1}
bytes=$(echo $(wc -c $rbf)|cut -d' ' -f1)

# check if device file is available
if ! [ -c $update ]; then
	echo $update does not exist
	exit 1
fi

# create a backup
cat $update > $backup
if [ $? -ne 0 ]; then
	echo "create CCAT backup failed"
	exit 1
fi

# write *.rbf to CCAT
cat $rbf > $update
if [ $? -ne 0 ]; then
	echo "write to flash failed"
	exit 1
fi

# read CCAT flash content back into a temporary file
cat $update > $tmp
if [ $? -ne 0 ]; then
	echo "read from flash failed"
	exit 1
fi

# since we never know the exact length of a CCATs firmware we trim the
# read file to the length of the original *.rbf and compare them
truncate -s $bytes $tmp
diff $rbf $tmp > /dev/null
if [ $? -eq 0 ]; then
	echo "Update complete"
	rm -f $tmp $backup
else
	echo "Update failed -> trying to restore backup..."
	cat $backup > $update
	if [ $? -ne 0 ]; then
		echo "Restore: write failed"
		exit 1
	fi
	# read CCAT flash content back into a temporary file
	cat $update > $tmp
	if [ $? -ne 0 ]; then
		echo "Restore: read from flash failed"
		exit 1
	fi
	diff $backup $tmp > /dev/null
	if [ $? -eq 0 ]; then
		echo "Restore: was successful"
		rm -f $backup
	else
		echo "WARNING restore failed! Try to fix it manually using the backup file " $backup
		# we keep the backup file
	fi
fi

# clean up our temp file
rm -f $tmp
