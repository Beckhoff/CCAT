/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014-2015  Beckhoff Automation GmbH
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
#include <linux/platform_device.h>
#include "module.h"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Patrick Bruenn <p.bruenn@beckhoff.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * configure the drivers capabilities here
 */
static const struct ccat_driver *const drivers[] = {
#ifdef BUILD_CX9020
	&eth_nodma_driver,	/* load Ethernet MAC/EtherCAT Master without DMA driver from */
#else
	&eth_driver,		/* load Ethernet MAC/EtherCAT Master driver from netdev.c */
#endif /* #ifdef BUILD_CX9020 */
	&gpio_driver,		/* load GPIO driver from gpio.c */
	&sram_driver,		/* load SRAM driver from sram.c */
	&update_driver,		/* load Update driver from update.c */
};

static int __init ccat_class_init(struct ccat_class *base)
{
	if (1 == atomic_inc_return(&base->instances)) {
		if (alloc_chrdev_region(&base->dev, 0, base->count, KBUILD_MODNAME)) {
			pr_warn("alloc_chrdev_region() for '%s' failed\n", base->name);
			return -1;
		}

		base->class = class_create(THIS_MODULE, base->name);
		if (!base->class) {
			pr_warn("Create device class '%s' failed\n", base->name);
			unregister_chrdev_region(base->dev, base->count);
			return -1;
		}
	}
	return 0;
}

static void ccat_class_exit(struct ccat_class *base)
{
	if ( !atomic_dec_return(&base->instances) ) {
		class_destroy(base->class);
		unregister_chrdev_region(base->dev, base->count);
	}
}

static void free_ccat_cdev(struct ccat_cdev *ccdev)
{
	ccat_class_exit(ccdev->class);
	ccdev->dev = 0;
}

static struct ccat_cdev *alloc_ccat_cdev(struct ccat_class *base)
{
	int i = 0;

	ccat_class_init(base);
	for (i = 0; i < base->count; ++i) {
		if (base->devices[i].dev == 0) {
			base->devices[i].dev = MKDEV(MAJOR(base->dev), i);
			return &base->devices[i];
		}
	}
	pr_warn("exceeding max. number of '%s' devices (%d)\n",
		base->class->name, base->count);
	atomic_dec_return(&base->instances);
	return NULL;
}

static int ccat_cdev_init(struct cdev *cdev, dev_t dev, struct class *class,
			  struct file_operations *fops)
{
	if (!device_create
	    (class, NULL, dev, NULL, "%s%d", class->name, MINOR(dev))) {
		pr_warn("device_create() failed\n");
		return -1;
	}

	cdev_init(cdev, fops);
	cdev->owner = fops->owner;
	if (cdev_add(cdev, dev, 1)) {
		pr_warn("add update device failed\n");
		device_destroy(class, dev);
		return -1;
	}

	pr_info("registered %s%d.\n", class->name, MINOR(dev));
	return 0;
}

int ccat_cdev_open(struct inode *const i, struct file *const f)
{
	struct ccat_cdev *ccdev =
	    container_of(i->i_cdev, struct ccat_cdev, cdev);
	struct cdev_buffer *buf;

	if (!atomic_dec_and_test(&ccdev->in_use)) {
		atomic_inc(&ccdev->in_use);
		return -EBUSY;
	}

	buf = kzalloc(sizeof(*buf) + ccdev->iosize, GFP_KERNEL);
	if (!buf) {
		atomic_inc(&ccdev->in_use);
		return -ENOMEM;
	}

	buf->ccdev = ccdev;
	f->private_data = buf;
	return 0;
}

int ccat_cdev_probe(struct ccat_function *func, struct ccat_class *cdev_class,
		    size_t iosize)
{
	struct ccat_cdev *const ccdev = alloc_ccat_cdev(cdev_class);
	if (!ccdev) {
		return -ENOMEM;
	}

	ccdev->ioaddr = func->ccat->bar_0 + func->info.addr;
	ccdev->iosize = iosize;
	atomic_set(&ccdev->in_use, 1);

	if (ccat_cdev_init
	    (&ccdev->cdev, ccdev->dev, cdev_class->class, &cdev_class->fops)) {
		pr_warn("ccat_cdev_probe() failed\n");
		free_ccat_cdev(ccdev);
		return -1;
	}
	ccdev->class = cdev_class;
	func->private_data = ccdev;
	return 0;
}

int ccat_cdev_release(struct inode *const i, struct file *const f)
{
	const struct cdev_buffer *const buf = f->private_data;
	struct ccat_cdev *const ccdev = buf->ccdev;

	kfree(f->private_data);
	atomic_inc(&ccdev->in_use);
	return 0;
}

void ccat_cdev_remove(struct ccat_function *func)
{
	struct ccat_cdev *const ccdev = func->private_data;

	cdev_del(&ccdev->cdev);
	device_destroy(ccdev->class->class, ccdev->dev);
	free_ccat_cdev(ccdev);
}

static const struct ccat_driver *ccat_function_connect(struct ccat_function
						       *const func)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(drivers); ++i) {
		if (func->info.type == drivers[i]->type) {
			return drivers[i]->probe(func) ? NULL : drivers[i];
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

#ifdef CONFIG_PCI
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

static int ccat_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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

static void ccat_pci_remove(struct pci_dev *pdev)
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

static struct pci_driver ccat_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci_ids,
	.probe = ccat_pci_probe,
	.remove = ccat_pci_remove,
};
module_pci_driver(ccat_pci_driver);
#endif /* #ifdef CONFIG_PCI */

#ifdef BUILD_CX9020
static int ccat_eim_probe(struct platform_device *pdev)
{
	struct ccat_device *ccatdev;

	ccatdev = devm_kzalloc(&pdev->dev, sizeof(*ccatdev), GFP_KERNEL);
	if (!ccatdev) {
		pr_err("%s() out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	ccatdev->pdev = pdev;
	platform_set_drvdata(pdev, ccatdev);

	if (!request_mem_region(0xf0000000, 0x02000000, pdev->name)) {
		pr_warn("request mem region failed.\n");
		return -EIO;
	}

	if (!(ccatdev->bar_0 = ioremap(0xf0000000, 0x02000000))) {
		pr_warn("initialization of bar0 failed.\n");
		return -EIO;
	}

	ccatdev->bar_2 = NULL;

	if (ccat_functions_init(ccatdev)) {
		pr_warn("some functions couldn't be initialized\n");
	}
	return 0;
}

static int ccat_eim_remove(struct platform_device *pdev)
{
	struct ccat_device *ccatdev = platform_get_drvdata(pdev);

	if (ccatdev) {
		ccat_functions_remove(ccatdev);
		iounmap(ccatdev->bar_0);
		release_mem_region(0xf0000000, 0x02000000);
	}
	return 0;
}

static const struct of_device_id bhf_eim_ccat_ids[] = {
	{ .compatible = "bhf,emi-ccat", },
	{}
};
MODULE_DEVICE_TABLE(of, bhf_eim_ccat_ids);

static struct platform_driver ccat_eim_driver = {
	.driver = {
		.name = "cx9020-ccat",
		.of_match_table = bhf_eim_ccat_ids,
	},
	.probe = ccat_eim_probe,
	.remove = ccat_eim_remove,
};
module_platform_driver(ccat_eim_driver);
#endif /* #ifdef BUILD_CX9020 */
