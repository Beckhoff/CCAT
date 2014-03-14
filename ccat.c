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
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "ccat.h"
#include "netdev.h"

static void ccat_bar_free(struct ccat_bar *bar)
{
	if(bar->ioaddr) {
		const struct ccat_bar tmp = *bar;
		memset(bar, 0, sizeof(*bar));
		iounmap(tmp.ioaddr);
		release_mem_region(tmp.start, tmp.len);
	} else {
		pr_warn("%s(): %p was already done.\n", __FUNCTION__, bar);
	}
}

static int ccat_bar_init(struct ccat_bar *bar, size_t index,
			 struct pci_dev *pdev)
{
	struct resource *res;
	bar->start = pci_resource_start(pdev, index);
	bar->end = pci_resource_end(pdev, index);
	bar->len = pci_resource_len(pdev, index);
	bar->flags = pci_resource_flags(pdev, index);
	if (!(IORESOURCE_MEM & bar->flags)) {
		pr_info("bar%d is no mem_region -> abort.\n", index);
		return -EIO;
	}

	res = request_mem_region(bar->start, bar->len, DRV_NAME);
	if (!res) {
		pr_info("allocate mem_region failed.\n");
		return -EIO;
	}
	pr_info("bar%d at [%lx,%lx] len=%lu res: %p.\n", index,
		bar->start, bar->end, bar->len, res);

	bar->ioaddr = ioremap(bar->start, bar->len);
	if (!bar->ioaddr) {
		pr_info("bar%d ioremap failed.\n", index);
		release_mem_region(bar->start, bar->len);
		return -EIO;
	}
	pr_info("bar%d I/O mem mapped to %p.\n", index, bar->ioaddr);
	return 0;
}

static void ccat_dma_free(struct ccat_dma *const dma)
{
	const struct ccat_dma tmp = *dma;
	free_dma(dma->channel);
	memset(dma, 0, sizeof(*dma));
	dma_free_coherent(tmp.dev, tmp.size, tmp.virt, tmp.phys);
}

int ccat_dma_init(struct ccat_dma *const dma, size_t channel,
		  void __iomem * const ioaddr, struct device *const dev)
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
	dma->size = 2 * memSize - PAGE_SIZE;
	dma->virt = dma_zalloc_coherent(dev, dma->size, &dma->phys, GFP_KERNEL);
	if (!dma->virt || !dma->phys) {
		pr_info("init DMA%d memory failed.\n", channel);
		return -1;
	}

	if (request_dma(channel, DRV_NAME)) {
		pr_info("request dma channel %d failed\n", channel);
		ccat_dma_free(dma);
		return -1;
	}

	translateAddr = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = translateAddr;
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
	frame = dma->virt + translateAddr - dma->phys;
	pr_info
	    ("DMA%d mem initialized\n virt:         0x%p\n phys:         0x%llx\n translated:   0x%llx\n pci addr:     0x%08x%x\n memTranslate: 0x%x\n size:         %u bytes.\n",
	     channel, dma->virt, (uint64_t) (dma->phys), addr,
	     ioread32(ioaddr + offset + 4), ioread32(ioaddr + offset),
	     memTranslate, dma->size);
	return 0;
}

static int ccat_functions_init(struct ccat_device *const ccatdev)
{
	/* read CCatInfoBlock.nMaxEntries from ccat */
	const uint8_t num_func = ioread8(ccatdev->bar[0].ioaddr + 4);
	void __iomem *addr = ccatdev->bar[0].ioaddr;
	const void __iomem *end = addr + (sizeof(CCatInfoBlock) * num_func);
	int status = 0;

	/* find CCATINFO_ETHERCAT_MASTER_DMA function */
	while (addr < end) {
		const uint8_t type = ioread16(addr);
		switch (type) {
			case CCATINFO_NOTUSED:
				break;
			case CCATINFO_ETHERCAT_MASTER_DMA:
				pr_info("Found: ETHERCAT_MASTER_DMA -> initializing\n");
				ccatdev->ethdev = ccat_eth_init(ccatdev);
				status = (NULL == ccatdev->ethdev);
				break;
			default:
				pr_info("Found: 0x%04x not supported\n", type);
				break;
		}
		addr += sizeof(CCatInfoBlock);
	}
	return status;
}

