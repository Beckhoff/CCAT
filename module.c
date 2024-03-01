// SPDX-License-Identifier: MIT
/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014-2018  Beckhoff Automation GmbH & Co. KG
    Author: Patrick Bruenn <p.bruenn@beckhoff.com>
*/

// vim: noexpandtab

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include "module.h"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Patrick Bruenn <p.bruenn@beckhoff.com>");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRV_VERSION);

static struct ccat_cell ccat_cells[] = {
	{
	 .type = CCATINFO_INFO,
	 .cell = {.name = "ccat_info"},
	 },
	{
	 .type = CCATINFO_ETHERCAT_SLAVE,
	 .cell = {.name = "ccat_esc"},
	 },
	{
	 .type = CCATINFO_ETHERCAT_NODMA,
	 .cell = {.name = "ccat_eth_eim"},
	 },
	{
	 .type = CCATINFO_ETHERCAT_MASTER_DMA,
	 .cell = {.name = "ccat_eth_dma"},
	 },
	{
	 .type = CCATINFO_GPIO,
	 .cell = {.name = "ccat_gpio"},
	 },
	{
	 .type = CCATINFO_EPCS_PROM,
	 .cell = {.name = "ccat_update"},
	 },
	{
	 .type = CCATINFO_SRAM,
	 .cell = {.name = "ccat_sram"},
	 },
	{
	 .type = CCATINFO_SYSTEMTIME,
	 .cell = {.name = "ccat_systemtime"},
	 },
	{
	 .type = CCATINFO_IRQ,
	 .cell = {.name = "ccat_irq"},
	 },
	{
	 .type = CCATINFO_EEPROM,
	 .cell = {.name = "ccat_eeprom"},
	 },
};

static int __init ccat_class_init(struct ccat_class *base)
{
	if (1 == atomic_inc_return(&base->instances)) {
		if (alloc_chrdev_region
		    (&base->dev, 0, base->count, KBUILD_MODNAME)) {
			pr_warn("alloc_chrdev_region() for '%s' failed\n",
				base->name);
			return -1;
		}

		base->class = class_create(THIS_MODULE, base->name);
		if (!base->class) {
			pr_warn("Create device class '%s' failed\n",
				base->name);
			unregister_chrdev_region(base->dev, base->count);
			return -1;
		}
	}
	return 0;
}

