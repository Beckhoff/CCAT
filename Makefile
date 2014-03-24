TARGET = ccat_eth
obj-m += $(TARGET).o
$(TARGET)-objs := ccat.o netdev.o print.o update.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	- sudo rmmod $(TARGET)
	sudo cp ./$(TARGET).ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	sudo modprobe $(TARGET)
	env sleep 1
	#sudo ifconfig eth2 debug

indent: ccat.c ccat.h netdev.c netdev.h print.c print.h
	./Lindent $?

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.c~ *.h~ *.bin

test:
	gcc ./unittest/test__update.c -o test__update.bin
	sudo ./test__update.bin 1000 /home/gpb/CX2001_B000_20130425_Release_V3.rbf
