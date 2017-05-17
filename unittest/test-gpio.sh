#!/bin/bash -l

gpiochip=$(dmesg | tac | grep -m1 "ccat_gpio: registered ccat_gpio as gpiochip" | awk {'printf $(NF-3)'})
gpioprefix="/sys/class/gpio"
base=$(cat ${gpioprefix}/${gpiochip}/base)
ngpio=$(cat ${gpioprefix}/${gpiochip}/ngpio)
last=$((base + ngpio))

function for_each_gpio() {
	for ((i = base; i < last; i++)); do
		if [ $# -eq 2 ]; then
			printf "$1" >${gpioprefix}/gpio$i/$2
		else
			printf "$i" >${gpioprefix}/$1
		fi
	done
}

for_each_gpio export
sleep 1
for_each_gpio "out" "direction"
for_each_gpio 1 "value"
sleep 1
for_each_gpio 0 "value"
sleep 1
for_each_gpio unexport
