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

#include <asm/dma.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "ccat.h"
#include "netdev.h"

static void ccat_bar_free(struct ccat_bar *bar)
{
	const struct ccat_bar tmp = {
		.start = bar->start,
		.len = bar->len,
		.ioaddr = bar->ioaddr
	};
	memset(bar, 0, sizeof(*bar));
	iounmap(tmp.ioaddr);
	release_mem_region(tmp.start, tmp.len);
}

static int ccat_bar_init(struct ccat_bar *bar, size_t index, struct pci_dev *pdev)
{
	struct resource *res;
	bar->start = pci_resource_start(pdev, index);
	bar->end = pci_resource_end(pdev, index);
	bar->len = pci_resource_len(pdev, index);
	bar->flags = pci_resource_flags(pdev, index);
	if(!(IORESOURCE_MEM & bar->flags)) {
		printk(KERN_INFO "%s: bar%d should be memory space, but it isn't -> abort CCAT initialization.\n", DRV_NAME, index);
		return -EIO;
	}

	res = request_mem_region(bar->start, bar->len, DRV_NAME);
	if(!res) {
		printk(KERN_INFO "%s: allocate mem_region failed.\n", DRV_NAME);
		return -EIO;
	}
	printk(KERN_INFO "%s: bar%d at [%lx,%lx] len=%lu.\n", DRV_NAME, index, bar->start, bar->end, bar->len);
	printk(KERN_INFO "%s: bar%d mem_region resource allocated as %p.\n", DRV_NAME, index, res);

	bar->ioaddr = ioremap(bar->start, bar->len);
	if(!bar->ioaddr) {
		printk(KERN_INFO "%s: bar%d ioremap failed.\n", DRV_NAME, index);
		release_mem_region(bar->start, bar->len);
		return -EIO;
	}
	printk(KERN_INFO "%s: bar%d I/O mem mapped to %p.\n", DRV_NAME, index, bar->ioaddr);
	return 0;
}

static void ccat_dma_free(struct ccat_dma *const dma)
{
	const struct ccat_dma tmp = *dma;
	free_dma(dma->channel);
	memset(dma, 0, sizeof(*dma));
	dma_free_coherent(tmp.dev, tmp.size, tmp.virt, tmp.phys);
}

int ccat_dma_init(struct ccat_dma *const dma, size_t channel, void __iomem *const ioaddr, struct device *const dev)
{
	void *frame;
	uint64_t addr;
	uint32_t translateAddr;
	uint32_t memTranslate;
	uint32_t memSize;
	uint32_t data = 0xffffffff;
	uint32_t offset = (sizeof(uint64_t) * channel) + 0x1000;

	dma->channel = channel;
	dma->dev = dev;

	/* calculate size and alignments */
	iowrite32(data, ioaddr + offset);
	wmb();
	data = ioread32(ioaddr + offset);
	memTranslate = data & 0xfffffffc;
	memSize = (~memTranslate) + 1;
	dma->size = 2*memSize - PAGE_SIZE;
	dma->virt = dma_zalloc_coherent(dev, dma->size, &dma->phys, GFP_KERNEL);
	if(!dma->virt || !dma->phys) {
		printk(KERN_INFO "%s: init DMA%d memory failed.\n", DRV_NAME, channel);
		return -1;
	}

	if(request_dma(channel, DRV_NAME)) {
		printk(KERN_INFO "%s: request dma channel %d failed\n", DRV_NAME, channel);
		ccat_dma_free(dma);
		return -1;
	}

	translateAddr = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = translateAddr;
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
	frame = dma->virt + translateAddr - dma->phys;
	printk(KERN_INFO "%s: DMA%d mem initialized\n virt:         0x%p\n phys:         0x%llx\n translated:   0x%llx\n pci addr:     0x%08x%x\n memTranslate: 0x%x\n size:         %u bytes.\n", DRV_NAME, channel, dma->virt, (uint64_t)(dma->phys), addr, ioread32(ioaddr + offset + 4), ioread32(ioaddr + offset), memTranslate, dma->size);
	return 0;
}

static void ccat_remove_pci(struct ccat_eth_priv *priv)
{
	ccat_dma_free(&priv->tx_fifo.dma);
	ccat_dma_free(&priv->rx_fifo.dma);
	ccat_bar_free(&priv->bar[2]);
	ccat_bar_free(&priv->bar[0]);
	priv->pdev = NULL;
}

