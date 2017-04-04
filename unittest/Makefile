DEV_PREFIX=/dev/ccat_

all: sram_test update_test

prepare:
	sudo chmod g+r ${DEV_PREFIX}*
	sudo chmod g+w ${DEV_PREFIX}*
	sudo chgrp ccat ${DEV_PREFIX}*

sram_test: prepare
	$(CURDIR)/test-rw_cdev.sh sram 0 131072

update_test: prepare
	$(CURDIR)/test-update.sh
