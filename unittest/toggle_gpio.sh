GPIO=${1}
DURATION=${2}
FREQUENZY=0.25

echo $GPIO >/sys/class/gpio/export
sleep 2
echo "out" >/sys/class/gpio/gpio${GPIO}/direction
sleep 2
for i in $(seq 1 1 ${DURATION}); do
	echo 1 >/sys/class/gpio/gpio${GPIO}/value
	sleep ${FREQUENZY}
	echo 0 >/sys/class/gpio/gpio${GPIO}/value
	sleep ${FREQUENZY}
done
echo $GPIO >/sys/class/gpio/unexport
