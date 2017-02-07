TARGET = ccat
EXTRA_DIR = /lib/modules/$(shell uname -r)/extra/
KDIR = /lib/modules/$(shell uname -r)/build
obj-m += $(TARGET).o
$(TARGET)-objs := gpio.o module.o netdev.o sram.o update.o
#ccflags-y := -DDEBUG
ccflags-y += -D__CHECK_ENDIAN__

all:
	make -C $(KDIR) $(MAKEFLAGS) M=$(CURDIR) modules

install:
	- sudo rmmod $(TARGET)
	sudo mkdir -p $(EXTRA_DIR)
	sudo cp ./$(TARGET).ko $(EXTRA_DIR)
	sudo depmod -a
	sudo modprobe $(TARGET)
	sudo ifconfig eth2 192.168.100.164 up

clean:
	make -C $(KDIR) M=$(CURDIR) clean
	rm -f *.c~ *.h~ *.bin

# indent the source files with the kernels Lindent script
indent: *.h *.c
	$(KDIR)/scripts/Lindent $?
