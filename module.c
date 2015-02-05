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
#include "module.h"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Patrick Bruenn <p.bruenn@beckhoff.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * configure the drivers capabilities here
 */
static const struct ccat_driver *const driver_list[] = {
	&eth_driver,		/* load Ethernet MAC/EtherCAT Master driver from netdev.c */
	&gpio_driver,		/* load GPIO driver from gpio.c */
	&sram_driver,		/* load SRAM driver from sram.c */
	&update_driver,		/* load Update driver from update.c */
	NULL			/* this entry is used to detect the end, don't remove it! */
};

void ccat_dma_free(struct ccat_dma *const dma)
{
	const struct ccat_dma tmp = *dma;

	free_dma(dma->channel);
	memset(dma, 0, sizeof(*dma));
	dma_free_coherent(tmp.dev, tmp.size, tmp.virt, tmp.phys);
}

/**
 * ccat_dma_init() - Initialize CCAT and host memory for DMA transfer
 * @dma object for management data which will be initialized
 * @channel number of the DMA channel
 * @ioaddr of the pci bar2 configspace used to calculate the address of the pci dma configuration
 * @dev which should be configured for DMA
 */
int ccat_dma_init(struct ccat_dma *const dma, size_t channel,
		  void __iomem * const ioaddr, struct device *const dev)
{
	void *frame;
	u64 addr;
	u32 translateAddr;
	u32 memTranslate;
	u32 memSize;
	u32 data = 0xffffffff;
	u32 offset = (sizeof(u64) * channel) + 0x1000;

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
		pr_info("init DMA%llu memory failed.\n", (u64) channel);
		return -1;
	}

	if (request_dma(channel, KBUILD_MODNAME)) {
		pr_info("request dma channel %llu failed\n", (u64) channel);
		ccat_dma_free(dma);
		return -1;
	}

	translateAddr = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = translateAddr;
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
	frame = dma->virt + translateAddr - dma->phys;
	pr_debug
	    ("DMA%llu mem initialized\n virt:         0x%p\n phys:         0x%llx\n translated:   0x%llx\n pci addr:     0x%08x%x\n memTranslate: 0x%x\n size:         %llu bytes.\n",
	     (u64) channel, dma->virt, (u64) (dma->phys), addr,
	     ioread32(ioaddr + offset + 4), ioread32(ioaddr + offset),
	     memTranslate, (u64) dma->size);
	return 0;
}

static const struct ccat_driver *ccat_function_connect(struct ccat_function
						       *const func)
{
	const struct ccat_driver *const *drv;

	for (drv = driver_list; NULL != *drv; drv++) {
		if (func->info.type == (*drv)->type) {
			return (*drv)->probe(func) ? NULL : *drv;
		}
	}
	return NULL;
}

/**
 * Initialize all available CCAT functions.
 *
 * Return: count of failed functions
 */
static int ccat_functions_init(struct ccat_device *const ccatdev)
{
	static const size_t block_size = sizeof(struct ccat_info_block);
	struct ccat_function *next = kzalloc(sizeof(*next), GFP_KERNEL);
	void __iomem *addr = ccatdev->bar_0; /** first block is the CCAT information block entry */
	const u8 num_func = ioread8(addr + 4); /** number of CCAT function blocks is at offset 0x4 */
	const void __iomem *end = addr + (block_size * num_func);

	INIT_LIST_HEAD(&ccatdev->functions);
	for (; addr < end && next; addr += block_size) {
		memcpy_fromio(&next->info, addr, sizeof(next->info));
		if (CCATINFO_NOTUSED != next->info.type) {
			next->ccat = ccatdev;
			next->drv = ccat_function_connect(next);
			if (next->drv) {
				list_add(&next->list, &ccatdev->functions);
				next = kzalloc(sizeof(*next), GFP_KERNEL);
			}
		}
	}
	kfree(next);
	return list_empty(&ccatdev->functions);
}

/**
 * Destroy all previously initialized CCAT functions
 */
static void ccat_functions_remove(struct ccat_device *const dev)
{
	struct ccat_function *func;
	struct ccat_function *tmp;
	list_for_each_entry_safe(func, tmp, &dev->functions, list) {
		if (func->drv) {
			func->drv->remove(func);
			func->drv = NULL;
		}
		list_del(&func->list);
		kfree(func);
	}
}

static int ccat_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ccat_device *ccatdev;
	u8 revision;
	int status;

	ccatdev = devm_kzalloc(&pdev->dev, sizeof(*ccatdev), GFP_KERNEL);
	if (!ccatdev) {
		pr_err("%s() out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	ccatdev->pdev = pdev;
	pci_set_drvdata(pdev, ccatdev);

	status = pci_enable_device_mem(pdev);
	if (status) {
		pr_info("enable %s failed: %d\n", pdev->dev.kobj.name, status);
		goto cleanup_pci_device;
	}

	status = pci_read_config_byte(pdev, PCI_REVISION_ID, &revision);
	if (status) {
		pr_warn("read CCAT pci revision failed with %d\n", status);
		goto cleanup_pci_device;
	}

	if ((status = pci_request_regions(pdev, KBUILD_MODNAME))) {
		pr_info("allocate mem_regions failed.\n");
		goto cleanup_pci_device;
	}

	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		pr_debug("64 bit DMA supported, pci rev: %u\n", revision);
	} else if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
		pr_debug("32 bit DMA supported, pci rev: %u\n", revision);
	} else {
		pr_warn("No suitable DMA available, pci rev: %u\n", revision);
	}

	if (!(ccatdev->bar_0 = pci_iomap(pdev, 0, 0))) {
		pr_warn("initialization of bar0 failed.\n");
		status = -EIO;
		goto cleanup_pci_device;
	}

	if (!(ccatdev->bar_2 = pci_iomap(pdev, 2, 0))) {
		pr_warn("initialization of optional bar2 failed.\n");
	}

	pci_set_master(pdev);
	if (ccat_functions_init(ccatdev)) {
		pr_warn("some functions couldn't be initialized\n");
	}
	return 0;
cleanup_pci_device:
	pci_disable_device(pdev);
	return status;
}

static void ccat_remove(struct pci_dev *pdev)
{
	struct ccat_device *ccatdev = pci_get_drvdata(pdev);

	if (ccatdev) {
		ccat_functions_remove(ccatdev);
		if (ccatdev->bar_2)
			pci_iounmap(pdev, ccatdev->bar_2);
		pci_iounmap(pdev, ccatdev->bar_0);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
	}
}

#define PCI_DEVICE_ID_BECKHOFF_CCAT 0x5000
#define PCI_VENDOR_ID_BECKHOFF 0x15EC

static const struct pci_device_id pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_BECKHOFF, PCI_DEVICE_ID_BECKHOFF_CCAT)},
	{0,},
};

MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver ccat_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci_ids,
	.probe = ccat_probe,
	.remove = ccat_remove,
};

extern void __exit ccat_update_exit(void);
extern int __init ccat_update_init(void);

static int __init ccat_init_module(void)
{
	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	ccat_update_init();
	return pci_register_driver(&ccat_driver);
}

static void __exit ccat_exit(void)
{
	pci_unregister_driver(&ccat_driver);
	ccat_update_exit();
}

module_init(ccat_init_module);
module_exit(ccat_exit);
