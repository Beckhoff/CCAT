DEV_PREFIX=/dev/ccat_update
SRAM_PREFIX=/dev/ccat_sram

prepare:
	sudo chmod g+r ${DEV_PREFIX}*
	sudo chgrp ccat ${DEV_PREFIX}*
	sudo chmod g+r ${SRAM_PREFIX}*
	sudo chmod g+w ${SRAM_PREFIX}*
	sudo chgrp ccat ${SRAM_PREFIX}*

clean:
	@rm -f sram?.bin
	@rm -f update?.bin

sram%.bin: ${SRAM_PREFIX}%
	dd if=$@.refA of=$<
	dd if=$< of=$@
	@diff $@ $@.refA
	dd if=$@.refB of=$<
	dd if=$< of=$@
	@diff $@ $@.refB

update%.bin: ${DEV_PREFIX}%
	dd if=$< of=$@
	@diff $@ $@.ref

sram_test: clean sram0.bin

update_read_test: clean update0.bin update1.bin update2.bin
