DURATION=3

sh rw_gpio_all.sh
sleep 1
# CX2030
for i in $(seq 244 1 252); do # CX5000 #for i in $(seq 232 1 255)
	sh toggle_gpio.sh ${i} ${DURATION} &
done
