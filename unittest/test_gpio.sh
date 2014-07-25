DURATION=3

sh rw_gpio_all.sh
sleep 1
for i in $(seq 244 1 252)
do
sh toggle_gpio.sh ${i} ${DURATION} &
done
