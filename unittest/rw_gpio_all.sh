#!/bin/bash

if [ $# -ne 1 ]; then
	echo "Usage $0 <gpio_base>\n      $0 244"
	exit -1
fi

BASE=$1
#on CX2030
NUM=11

for i in $(seq ${BASE} 1 $((BASE+NUM)))
do
sh rw_gpio.sh ${i} &
done
