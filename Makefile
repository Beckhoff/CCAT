TARGET = ccat
obj-m += $(TARGET).o
$(TARGET)-objs := module.o netdev.o print.o update.o
#CFLAGS_print.o :=-DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	- sudo rmmod $(TARGET)
	sudo cp ./$(TARGET).ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	sudo modprobe $(TARGET)
	env sleep 1
	#sudo ifconfig eth2 debug

indent: module.c module.h netdev.c netdev.h print.c print.h update.c update.h
	./Lindent $?

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.c~ *.h~ *.bin
