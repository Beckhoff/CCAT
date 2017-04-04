DEV_PREFIX=/dev/ccat_

all: sram_test

prepare:
	sudo chmod g+r ${DEV_PREFIX}*
	sudo chmod g+w ${DEV_PREFIX}*
	sudo chgrp ccat ${DEV_PREFIX}*

clean:
	@rm -f update?.bin

update%.bin: ${DEV_PREFIX}%
	dd if=$< of=$@
	@diff $@ $@.ref

sram_test: prepare
	$(CURDIR)/test-rw_cdev.sh sram 0 131072

update_read_test: clean update0.bin update1.bin update2.bin
