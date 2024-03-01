// SPDX-License-Identifier: MIT
/**
	IRQ Driver for Beckhoff CCAT FPGA irq
	Copyright (C) 2024 DLR e.V.
	Author: Robert Burger <robert.burger@dlr.de>
*/

// vim: set noexpandtab

#include "module.h"
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Robert Burger <robert.burger@dlr.de>");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRV_VERSION);

struct ccat_irq {
	char name[16];
	int irq_num;
	wait_queue_head_t ir_queue;     // Interrupt wait queue.
};

#define CCAT_IRQ_FUNCTION_INFO					((uint16_t)0x0001u)
#define CCAT_IRQ_FUNCTION_ESC					((uint16_t)0x0002u) // slot 1
#define CCAT_IRQ_FUNCTION_SYSTEM_TIME			((uint16_t)0x0010u) // slot 6
#define CCAT_IRQ_FUNCTION_IRQ					((uint16_t)0x0011u) // slot 8
#define CCAT_IRQ_FUNCTION_EPSC_PROM				((uint16_t)0x000Fu) // slot 10
#define CCAT_IRQ_FUNCTION_EEPROM				((uint16_t)0x0012u) // slot 11
#define CCAT_IRQ_FUNCTION_SRAM					((uint16_t)0x0016u) // slot 13

#define CCAT_IRQ_FUNCTION_IRQ__SLOT_N(n)		((uint16_t)1u << n)
#define CCAT_IRQ_FUNCTION_IRQ__SLOT				(CCAT_IRQ_FUNCTION_IRQ__SLOT_N(1)) 

#define CCAT_IRQ_FUNCTION_IRQ__STATUS_REG		((uint32_t)0x0u)	// 2 byte / 1 word
#define CCAT_IRQ_FUNCTION_IRQ__CONTROL_REG		((uint32_t)0x8u)	// 2 byte / 1 word

#define CCAT_IRQ_GLOBAL_IRQ_STATUS_REG			((uint32_t)0x40u) // 1 byte
#define CCAT_IRQ_GLOBAL_IRQ_ENABLE_REG			((uint32_t)0x50u) // 1 byte

#define CCAT_IRQ_GLOBAL_IRQ_ENABLE				((uint32_t)0x80u)

#define CCAT_IRQ_DEVICES_MAX 4

static uint16_t ccat_irq_get_slot_irq_stat(struct ccat_function *func) 
{
	return ioread16(func->ccat->bar_0 + func->info.addr + CCAT_IRQ_FUNCTION_IRQ__STATUS_REG);
}

static void ccat_irq_set_slot_irq_ctrl(struct ccat_function *func, uint16_t ctrl) 
{
	iowrite16(ctrl, func->ccat->bar_0 + func->info.addr + CCAT_IRQ_FUNCTION_IRQ__CONTROL_REG);
}

static uint8_t ccat_irq_get_global_irq_stat(struct ccat_function *func)
{
	return ioread8(func->ccat->bar_2 + CCAT_IRQ_GLOBAL_IRQ_STATUS_REG);
}

static void ccat_irq_set_global_irq_ctrl(struct ccat_function *func, uint8_t ctrl)
{
	iowrite8(ctrl, func->ccat->bar_2 + CCAT_IRQ_GLOBAL_IRQ_ENABLE_REG);
}

int ccat_irq_use_msi = 0;
module_param(ccat_irq_use_msi, int, 0);
MODULE_PARM_DESC(ccat_irq_use_msi, "Use MSI interrupts instead of legacy PCI");

static irqreturn_t ccat_irq_int_handler(int int_no, void *arg) {
	struct cdev_buffer *buffer = (struct cdev_buffer *)arg;
	struct ccat_irq *irq = (struct ccat_irq *)(buffer->ccdev->user);
	irqreturn_t ret = IRQ_NONE;
	uint32_t global_state = ccat_irq_get_global_irq_stat(buffer->ccdev->func);

	if (ccat_irq_use_msi != 0) {
		// no need to check here, it's 100% sure it is ours

		// disable here until interrupt source is processed.
		// will be re-enable in poll
		ccat_irq_set_slot_irq_ctrl(buffer->ccdev->func, 0);
		wake_up(&irq->ir_queue);

		ret = IRQ_HANDLED;
	} else {
		if (global_state & 0x80) {
			if (ccat_irq_get_slot_irq_stat(buffer->ccdev->func) & CCAT_IRQ_FUNCTION_IRQ__SLOT) {
				// disable here until interrupt source is processed.
				// will be re-enable in poll
				ccat_irq_set_slot_irq_ctrl(buffer->ccdev->func, 0);
				wake_up(&irq->ir_queue);

				ret = IRQ_HANDLED;
			}
		}
	}

	return ret;
}

