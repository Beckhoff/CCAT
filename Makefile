TARGET = ccat_eth
obj-m += $(TARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	- sudo rmmod $(TARGET)
	sudo cp ./$(TARGET).ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	sudo modprobe $(TARGET)
	env sleep 1
	sudo ifconfig eth2 debug

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

