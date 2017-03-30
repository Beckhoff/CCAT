#!/bin/bash -l

set -e

echo "$0 running..."

current_clocksource="/sys/devices/system/clocksource/clocksource0/current_clocksource"
dmesg_pattern="clocksource: Switched to clocksource"
old_clock=$(cat ${current_clocksource})

# reset original clocksource on interruption or exit
cleanup() {
	printf "${old_clock}" >${current_clocksource}
}
trap cleanup INT TERM EXIT

# switch kernel clocksource to ccat_systemtime
printf "ccat" >${current_clocksource}
test "ccat" = $(cat ${current_clocksource})
echo "$0 done."
