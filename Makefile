TARGET = ccat
EXTRA_DIR = /lib/modules/$(shell uname -r)/extra/
obj-m += $(TARGET).o
$(TARGET)-objs := module.o netdev.o update.o
#ccflags-y := -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	- sudo rmmod $(TARGET)
	sudo mkdir -p $(EXTRA_DIR)
	sudo cp ./$(TARGET).ko $(EXTRA_DIR)
	sudo depmod -a
	sudo modprobe $(TARGET)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.c~ *.h~ *.bin

# indent the source files with the kernels Lindent script
indent: compat.h module.c module.h netdev.c netdev.h print.c print.h update.c update.h
	./Lindent $?