static int ccat_irq_open(struct inode *const i, struct file *const f)
{
	struct ccat_cdev *ccdev =
	    container_of(i->i_cdev, struct ccat_cdev, cdev);
	struct ccat_irq *irq = (struct ccat_irq *)(ccdev->user);
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

	if (ccat_irq_use_msi == 1) {
		int num_vecs = pci_alloc_irq_vectors(ccdev->func->ccat->pdev, 1, 1, PCI_IRQ_ALL_TYPES);
		if (num_vecs < 0 ) {
			pr_err("Allocating IRQ vectors failed\n");
		} else {
			int r, irq_success = 1;
			pr_info("Got %d IRQ vectors\n", num_vecs);

			for (r = 0; r < num_vecs; ++r) {
				irq->irq_num = pci_irq_vector(ccdev->func->ccat->pdev, r);
				pr_info("Interrupt %d has been reserved, using irq name %s\n", irq->irq_num, &irq->name[0]);

				if (request_irq(irq->irq_num, ccat_irq_int_handler, IRQF_NO_THREAD, &irq->name[0], buf)) { 
					pr_err("Interrupt %d reqeust failed!\n", irq->irq_num);
					irq_success = 0;
				}
			}

			if (irq_success == 0) {
				pci_disable_device(ccdev->func->ccat->pdev);

				return -EBUSY;
			} else {
				// disable all Interrupt slots
				ccat_irq_set_slot_irq_ctrl(ccdev->func, 0); 
				ccat_irq_set_global_irq_ctrl(ccdev->func, CCAT_IRQ_GLOBAL_IRQ_ENABLE);
			}
		}
	} else {
		irq->irq_num = ((struct pci_dev *)(ccdev->func->ccat->pdev))->irq;

		pr_info("Interrupt %d has been reserved, using irq name %s\n", irq->irq_num, &irq->name[0]);

		if (request_irq(irq->irq_num, ccat_irq_int_handler, IRQF_SHARED, &irq->name[0], buf)) { 
			pci_disable_device(ccdev->func->ccat->pdev);

			pr_err("Interrupt %d isn't free\n", irq->irq_num);
			return -EBUSY;
		} else {
			// disable all Interrupt slots
			ccat_irq_set_slot_irq_ctrl(ccdev->func, 0); 
			ccat_irq_set_global_irq_ctrl(ccdev->func, CCAT_IRQ_GLOBAL_IRQ_ENABLE);
		}
	}
	
	return 0;
}

static int ccat_irq_release(struct inode *const i, struct file *const f)
{
	struct cdev_buffer *const buf = f->private_data;
	struct ccat_cdev *const ccdev = buf->ccdev;
	struct ccat_irq *irq = (struct ccat_irq *)(ccdev->user);

	ccat_irq_set_global_irq_ctrl(ccdev->func, 0);

	if (ccat_irq_use_msi == 1) {
		// disable Interrupt (see FC1121 Application Notes 3.2.1)
		free_irq(irq->irq_num, buf);
		pci_free_irq_vectors(ccdev->func->ccat->pdev);
	} else {
		free_irq(irq->irq_num, buf);
	}

	kfree(f->private_data);
	atomic_inc(&ccdev->in_use);
	return 0;
}

static unsigned int ccat_irq_poll(struct file *f, struct poll_table_struct *poll_table) 
{
	struct cdev_buffer *buffer = f->private_data;
	struct ccat_irq *irq = (struct ccat_irq *)(buffer->ccdev->user);

	// status of slot 1 (see FC1121 Application Notes 3.2.1)
	if (ccat_irq_get_slot_irq_stat(buffer->ccdev->func) & CCAT_IRQ_FUNCTION_IRQ__SLOT) {
		return DEFAULT_POLLMASK;
	}
	
	ccat_irq_set_slot_irq_ctrl(buffer->ccdev->func, CCAT_IRQ_FUNCTION_IRQ__SLOT);
	poll_wait(f, &irq->ir_queue, poll_table);
	return 0;
}

static struct ccat_cdev dev_table[CCAT_IRQ_DEVICES_MAX];
static struct ccat_class cdev_class = {
	.instances = {0},
	.count = CCAT_IRQ_DEVICES_MAX,
	.devices = dev_table,
	.name = "ccat_irq",
	.fops = {
		 .owner = THIS_MODULE,
		 .open = ccat_irq_open,
		 .release = ccat_irq_release,
		 .poll = ccat_irq_poll,
		 },
};

static int ccat_irq_probe(struct platform_device *pdev)
{
	struct ccat_function *const func = pdev->dev.platform_data;
	struct ccat_irq *const irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	int ret = 0;

	if (!irq)
		return -ENOMEM;
                        
	// init wait queue
	init_waitqueue_head(&irq->ir_queue);

	pr_info("%s: 0x%04x rev: 0x%04x, addr: 0x%X, size: 0x%X\n", __FUNCTION__, 
			func->info.type, func->info.rev, func->info.addr, func->info.size);
	
	ret = ccat_cdev_probe(func, &cdev_class, func->info.size, irq);

	if (ret == 0) {
		struct ccat_cdev *ccdev = (struct ccat_cdev *)func->private_data;
		snprintf(&irq->name[0], 16, "esc%d", MINOR(ccdev->dev));
	}

	return ret;
}

static struct platform_driver irq_driver = {
	.driver = {.name = "ccat_irq"},
	.probe = ccat_irq_probe,
	.remove = ccat_cdev_remove, // TODO release ir_queue
};

module_platform_driver(irq_driver);


