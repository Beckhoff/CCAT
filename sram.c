/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2015  Beckhoff Automation GmbH
    Author: Patrick Bruenn <p.bruenn@beckhoff.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "module.h"
#include <asm/io.h>

static int ccat_sram_probe(struct ccat_function *func)
{
	const u8 type = func->info.sram_width & 0x3;
	const u32 size = (1 << func->info.sram_size)/1024;
	void *ioaddr = func->ccat->bar_0 + func->info.addr;

	pr_info("%s: 0x%04x rev: 0x%04x\n", __FUNCTION__, func->info.type, func->info.rev);
	if (type == 0) {
		pr_info("no sram connected\n");
		return 0;
	}

	pr_info("%dBit sram connected with %d kbytes capacity @%p\n", 8*type, size, ioaddr);
	pr_info("0x%08x\n", ioread32(ioaddr));
	iowrite32(~ioread32(ioaddr), ioaddr);
	pr_info("0x%08x\n", ioread32(ioaddr));

	return 0;
}

static void ccat_sram_remove(struct ccat_function *func)
{
	pr_info("%s\n", __FUNCTION__);
}

struct ccat_driver sram_driver = {
	.type = CCATINFO_SRAM,
	.probe = ccat_sram_probe,
	.remove = ccat_sram_remove,
};
