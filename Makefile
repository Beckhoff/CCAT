TARGET = ccat_eth
obj-m += $(TARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	- sudo rmmod $(TARGET)
	sudo insmod ./$(TARGET).ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

