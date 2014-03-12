/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014  Beckhoff Automation GmbH
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

#include <asm/dma.h>
#include "ccat.h"

void ccat_bar_free(struct ccat_bar *bar)
{
	const struct ccat_bar tmp = {
		.start = bar->start,
		.len = bar->len,
		.ioaddr = bar->ioaddr
	};
	memset(bar, 0, sizeof(*bar));
	iounmap(tmp.ioaddr);
	release_mem_region(tmp.start, tmp.len);
}

int ccat_bar_init(struct ccat_bar *bar, size_t index, struct pci_dev *pdev)
{
	struct resource *res;
	bar->start = pci_resource_start(pdev, index);
	bar->end = pci_resource_end(pdev, index);
	bar->len = pci_resource_len(pdev, index);
	bar->flags = pci_resource_flags(pdev, index);
	if(!(IORESOURCE_MEM & bar->flags)) {
		printk(KERN_INFO "%s: bar%d should be memory space, but it isn't -> abort CCAT initialization.\n", DRV_NAME, index);
		return -EIO;
	}
	
	res = request_mem_region(bar->start, bar->len, DRV_NAME);
	if(!res) {
		printk(KERN_INFO "%s: allocate mem_region failed.\n", DRV_NAME);
		return -EIO;
	}
	printk(KERN_INFO "%s: bar%d at [%lx,%lx] len=%lu.\n", DRV_NAME, index, bar->start, bar->end, bar->len);
	printk(KERN_INFO "%s: bar%d mem_region resource allocated as %p.\n", DRV_NAME, index, res);
	
	bar->ioaddr = ioremap(bar->start, bar->len);
	if(!bar->ioaddr) {
		printk(KERN_INFO "%s: bar%d ioremap failed.\n", DRV_NAME, index);
		release_mem_region(bar->start, bar->len);
		return -EIO;
	}
	printk(KERN_INFO "%s: bar%d I/O mem mapped to %p.\n", DRV_NAME, index, bar->ioaddr);
	return 0;
}

void ccat_dma_free(struct ccat_dma *const dma)
{
	const struct ccat_dma tmp = *dma;
	free_dma(dma->channel);
	memset(dma, 0, sizeof(*dma));	
	dma_free_coherent(tmp.dev, tmp.size, tmp.virt, tmp.phys);
}

int ccat_dma_init(struct ccat_dma *const dma, size_t channel, void __iomem *const ioaddr, struct device *const dev)
{
	void *frame;
	uint64_t addr;
	uint32_t translateAddr;
	uint32_t memTranslate;
	uint32_t memSize;
	uint32_t data = 0xffffffff;
	uint32_t offset = (sizeof(uint64_t) * channel) + 0x1000;

	dma->channel = channel;
	dma->dev = dev;

	/* calculate size and alignments */
	iowrite32(data, ioaddr + offset);
	wmb();
	data = ioread32(ioaddr + offset);
	memTranslate = data & 0xfffffffc;
	memSize = (~memTranslate) + 1;
	dma->size = 2*memSize - PAGE_SIZE;
	dma->virt = dma_zalloc_coherent(dev, dma->size, &dma->phys, GFP_KERNEL);
	if(!dma->virt || !dma->phys) {
		printk(KERN_INFO "%s: init DMA%d memory failed.\n", DRV_NAME, channel);
		return -1;
	}

	if(request_dma(channel, DRV_NAME)) {
		printk(KERN_INFO "%s: request dma channel %d failed\n", DRV_NAME, channel);
		ccat_dma_free(dma);
		return -1;
	}

	translateAddr = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = translateAddr;
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
	frame = dma->virt + translateAddr - dma->phys;
	printk(KERN_INFO "%s: DMA%d mem initialized\n virt:         0x%p\n phys:         0x%llx\n translated:   0x%llx\n pci addr:     0x%08x%x\n memTranslate: 0x%x\n size:         %u bytes.\n", DRV_NAME, channel, dma->virt, (uint64_t)(dma->phys), addr, ioread32(ioaddr + offset + 4), ioread32(ioaddr + offset), memTranslate, dma->size);
	return 0;
}
