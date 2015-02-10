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
#include <linux/fs.h>
#include <linux/uaccess.h>

#define CCAT_SRAM_DEVICES_MAX 4
static struct ccat_cdev dev_table[CCAT_SRAM_DEVICES_MAX];
static struct ccat_class cdev_class = {
	.count = CCAT_SRAM_DEVICES_MAX,
	.devices = dev_table,
	.name = "ccat_sram",
};


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

/**
 * ccat_update_read() - Read CCAT configuration data from flash
 * @f: file handle previously initialized with ccat_update_open()
 * @buf: buffer in user space provided for our data
 * @len: length of the user space buffer
 * @off: current offset of our file operation
 *
 * Copies data from the CCAT FPGA's configuration flash to user space.
 * Note that the size of the FPGA's firmware is not known exactly so it
 * is very possible that the overall buffer ends with a lot of 0xff.
 *
 * Return: the number of bytes written, or 0 if EOF reached
 */
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

/**
 * ccat_update_write() - Write data to the CCAT FPGA's configuration flash
 * @f: file handle previously initialized with ccat_update_open()
 * @buf: buffer in user space providing the new configuration data (from *.rbf)
 * @len: length of the user space buffer
 * @off: current offset in the configuration data
 *
 * Copies data from user space (possibly a *.rbf) to the CCAT FPGA's
 * configuration flash.
 *
 * Return: the number of bytes written, or 0 if flash end is reached
 */
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

static struct file_operations sram_ops = {
	.owner = THIS_MODULE,
	.open = ccat_sram_open,
	.release = ccat_sram_release,
	.read = ccat_sram_read,
	.write = ccat_sram_write,
};

static int ccat_sram_probe(struct ccat_function *func)
{
	static const u8 NO_SRAM_CONNECTED = 0;
	const u8 type = func->info.sram_width & 0x3;
	struct ccat_cdev *ccdev;

	pr_info("%s: 0x%04x rev: 0x%04x\n", __FUNCTION__, func->info.type, func->info.rev);
	if (type == NO_SRAM_CONNECTED) {
		return -ENODEV;
	}

	ccdev = alloc_ccat_cdev(&cdev_class);
	if (!ccdev) {
		return -ENOMEM;
	}

	ccdev->ioaddr = func->ccat->bar_0 + func->info.addr;
	ccdev->iosize = (1 << func->info.sram_size);
	atomic_set(&ccdev->in_use, 1);

	if (ccat_cdev_probe
	    (&ccdev->cdev, ccdev->dev, cdev_class.class, &sram_ops)) {
		pr_warn("ccat_cdev_probe() failed\n");
		free_ccat_cdev(ccdev);
		return -1;
	}
	ccdev->class = cdev_class.class;
	func->private_data = ccdev;

	pr_info("%dBit sram connected with %zu kbytes capacity @%p\n", 8*type, ccdev->iosize/1024, ccdev->ioaddr);
	return 0;
}

static void ccat_sram_remove(struct ccat_function *func)
{
	struct ccat_cdev *const ccdev = func->private_data;
	ccat_cdev_remove(ccdev);
}

struct ccat_driver sram_driver = {
	.type = CCATINFO_SRAM,
	.probe = ccat_sram_probe,
	.remove = ccat_sram_remove,
	.cdev_class = &cdev_class,
};
