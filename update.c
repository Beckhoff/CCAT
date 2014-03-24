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

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "ccat.h"
#include "print.h"
#include "update.h"

#define CCAT_DATA_IN_4 0x038
#define CCAT_DATA_IN_N 0x7F0
#define CCAT_DATA_BLOCK_SIZE (size_t)((CCAT_DATA_IN_N - CCAT_DATA_IN_4)/8)
#define CCAT_FLASH_SIZE (size_t)0xDE1F1
#define CCAT_GET_PROM_ID 0xD5
#define CCAT_GET_PROM_ID_CLOCKS 40
#define CCAT_READ_FLASH  0xC0
#define CCAT_READ_FLASH_CLOCKS 32

/* from http://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith32Bits */
#define SWAP_BITS(B) \
	((((B) * 0x0802LU & 0x22110LU) | ((B) * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16)

static int ccat_read_flash(const struct ccat_update *update, uint32_t addr, uint16_t len, char __user *buf);

static int ccat_update_open(struct inode *const i, struct file *const f)
{
	pr_info("%s()\n", __FUNCTION__);
	f->private_data = container_of(i->i_cdev, struct ccat_update, cdev);
	return 0;
}

static int ccat_update_release(struct inode *const i, struct file *const f)
{
	pr_info("%s()\n", __FUNCTION__);
	return 0;
}

static int __ccat_update_read(const struct ccat_update *const update, char __user *buf, size_t len, loff_t *off)
{
	const size_t length = min(CCAT_DATA_BLOCK_SIZE, len);
	ccat_read_flash(update, *off, length, buf);
	*off += length;
	return length;
}

/**
 * ccat_update_read() - Read CCAT configuration data from flash
 * //TODO
 * A maximum of CCAT_DATA_BLOCK_SIZE bytes are read. To read the full
 * CCAT flash repeat calling this function until it returns 0, which
 * indicates EOF.
 */
static int ccat_update_read(struct file *const f, char __user *buf, size_t len, loff_t *off)
{
	if(!buf || !off) {
		return -EINVAL;
	}
	if(*off >= CCAT_FLASH_SIZE) {
		return 0;
	}
	if(*off + len > CCAT_FLASH_SIZE) {
		len = CCAT_FLASH_SIZE - *off;
	}
	return __ccat_update_read(f->private_data, buf, len, off);
}

static int ccat_update_write(struct file *const f, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("%s()\n", __FUNCTION__);
	return len;
}

static struct file_operations update_ops = {
	.owner = THIS_MODULE,
	.open = ccat_update_open,
	.release = ccat_update_release,
	.read = ccat_update_read,
	.write = ccat_update_write,
};

static inline void ccat_update_cmd(void __iomem *const ioaddr, uint16_t clocks, uint8_t cmd)
{
	iowrite8((0xff00 & clocks) >> 8, ioaddr);
	iowrite8(0x00ff & clocks, ioaddr + 0x8);
	iowrite8(cmd, ioaddr + 0x10);
}

/**
 * ccat_get_prom_id() - Read CCAT PROM ID
 * @update: CCAT Update function object
 */
static uint8_t ccat_get_prom_id(const struct ccat_update *const update)
{
	ccat_update_cmd(update->ioaddr, CCAT_GET_PROM_ID_CLOCKS, CCAT_GET_PROM_ID);
	wmb();
	iowrite8(0xff, update->ioaddr + 0x7f8);
	wmb();
	/* wait until busy flag was reset */
	while(ioread8(update->ioaddr + 1));
	return ioread8(update->ioaddr + 0x38);
}

/**
 * ccat_read_flash() - Read bytes from CCAT flash
 * @update: CCAT Update function object
 * @len: number of bytes to read
 */
static int ccat_read_flash(const struct ccat_update *const update, const uint32_t addr, const uint16_t len, char __user *const buf)
{
	const uint16_t clocks = CCAT_READ_FLASH_CLOCKS + (8 * len);
	const uint8_t addr_0 = SWAP_BITS(addr & 0xff);
	const uint8_t addr_1 = SWAP_BITS((addr & 0xff00) >> 8);
	const uint8_t addr_2 = SWAP_BITS((addr & 0xff0000) >> 16);
	ccat_update_cmd(update->ioaddr, clocks, CCAT_READ_FLASH);
	iowrite8(addr_2, update->ioaddr + 0x18);
	iowrite8(addr_1, update->ioaddr + 0x20);
	iowrite8(addr_0, update->ioaddr + 0x28);
	wmb();
	iowrite8(0xff, update->ioaddr + 0x7f8);
	wmb();
	/* wait until busy flag was reset */
	while(ioread8(update->ioaddr + 1));
	if(buf) {
		uint16_t i;
		for(i = 0; i < len; i++) {
			const char tmp = ioread8(update->ioaddr + CCAT_DATA_IN_4 + 8*i);
			put_user(tmp, buf + i);
		}
	}
	return len;
}

struct ccat_update *ccat_update_init(const struct ccat_device *const ccatdev,
				    void __iomem * const addr)
{
	struct ccat_update *const update = kzalloc(sizeof(*update), GFP_KERNEL);
	if(!update) {
		return NULL;
	}
	update->ccatdev = ccatdev;
	update->ioaddr = ccatdev->bar[0].ioaddr + ioread32(addr + 0x8);
	memcpy_fromio(&update->info, addr, sizeof(update->info));
	print_update_info(&update->info);
	pr_info("     PROM ID is:   0x%x\n", ccat_get_prom_id(update));

	if(0x00 != update->info.nRevision) {
		pr_warn("CCAT Update rev. %d not supported\n", update->info.nRevision);
		goto cleanup;
	}

	if(alloc_chrdev_region(&update->dev, 0, 1, DRV_NAME)) {
		pr_warn("alloc_chrdev_region() failed\n");
		goto cleanup;
	}

	update->class = class_create(THIS_MODULE, "ccat_update");
	if(NULL == update->class) {
		pr_warn("Create device class failed\n");
		goto cleanup;
	}

	if(NULL == device_create(update->class, NULL, update->dev, NULL, "ccat_update")) {
		pr_warn("device_create() failed\n");
		goto cleanup;
	}

	cdev_init(&update->cdev, &update_ops);
	update->cdev.owner = THIS_MODULE;
	update->cdev.ops = &update_ops;
	if(cdev_add(&update->cdev, update->dev, 1)) {
		pr_warn("add update device failed\n");
		goto cleanup;
	}
	return update;
cleanup:
	device_destroy(update->class, update->dev);
	class_destroy(update->class);
	unregister_chrdev_region(update->dev, 1);
	kfree(update);
	return NULL;
}

void ccat_update_remove(struct ccat_update *update)
{
	cdev_del(&update->cdev);
	device_destroy(update->class, update->dev);
	class_destroy(update->class);
	unregister_chrdev_region(update->dev, 1);
	kfree(update);
	pr_info("%s(): done\n", __FUNCTION__);
}
