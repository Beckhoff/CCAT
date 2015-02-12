DEV_PREFIX=/dev/ccat_update

prepare:
	sudo chmod g+r ${DEV_PREFIX}*
	sudo chgrp ccat ${DEV_PREFIX}*

clean:
	@rm -f update?.bin

update%.bin: ${DEV_PREFIX}%
	dd if=$< of=$@
	@diff $@ $@.ref

update_read_test: clean update0.bin update1.bin update2.bin
