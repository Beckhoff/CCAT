// SPDX-License-Identifier: MIT
/**
    ESC Driver for Beckhoff CCAT FPGA ESCs
    Copyright (C) 2024 DLR e.V.
    Author: Robert Burger <robert.burger@dlr.de>
*/

// vi: set noexpandtab:

#include "module.h"
#include "sram.h"
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Robert Burger <robert.burger@dlr.de>");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRV_VERSION);

#define CCAT_ESC_DEVICES_MAX 4

static int ccat_esc_mmap(struct file *f, struct vm_area_struct *vma) 
{
	struct cdev_buffer *const buffer = f->private_data;
	struct pci_dev *pdev = (struct pci_dev *)(buffer->ccdev->func->ccat->pdev);

	vma->vm_pgoff = (pci_resource_start(pdev, 0) + buffer->ccdev->func->info.addr) >> PAGE_SHIFT;

	return remap_pfn_range(
			vma, 
			vma->vm_start, 
			vma->vm_pgoff,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static struct ccat_cdev dev_table[CCAT_ESC_DEVICES_MAX];
static struct ccat_class cdev_class = {
	.instances = {0},
	.count = CCAT_ESC_DEVICES_MAX,
	.devices = dev_table,
	.name = "ccat_esc",
	.fops = {
		 .owner = THIS_MODULE,
		 .llseek = ccat_cdev_llseek,
		 .open = ccat_cdev_open,
		 .release = ccat_cdev_release,
		 .read = ccat_sram_read,
		 .write = ccat_sram_write,
		 .mmap = ccat_esc_mmap,
		 },
};

static int ccat_esc_probe(struct platform_device *pdev)
{
	struct ccat_function *const func = pdev->dev.platform_data;

	pr_info("%s: 0x%04x rev: 0x%04x, addr: 0x%X, size: 0x%X\n", __FUNCTION__, 
			func->info.type, func->info.rev, func->info.addr, func->info.size);
	
	return ccat_cdev_probe(func, &cdev_class, func->info.size, NULL);
}

static struct platform_driver esc_driver = {
	.driver = {.name = "ccat_esc"},
	.probe = ccat_esc_probe,
	.remove = ccat_cdev_remove,
};

module_platform_driver(esc_driver);

