#!/bin/bash

for i in $(seq 244 1 251)
do
sh rw_gpio.sh ${i} &
done
sh rw_gpio.sh 252
