#!/bin/sh
#$1 *.rbf file for FPGA update

update=/dev/ccat_update
tmp=$1.~ccat_update

cat $1 > $update
cat $update > $tmp
diff $1 $tmp
if [ $? -eq 0 ]; then
	echo "Update complete"
else
	echo "Update failed"
fi
rm -f $tmp
