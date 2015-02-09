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

#ifndef _CCAT_H_
#define _CCAT_H_

#include <linux/cdev.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#define DRV_EXTRAVERSION ""
#define DRV_VERSION      "0.12" DRV_EXTRAVERSION
#define DRV_DESCRIPTION  "Beckhoff CCAT Ethernet/EtherCAT Network Driver"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

extern struct ccat_driver eth_driver;
extern struct ccat_driver gpio_driver;
extern struct ccat_driver sram_driver;
extern struct ccat_driver update_driver;

/**
 * CCAT function type identifiers (u16)
 */
enum ccat_info_t {
	CCATINFO_NOTUSED = 0,
	CCATINFO_GPIO = 0xd,
	CCATINFO_EPCS_PROM = 0xf,
	CCATINFO_ETHERCAT_MASTER_DMA = 0x14,
	CCATINFO_SRAM = 0x16,
};

struct ccat_cdev {
	atomic_t in_use;
	void __iomem *ioaddr;
	dev_t dev;
	struct cdev cdev;
	struct class *class;
};

/**
 * struct ccat_class - helper to register character device classes for CCAT functions
 */
struct ccat_class {
	dev_t dev;
	struct class *class;
	const unsigned count;
	struct ccat_cdev *devices;
};


extern struct ccat_cdev *alloc_ccat_cdev(struct ccat_class *base);
extern void free_ccat_cdev(struct ccat_cdev *ccdev);
extern int ccat_cdev_probe(struct cdev *cdev, dev_t dev, struct class *class, struct file_operations *fops);
extern void ccat_cdev_remove(struct ccat_cdev *ccdev);

extern int ccat_class_init(struct ccat_class *base, const char *name);
extern void ccat_class_exit(struct ccat_class *base);

/**
 * struct ccat_dma - CCAT DMA channel configuration
 * @phys: device-viewed address(physical) of the associated DMA memory
 * @virt: CPU-viewed address(virtual) of the associated DMA memory
 * @size: number of bytes in the associated DMA memory
 * @channel: CCAT DMA channel number
 * @dev: valid struct device pointer
 */
struct ccat_dma {
	dma_addr_t phys;
	void *virt;
	size_t size;
	size_t channel;
	struct device *dev;
};

extern void ccat_dma_free(struct ccat_dma *const dma);
extern int ccat_dma_init(struct ccat_dma *const dma, size_t channel,
			 void __iomem * const ioaddr, struct device *const dev);

/**
 * struct ccat_device - CCAT device representation
 * @pdev: pointer to the pci object allocated by the kernel
 * @bar_0: holding information about PCI BAR 0
 * @bar_2: holding information about PCI BAR 2 (optional)
 * @functions: list of available (driver loaded) FPGA functions
 *
 * One instance of a ccat_device should represent a physical CCAT. Since
 * a CCAT is implemented as FPGA the available functions can vary.
 */
struct ccat_device {
	struct pci_dev *pdev;
	void __iomem *bar_0;
	void __iomem *bar_2;
	struct list_head functions;
};

struct ccat_info_block {
	u16 type;
	u16 rev;
	union {
		u32 config;
		u8 num_gpios;
		struct {
			u8 tx_dma_chan;
			u8 rx_dma_chan;
		};
		struct {
			u8 sram_width;
			u8 sram_size;
			u16 reserved;
		};
	};
	u32 addr;
	u32 size;
};

struct ccat_function {
	const struct ccat_driver *drv;
	struct ccat_device *ccat;
	struct ccat_info_block info;
	struct list_head list;
	void *private_data;
};

/**
 * struct ccat_driver - CCAT FPGA function
 * @probe: add device instance
 * @remove: remove device instance
 * @type: type of the FPGA function supported by this driver
 */
struct ccat_driver {
	void (*exit) (void);
	int (*init) (void);
	int (*probe) (struct ccat_function * func);
	void (*remove) (struct ccat_function * drv);
	enum ccat_info_t type;
};
#endif /* #ifndef _CCAT_H_ */
