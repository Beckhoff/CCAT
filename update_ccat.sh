#!/bin/sh
# Use this script to update your CCAT FPGA configuration in flash.
# You should provide the path to your *.rbf as first parameter to the
# script. As a first step a backup of the current FPGAs flash content is
# read and saved as *.rbf.~ccat_update_backup. Then the contents of *.rbf
# is written to the FPGA and read back. If the compare of the readback
# data and the original file fails, the script tries to restore the
# backup. If that restore fails, too, keep calm and try it manually. As
# long as the FPGA stays powered on you have a chance to recover!
#
#    Copyright (C) 2014  Beckhoff Automation GmbH
#    Author: Patrick Bruenn <p.bruenn@beckhoff.com>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

rbf=$1
tmp=$rbf.~ccat_update_tmp
backup=$rbf.~ccat_update_backup
update=/dev/ccat_update
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
