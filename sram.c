/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2015  Beckhoff Automation GmbH & Co. KG
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
#include <linux/fs.h>
#include <linux/uaccess.h>

#define CCAT_SRAM_DEVICES_MAX 4

static int ccat_sram_open(struct inode *const i, struct file *const f)
{
	struct ccat_cdev *ccdev =
	    container_of(i->i_cdev, struct ccat_cdev, cdev);
	    f->private_data = ccdev;
	pr_info("---->%s\n", __FUNCTION__);
	return 0;
}

static int ccat_sram_release(struct inode *const i, struct file *const f)
{
	pr_info("---->%s\n", __FUNCTION__);
	return 0;
}

static ssize_t ccat_sram_read(struct file *const f, char __user * buf,
				size_t len, loff_t * off)
{
	struct ccat_cdev *ccdev = f->private_data;
	u8 *kern_buf;

	if (*off >= ccdev->iosize) {
		return 0;
	}

	len = min(len, (size_t)(ccdev->iosize - *off));
	kern_buf = kzalloc(len, GFP_KERNEL);
	if (!kern_buf) {
		return -ENOMEM;
	}

	memcpy_fromio(kern_buf, ccdev->ioaddr + *off, len);
	copy_to_user(buf, kern_buf, len);
	*off += len;
	kfree(kern_buf);
	return len;
}

static ssize_t ccat_sram_write(struct file *const f, const char __user * buf,
				 size_t len, loff_t * off)
{
	struct ccat_cdev *ccdev = f->private_data;
	u8 *kern_buf;

	if (*off + len > ccdev->iosize) {
		return 0;
	}

	kern_buf = kzalloc(ccdev->iosize, GFP_KERNEL);
	if (!kern_buf) {
		return -ENOMEM;
	}

	copy_from_user(kern_buf, buf, len);
	memcpy_toio(ccdev->ioaddr + *off, kern_buf, len);
	*off += len;
	kfree(kern_buf);
	return len;
}

static struct ccat_cdev dev_table[CCAT_SRAM_DEVICES_MAX];
static struct ccat_class cdev_class = {
	.count = CCAT_SRAM_DEVICES_MAX,
	.devices = dev_table,
	.name = "ccat_sram",
	.fops = {
		.owner = THIS_MODULE,
		.open = ccat_sram_open,
		.release = ccat_sram_release,
		.read = ccat_sram_read,
		.write = ccat_sram_write,
	},
};

static int ccat_sram_probe(struct ccat_function *func)
{
	static const u8 NO_SRAM_CONNECTED = 0;
	const u8 type = func->info.sram_width & 0x3;
	const size_t iosize = (1 << func->info.sram_size);

	pr_info("%s: 0x%04x rev: 0x%04x\n", __FUNCTION__, func->info.type, func->info.rev);
	if (type == NO_SRAM_CONNECTED) {
		return -ENODEV;
	}
	return ccat_cdev_probe(func, &cdev_class, iosize);
}

struct ccat_driver sram_driver = {
	.type = CCATINFO_SRAM,
	.probe = ccat_sram_probe,
	.remove = ccat_cdev_remove,
	.cdev_class = &cdev_class,
};
