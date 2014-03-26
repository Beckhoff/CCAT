#!/bin/sh
# Writes the file specified by $1 to the CCAT flash, after writting a copy
# of the current CCAT flash content is read and compared to the contents of $1

rbf=$1
update=/dev/ccat_update
tmp=$rbf.~ccat_update
bytes=$(echo $(wc -c $rbf)|cut -d' ' -f1)

# check if device file is available
if ! [ -c $update ]; then
	echo $update does not exist
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
	echo "Update failed"
fi

# clean up our temp file
rm -f $tmp
