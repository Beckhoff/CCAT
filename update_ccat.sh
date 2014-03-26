#!/bin/sh
# Writes the file specified by $1 to the CCAT flash, after writting a copy
# of the current CCAT flash content is read and compared to the contents of $1

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
		rm -f $tmp
	else
		echo "WARNING restore failed! Situation seems really bad now! you find the backup at " $backup
		# we keep the backup file
	fi
fi

# clean up our temp file
rm -f $tmp
