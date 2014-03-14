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

#include <linux/kernel.h>
#include <linux/pci.h>
#include "CCatDefinitions.h"

#define DRV_NAME         "ccat_eth"
#define DRV_EXTRAVERSION ""
#define DRV_VERSION      "0.3" DRV_EXTRAVERSION
#define DRV_DESCRIPTION  "Beckhoff CCAT Ethernet/EtherCAT Network Driver"

struct ccat_bar {
	unsigned long start;
	unsigned long end;
	unsigned long len;
	unsigned long flags;
	void __iomem *ioaddr;
};

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

struct ccat_eth_frame {
	uint32_t reserved1;
	uint32_t received:1;
	uint32_t reserved2:31;
	uint16_t length;
	uint16_t reserved3;
	uint32_t sent:1;
	uint32_t reserved4:31;
	uint64_t timestamp;
	uint8_t data[0x800 - 3 * sizeof(uint64_t)];
};

struct ccat_eth_register {
	void __iomem *mii;
	void __iomem *tx_fifo;
	void __iomem *rx_fifo;
	void __iomem *mac;
	void __iomem *rx_mem;
	void __iomem *tx_mem;
	void __iomem *misc;
};

struct ccat_eth_dma_fifo {
	void (*add) (struct ccat_eth_frame *, struct ccat_eth_dma_fifo *);
	void __iomem *reg;
	struct ccat_dma dma;
};

struct ccat_device {
	struct pci_dev *pdev;
	struct ccat_eth_priv *ethdev;
	struct ccat_bar bar[3];
};

struct ccat_eth_priv {
	const struct ccat_device *ccatdev;
	struct net_device *netdev;
	struct task_struct *poll_thread;	/* since there are no IRQs we pool things like "link state" */
	struct task_struct *rx_thread;	/* housekeeper for rx dma descriptors */
	struct task_struct *tx_thread;	/* housekeeper for tx dma descriptors */
	const struct ccat_eth_frame *next_tx_frame;	/* next frame the tx_thread should check for availability */
	CCatInfoBlock info;
	struct ccat_eth_register reg;
	struct ccat_eth_dma_fifo rx_fifo;
	struct ccat_eth_dma_fifo tx_fifo;
	atomic64_t rx_bytes;
	atomic64_t rx_dropped;
	atomic64_t tx_bytes;
	atomic64_t tx_dropped;
};
#endif /* #ifndef _CCAT_H_ */