static void ccat_functions_remove(struct ccat_device *const ccatdev)
{
	if(!ccatdev->ethdev) {
		pr_warn("%s(): 'ethdev' was not initialized.\n", __FUNCTION__);
	} else {
		struct ccat_eth_priv *const ethdev = ccatdev->ethdev;
		ccatdev->ethdev = NULL;
		ccat_eth_remove(ethdev);
	}
}

static int ccat_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int status;
	u8 revision;
	struct ccat_device *ccatdev = kmalloc(sizeof(*ccatdev), GFP_KERNEL);
	if(!ccatdev) {
		pr_err("%s() out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	memset(ccatdev, 0, sizeof(*ccatdev));
	ccatdev->pdev = pdev;
	pci_set_drvdata(pdev, ccatdev);

	status = pci_enable_device_mem(pdev);
	if (status) {
		pr_info("enable %s failed: %d\n", pdev->dev.kobj.name, status);
		return status;
	}

	status = pci_read_config_byte(pdev, PCI_REVISION_ID, &revision);
	if (status) {
		pr_warn("read CCAT pci revision failed with %d\n", status);
		return status;
	}

	/* FIXME upgrade to a newer kernel to get support of dma_set_mask_and_coherent()
	 * (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64))) {
	 */
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		pr_info("64 bit DMA supported, pci rev: %u\n", revision);
		/*} else if (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32))) { */
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		pr_info("32 bit DMA supported, pci rev: %u\n", revision);
	} else {
		pr_warn("No suitable DMA available, pci rev: %u\n", revision);
	}

	if (ccat_bar_init(&ccatdev->bar[0], 0, pdev)) {
		pr_warn("initialization of bar0 failed.\n");
		return -EIO;
	}

	if (ccat_bar_init(&ccatdev->bar[2], 2, pdev)) {
		pr_warn("initialization of bar2 failed.\n");
		return -EIO;
	}

	pci_set_master(pdev);
	return ccat_functions_init(ccatdev);
}

static void ccat_remove(struct pci_dev *pdev)
{
	struct ccat_device *ccatdev = pci_get_drvdata(pdev);
	if(ccatdev) {
		//TODO
		ccat_functions_remove(ccatdev);
		ccat_bar_free(&ccatdev->bar[2]);
		ccat_bar_free(&ccatdev->bar[0]);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		kfree(ccatdev);
	}
	pr_info("%s() done.\n", __FUNCTION__);
}

#define PCI_DEVICE_ID_BECKHOFF_CCAT 0x5000
#define PCI_VENDOR_ID_BECKHOFF 0x15EC

static const struct pci_device_id pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_BECKHOFF, PCI_DEVICE_ID_BECKHOFF_CCAT)},
	{0,},
};

MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver pci_driver = {
	.name = DRV_NAME,
	.id_table = pci_ids,
	.probe = ccat_probe,
	.remove = ccat_remove,
};

static void ccat_exit_module(void)
{
	pci_unregister_driver(&pci_driver);
}

static int ccat_init_module(void)
{
	static const size_t offset = offsetof(struct ccat_eth_frame, data);
	BUILD_BUG_ON(sizeof(struct ccat_eth_frame) != sizeof(CCatDmaTxFrame));
	BUILD_BUG_ON(sizeof(struct ccat_eth_frame) != sizeof(CCatRxDesc));
	BUILD_BUG_ON(offset != offsetof(CCatDmaTxFrame, data));
	BUILD_BUG_ON(offset != offsetof(CCatRxDesc, data));
	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	return pci_register_driver(&pci_driver);
}

module_exit(ccat_exit_module);
module_init(ccat_init_module);
