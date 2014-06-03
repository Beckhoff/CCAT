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
#include "CCatDefinitions.h"

#define DRV_EXTRAVERSION ""
#define DRV_VERSION      "0.9" DRV_EXTRAVERSION
#define DRV_DESCRIPTION  "Beckhoff CCAT Ethernet/EtherCAT Network Driver"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/**
 * struct ccat_bar - CCAT PCI Base Address Register(BAR) configuration
 * @start: start address of this BAR
 * @end: end address of this BAR
 * @len: length of this BAR
 * @flags: flags set on this BAR
 * @ioaddr: ioremapped address of this bar
 */
struct ccat_bar {
	unsigned long start;
	unsigned long end;
	unsigned long len;
	unsigned long flags;
	void __iomem *ioaddr;
};

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
 * struct ccat_eth_frame - Ethernet frame with DMA descriptor header in front
 * @reservedn: is not used and should always be set to 0
 * @received: used for reception, is set to 1 by the CCAT when data was written
 * @length: number of bytes in the frame including the DMA header
 * @sent: is set to 1 by the CCAT when data was transmitted
 * @timestamp: a 64 bit EtherCAT timestamp
 * @data: the bytes of the ethernet frame
 */
struct ccat_eth_frame {
	u32 reserved1;
	u32 received:1;
	u32 reserved2:31;
	u16 length;
	u16 reserved3;
	u32 sent:1;
	u32 reserved4:31;
	u64 timestamp;
	u8 data[0x800 - 3 * sizeof(u64)];
};

/**
 * struct ccat_eth_register - CCAT register addresses in the PCI BAR
 * @mii: address of the CCAT management interface register
 * @tx_fifo: address of the CCAT TX DMA fifo register
 * @rx_fifo: address of the CCAT RX DMA fifo register
 * @mac: address of the CCAT media access control register
 * @rx_mem: address of the CCAT register holding the RX DMA address
 * @tx_mem: address of the CCAT register holding the TX DMA address
 * @misc: address of a CCAT register holding miscellaneous information
 */
struct ccat_eth_register {
	void __iomem *mii;
	void __iomem *tx_fifo;
	void __iomem *rx_fifo;
	void __iomem *mac;
	void __iomem *rx_mem;
	void __iomem *tx_mem;
	void __iomem *misc;
};

/**
 * struct ccat_eth_dma_fifo - CCAT RX or TX DMA fifo
 * @add: callback used to add a frame to this fifo
 * @reg: PCI register address of this DMA fifo
 * @dma: information about the associated DMA memory
 */
struct ccat_eth_dma_fifo {
	void (*add) (struct ccat_eth_frame *, struct ccat_eth_dma_fifo *);
	void __iomem *reg;
	struct ccat_dma dma;
};

/**
 * struct ccat_device - CCAT device representation
 * @pdev: pointer to the pci object allocated by the kernel
 * @ethdev: CCAT Ethernet/EtherCAT Master (with DMA) function, NULL if function is not available or failed to initialize
 * @update: CCAT Update function, NULL if function is not available or failed to initialize
 * @bar [0] and [2] holding information about PCI BARs 0 and 2.
 *
 * One instance of a ccat_device should represent a physical CCAT. Since
 * a CCAT is implemented as FPGA the available functions can vary so
 * the function object pointers can be NULL.
 * Extra note: you will recognize that PCI BAR1 is not used and is a
 * waste of memory, thats true but right now, its very easy to use it
 * this way. So we might optimize it later.
 */
struct ccat_device {
	struct pci_dev *pdev;
	struct ccat_eth_priv *ethdev;
	struct ccat_update *update;
	struct ccat_bar bar[3];	//TODO optimize this
};

/**
 * struct ccat_eth_priv - CCAT Ethernet/EtherCAT Master function (netdev)
 * @ccatdev: pointer to the parent struct ccat_device
 * @netdev: the net_device structure used by the kernel networking stack
 * @next_tx_frame: pointer to the next TX DMA descriptor, which the tx_thread should check for availablity
 * @info: holds a copy of the CCAT Ethernet/EtherCAT Master function information block (read from PCI config space)
 * @reg: register addresses in PCI config space of the Ethernet/EtherCAT Master function
 * @rx_fifo: DMA fifo used for RX DMA descriptors
 * @tx_fifo: DMA fifo used for TX DMA descriptors
 * @poll_timer: interval timer used to poll CCAT for events like link changed, rx done, tx done
 * @rx_bytes: number of bytes received -> reported with ndo_get_stats64()
 * @rx_dropped: number of received frames, which were dropped -> reported with ndo_get_stats64()
 * @tx_bytes: number of bytes send -> reported with ndo_get_stats64()
 * @tx_dropped: number of frames requested to send, which were dropped -> reported with ndo_get_stats64()
 */
struct ccat_eth_priv {
	const struct ccat_device *ccatdev;
	struct net_device *netdev;
	const struct ccat_eth_frame *next_tx_frame;
	CCatInfoBlock info;
	struct ccat_eth_register reg;
	struct ccat_eth_dma_fifo rx_fifo;
	struct ccat_eth_dma_fifo tx_fifo;
	struct hrtimer poll_timer;
	atomic64_t rx_bytes;
	atomic64_t rx_dropped;
	atomic64_t tx_bytes;
	atomic64_t tx_dropped;
};

/**
 * struct ccat_update - CCAT Update function (update)
 * @ccatdev: pointer to the parent struct ccat_device
 * @ioaddr: PCI base address of the CCAT Update function
 * dev: device number for this update function
 * cdev: character device used for the CCAT Update function
 * class: pointer to a device class used when registering the CCAT Update device
 * @info: holds a copy of the CCAT Update function information block (read from PCI config space)
 */
struct ccat_update {
	struct kref refcount;
	void __iomem *ioaddr;
	dev_t dev;
	struct cdev cdev;
	struct class *class;
	CCatInfoBlock info;
};
#endif /* #ifndef _CCAT_H_ */
