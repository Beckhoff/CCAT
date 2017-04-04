EXTRA_DIR = /lib/modules/$(shell uname -r)/extra/
KDIR = /lib/modules/$(shell uname -r)/build
obj-m += ccat.o ccat_netdev.o ccat_gpio.o ccat_sram.o ccat_systemtime.o ccat_update.o
ccat-y := module.o
ccat_netdev-y := netdev.o
ccat_gpio-y := gpio.o
ccat_sram-y := sram.o
ccat_systemtime-y := systemtime.o
ccat_update-y := update.o
#ccflags-y := -DDEBUG
ccflags-y += -D__CHECK_ENDIAN__

DEV_PREFIX=/dev/ccat_

all:
	make -C $(KDIR) $(MAKEFLAGS) M=$(CURDIR) modules

install:
	- sudo rmmod ccat_netdev
	- sudo rmmod ccat_gpio
	- sudo rmmod ccat_sram
	- sudo rmmod ccat_systemtime
	- sudo rmmod ccat_update
	- sudo rmmod ccat
	sudo mkdir -p $(EXTRA_DIR)
	sudo cp ./ccat.ko $(EXTRA_DIR)
	sudo cp ./ccat_netdev.ko $(EXTRA_DIR)
	sudo cp ./ccat_gpio.ko $(EXTRA_DIR)
	sudo cp ./ccat_sram.ko $(EXTRA_DIR)
	sudo cp ./ccat_systemtime.ko $(EXTRA_DIR)
	sudo cp ./ccat_update.ko $(EXTRA_DIR)
	sudo depmod -a
	sudo modprobe ccat
	sudo modprobe ccat_netdev
	sudo modprobe ccat_gpio
	sudo modprobe ccat_sram
	sudo modprobe ccat_systemtime
	sudo modprobe ccat_update

clean:
	make -C $(KDIR) M=$(CURDIR) clean
	rm -f *.c~ *.h~ *.bin

# indent the source files with the kernels Lindent script
indent: *.h *.c
	$(KDIR)/scripts/Lindent $?
	$(CURDIR)/tools/shfmt -w $(CURDIR)/scripts $(CURDIR)/unittest

unittest:
	sudo chmod g+r ${DEV_PREFIX}*
	sudo chmod g+w ${DEV_PREFIX}*
	sudo chgrp ccat ${DEV_PREFIX}*

	cd unittest && ./test-rw_cdev.sh sram 0 131072
	cd unittest && ./test-update.sh

.PHONY: clean indent unittest