static void ccat_remove_one(struct pci_dev *pdev)
{
	struct net_device *const netdev = pci_get_drvdata(pdev);
	if(netdev) {
		struct ccat_eth_priv *const priv = netdev_priv(netdev);
		ccat_eth_remove(netdev);
		ccat_remove_pci(priv);
		free_netdev(netdev);
		pci_disable_device (pdev);
		printk(KERN_INFO "%s: cleanup done.\n\n", DRV_NAME);
	}
}

static int ccat_init_pci(struct ccat_eth_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	void __iomem *addr;
	size_t i;
	int status;
	uint8_t revision;
	uint8_t num_functions;
	status = pci_enable_device (pdev);
	if(status) {
		printk(KERN_INFO "%s: enable CCAT pci device %s failed with %d\n", DRV_NAME, pdev->dev.kobj.name, status);
		return status;
	}

	pci_set_master(pdev);
	status = pci_read_config_byte(pdev, PCI_REVISION_ID, &revision);
	if(status) {
		printk(KERN_INFO "%s: read CCAT pci revision failed with %d\n", DRV_NAME, status);
		return status;
	}

	/* FIXME upgrade to a newer kernel to get support of dma_set_mask_and_coherent()
	if (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64))) { */
	//if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		printk(KERN_INFO "%s: 64 bit DMA supported.\n", DRV_NAME);
	/*} else if (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32))) { */
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		printk(KERN_INFO "%s: 32 bit DMA supported.\n", DRV_NAME);
	} else {
		printk(KERN_WARNING "%s: No suitable DMA available.\n", DRV_NAME);
	}

	if(ccat_bar_init(&priv->bar[0], 0, priv->pdev)) {
		printk(KERN_WARNING "%s: initialization of bar0 failed.\n", DRV_NAME);
		return -EIO;
	}

	if(ccat_bar_init(&priv->bar[2], 2, priv->pdev)) {
		printk(KERN_WARNING "%s: initialization of bar2 failed.\n", DRV_NAME);
		return -EIO;
	}

	num_functions = ioread8(priv->bar[0].ioaddr + 4); /* jump to CCatInfoBlock.nMaxEntries */

	/* find CCATINFO_ETHERCAT_MASTER_DMA function */
	for(i = 0, addr = priv->bar[0].ioaddr; i < num_functions; ++i, addr += sizeof(priv->info)) {
		if(CCATINFO_ETHERCAT_MASTER_DMA == ioread16(addr)) {
			status = ccat_eth_init(priv, addr);
			break;
		}
	}
	return status;
}

static int ccat_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *netdev;
	struct ccat_eth_priv *priv;

	netdev = alloc_etherdev(sizeof(*priv));
	if(!netdev) {
		printk(KERN_INFO "%s: mem alloc failed.\n", DRV_NAME);
		return -ENOMEM;
	}
	priv = netdev_priv(netdev);
	priv->pdev = pdev;
	priv->netdev = netdev;
	pci_set_drvdata(pdev, netdev);

	/* pci initialization */
	if(ccat_init_pci(priv)) {
		printk(KERN_INFO "%s: CCAT pci init failed.\n", DRV_NAME);
		ccat_remove_one(priv->pdev);
		return -EIO;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	/* complete ethernet device initialization */
	if(ccat_eth_init_netdev(netdev)) {
		printk(KERN_INFO "%s: unable to register network device.\n", DRV_NAME);
		ccat_remove_one(pdev);
		return -EINVAL;
	}
	printk(KERN_INFO "%s: registered %s as network device.\n", DRV_NAME, netdev->name);
	return 0;
}

#define PCI_DEVICE_ID_BECKHOFF_CCAT 0x5000
#define PCI_VENDOR_ID_BECKHOFF 0x15EC

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BECKHOFF, PCI_DEVICE_ID_BECKHOFF_CCAT) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver pci_driver = {
	.name = DRV_NAME,
	.id_table = pci_ids,
	.probe = ccat_init_one,
	.remove = ccat_remove_one,
};

static void ccat_exit_module(void) {
	pci_unregister_driver(&pci_driver);
}

static int ccat_init_module(void) {
	BUILD_BUG_ON(sizeof(struct ccat_eth_frame) != sizeof(CCatDmaTxFrame));
	BUILD_BUG_ON(sizeof(struct ccat_eth_frame) != sizeof(CCatRxDesc));
	BUILD_BUG_ON(offsetof(struct ccat_eth_frame, data) != offsetof(CCatDmaTxFrame, data));
	BUILD_BUG_ON(offsetof(struct ccat_eth_frame, data) != offsetof(CCatRxDesc, data));
	pr_info("%s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	return pci_register_driver(&pci_driver);
}

module_exit(ccat_exit_module);
module_init(ccat_init_module);
