#!/bin/sh
# Writes the file specified by $1 to the CCAT flash, after writting a copy
# of the current CCAT flash content is read and compared to the contents of $1

rbf=$1
update=/dev/ccat_update
tmp=$rbf.~ccat_update
bytes=$(echo $(wc -c $rbf)|cut -d' ' -f1)

cat $rbf > $update
cat $update > $tmp
truncate -s $bytes $tmp
diff $rbf $tmp
if [ $? -eq 0 ]; then
	echo "Update complete"
else
	echo "Update failed"
fi
rm -f $tmp
