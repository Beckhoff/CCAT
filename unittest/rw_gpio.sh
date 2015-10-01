#!/bin/bash

GPIO=${1}
CAT_DIRECTION="cat /sys/class/gpio/gpio${GPIO}/direction"
CAT_VALUE="cat /sys/class/gpio/gpio${GPIO}/value"

echo $GPIO > /sys/class/gpio/export
sleep 1
echo "out" > /sys/class/gpio/gpio${GPIO}/direction
echo 1 > /sys/class/gpio/gpio${GPIO}/value
sleep 1
echo "${GPIO}: $(${CAT_DIRECTION}) $(${CAT_VALUE})"

echo 0 > /sys/class/gpio/gpio${GPIO}/value
sleep 1
echo "${GPIO}: $(${CAT_DIRECTION}) $(${CAT_VALUE})"
echo $GPIO > /sys/class/gpio/unexport