static void ccat_class_exit(struct ccat_class *base)
{
	if (!atomic_dec_return(&base->instances)) {
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

loff_t ccat_cdev_llseek(struct file * f, loff_t offset, int whence)
{
	struct cdev_buffer *buffer = f->private_data;
	const size_t iosize = buffer->ccdev->iosize;

	return fixed_size_llseek(f, offset, whence, iosize);
}

EXPORT_SYMBOL(ccat_cdev_llseek);

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

EXPORT_SYMBOL(ccat_cdev_open);

int ccat_cdev_probe(struct ccat_function *func, struct ccat_class *cdev_class,
		    size_t iosize, void *user)
{
	struct ccat_cdev *const ccdev = alloc_ccat_cdev(cdev_class);
	if (!ccdev) {
		return -ENOMEM;
	}

	ccdev->ioaddr = func->ccat->bar_0 + func->info.addr;
	ccdev->iosize = iosize;
	ccdev->func = func;
	ccdev->user = user;
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

EXPORT_SYMBOL(ccat_cdev_probe);

int ccat_cdev_release(struct inode *const i, struct file *const f)
{
	const struct cdev_buffer *const buf = f->private_data;
	struct ccat_cdev *const ccdev = buf->ccdev;

	kfree(f->private_data);
	atomic_inc(&ccdev->in_use);
	return 0;
}

EXPORT_SYMBOL(ccat_cdev_release);

int ccat_cdev_remove(struct platform_device *pdev)
{
	struct ccat_function *const func = pdev->dev.platform_data;
	struct ccat_cdev *const ccdev = func->private_data;

	cdev_del(&ccdev->cdev);
	device_destroy(ccdev->class->class, ccdev->dev);
	free_ccat_cdev(ccdev);
	return 0;
}

EXPORT_SYMBOL(ccat_cdev_remove);

static int ccat_function_connect(struct ccat_function
				 *const func, struct ccat_device *const ccatdev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ccat_cells); ++i) {
		if (func->info.type == ccat_cells[i].type) {
			ccat_cells[i].cell.platform_data = func;
			ccat_cells[i].cell.pdata_size = sizeof(*func);
			return mfd_add_devices(ccatdev->dev,
					       PLATFORM_DEVID_AUTO,
					       &ccat_cells[i].cell, 1, NULL, 0,
					       NULL);
		}
	}
	return 0;
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
	int ret = 0;

	for (; addr < end && next; addr += block_size) {
		memcpy_fromio(&next->info, addr, sizeof(next->info));
		if (CCATINFO_NOTUSED != next->info.type) {
			next->ccat = ccatdev;
			ret = ccat_function_connect(next, ccatdev);
			if (ret < 0) {
				return ret;
			}
			next = kzalloc(sizeof(*next), GFP_KERNEL);
		}
	}
	kfree(next);
	return 0;
}

#ifdef CONFIG_PCI
static int ccat_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ccat_device *ccatdev;
	u8 rev;
	int status;

	ccatdev = devm_kzalloc(&pdev->dev, sizeof(*ccatdev), GFP_KERNEL);
	if (!ccatdev) {
		pr_err("%s() out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	ccatdev->pdev = pdev;
	ccatdev->dev = &pdev->dev;
	pci_set_drvdata(pdev, ccatdev);

	status = pci_enable_device_mem(pdev);
	if (status) {
		pr_err("enable %s failed: %d\n", pdev->dev.kobj.name, status);
		return status;
	}

	status = pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
	if (status) {
		pr_err("read CCAT pci revision failed with %d\n", status);
		goto disable_device;
	}

	status = pci_request_regions(pdev, KBUILD_MODNAME);
	if (status) {
		pr_err("allocate mem_regions failed.\n");
		goto disable_device;
	}

	status = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (status) {
		status =
		    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (status) {
			pr_err("No suitable DMA available, pci rev: %u\n", rev);
			goto release_regions;
		}
		pr_debug("32 bit DMA supported, pci rev: %u\n", rev);
	} else {
		pr_debug("64 bit DMA supported, pci rev: %u\n", rev);
	}

	ccatdev->bar_0 = pci_iomap(pdev, 0, 0);
	if (!ccatdev->bar_0) {
		pr_err("initialization of bar0 failed.\n");
		status = -EIO;
		goto release_regions;
	}

	ccatdev->bar_2 = pci_iomap(pdev, 2, 0);
	if (!ccatdev->bar_2) {
		pr_warn("initialization of optional bar2 failed.\n");
	}

	pci_set_master(pdev);
	if (ccat_functions_init(ccatdev)) {
		pr_warn("some functions couldn't be initialized\n");
	}
	return 0;

release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
	return status;
}

static void ccat_pci_remove(struct pci_dev *pdev)
{
	struct ccat_device *ccatdev = pci_get_drvdata(pdev);

	if (ccatdev) {
		mfd_remove_devices(ccatdev->dev);
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

#else /* #ifdef CONFIG_PCI */
static const size_t CCAT_EIM_ADDR = 0xf0000000;
static const size_t CCAT_EIM_LEN = 0x02000000;

static int ccat_eim_probe(struct platform_device *pdev)
{
	struct ccat_device *ccatdev;

	ccatdev = devm_kzalloc(&pdev->dev, sizeof(*ccatdev), GFP_KERNEL);
	if (!ccatdev) {
		pr_err("%s() out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	ccatdev->pdev = pdev;
	ccatdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, ccatdev);

	if (!request_mem_region(CCAT_EIM_ADDR, CCAT_EIM_LEN, pdev->name)) {
		pr_warn("request mem region failed.\n");
		return -EIO;
	}

	if (!(ccatdev->bar_0 = ioremap(CCAT_EIM_ADDR, CCAT_EIM_LEN))) {
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
		mfd_remove_devices(ccatdev->dev);
		iounmap(ccatdev->bar_0);
		release_mem_region(CCAT_EIM_ADDR, CCAT_EIM_LEN);
	}
	return 0;
}

static const struct of_device_id bhf_eim_ccat_ids[] = {
	{.compatible = "bhf,emi-ccat",},
	{}
};

MODULE_DEVICE_TABLE(of, bhf_eim_ccat_ids);

static struct platform_driver ccat_eim_driver = {
	.driver = {
		   .name = KBUILD_MODNAME,
		   .of_match_table = bhf_eim_ccat_ids,
		   },
	.probe = ccat_eim_probe,
	.remove = ccat_eim_remove,
};

module_platform_driver(ccat_eim_driver);
#endif /* #ifdef CONFIG_PCI */
