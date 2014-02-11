#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "CCatDefinitions.h"

#define DRV_NAME "ccat_eth"

struct ccat_eth_func_ptr {
	void *mii;
	void *tx_fifo;
	void *rx_fifo;
	void *mac;
	void *rx;
	void *tx;
	void *misc;
};

struct ccat_bar {
	unsigned long start;
	unsigned long end;
	unsigned long len;
	unsigned long flags;
	void *ioaddr;
};


static void ccat_bar_free(struct ccat_bar *bar)
{
	iounmap(bar->ioaddr);
	bar->ioaddr = NULL;
	release_mem_region(bar->start, bar->len);
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
		return -1;
	}
	
	res = request_mem_region(bar->start, bar->len, DRV_NAME);
	if(!res) {
		printk(KERN_INFO "%s: allocate mem_region failed.\n", DRV_NAME);
		return -1;
	}
	printk(KERN_INFO "%s: bar%d at [%lx,%lx] len=%lu.\n", DRV_NAME, index, bar->start, bar->end, bar->len);
	printk(KERN_INFO "%s: bar%d mem_region resource allocated as %p.\n", DRV_NAME, index, res);
	
	bar->ioaddr = ioremap(bar->start, bar->len);
	if(!bar->ioaddr) {
		printk(KERN_INFO "%s: bar%d ioremap failed.\n", DRV_NAME, index);
		release_mem_region(bar->start, bar->len);
		return -1;
	}
	printk(KERN_INFO "%s: bar%d I/O mem mapped to %p.\n", DRV_NAME, index, bar->ioaddr);
	return 0;
}

struct ccat_dma {
	dma_addr_t phys;
	void *virt;
	size_t size;
};

static void ccat_dma_free(struct ccat_dma *const dma, struct device *const dev)
{
	const struct ccat_dma tmp = {
		.phys = dma->phys,
		.virt = dma->virt,
		.size = dma->size
	};
	memset(dma, 0, sizeof(*dma));	
	dma_free_coherent(dev, tmp.size, tmp.virt, tmp.phys);
}

static int ccat_dma_init(struct ccat_dma *const dma, size_t channel, void *const ioaddr, struct device *const dev)
{
	uint64_t addr;
	uint32_t addrTranslate;
	uint32_t memTranslate;
	uint32_t memSize;
	uint32_t data = 0xffffffff;
	uint32_t offset = (sizeof(uint64_t) * channel) + 0x1000;
	iowrite32(data, ioaddr + offset);
	data = ioread32(ioaddr + offset);
	memTranslate = data & 0xfffffffc;
	memSize = (~memTranslate) + 1;
	dma->size = (2 * memSize) - PAGE_SIZE;
	printk(KERN_INFO "%s: %s() %u %u %u\n", DRV_NAME, __FUNCTION__, memTranslate, memSize, dma->size);
	dma->virt = dma_zalloc_coherent(dev, dma->size, &dma->phys, GFP_KERNEL | __GFP_DMA);
	if(!dma->virt || !dma->phys) {
		printk(KERN_INFO "%s: init DMA memory failed.\n", DRV_NAME);
		return -1;
	}
	addrTranslate = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = addrTranslate;
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
	return 0;
}

struct ccat_eth_priv {
	struct pci_dev *pdev;
	struct ccat_bar bar[3];
	unsigned char num_functions;
	CCatInfoBlock info;
	CCatInfoBlockOffs offsets;
	CCatMii mii;
	CCatDmaTxFifo tx_fifo;
	CCatDmaRxActBuf rx_fifo;
	CCatMacRegs mac;
	struct ccat_eth_func_ptr addr;
	struct ccat_dma rx_dma;
	struct ccat_dma tx_dma;
};

static int ccat_eth_priv_init_dma(struct ccat_eth_priv *priv)
{
	if(ccat_dma_init(&priv->rx_dma, priv->info.rxDmaChn, priv->bar[2].ioaddr, &priv->pdev->dev)) {
		printk(KERN_INFO "%s: init Rx DMA memory failed.\n", DRV_NAME);
		return -1;
	}
	if(ccat_dma_init(&priv->tx_dma, priv->info.txDmaChn, priv->bar[2].ioaddr, &priv->pdev->dev)) {
		printk(KERN_INFO "%s: init Tx DMA memory failed.\n", DRV_NAME);
		return -1;
	}
	
	priv->rx_fifo.rxActBuf = 0;
	priv->rx_fifo.FifoLevel = 0;
	memcpy_toio(priv->addr.rx_fifo, &priv->rx_fifo, sizeof(priv->rx_fifo));
	
	priv->tx_fifo.fifoReset = 1;
	memcpy_toio(priv->addr.tx_fifo, &priv->tx_fifo, sizeof(priv->tx_fifo));
	return 0;
}

/*
 * Initializes the CCat... members of the ccat_eth_priv structure.
 * Call this function only if info and ioaddr are already initialized!
 */
static void ccat_eth_priv_init_mappings(struct ccat_eth_priv *priv)
{
	void* func_base = priv->bar[0].ioaddr + priv->info.nAddr;
	memcpy_fromio(&priv->offsets, func_base, sizeof(priv->offsets));
	
	priv->addr.mii = func_base + priv->offsets.nMMIOffs;
	priv->addr.tx_fifo = func_base + priv->offsets.nTxFifoOffs;
	priv->addr.rx_fifo = func_base + priv->offsets.nTxFifoOffs + 0x10;
	priv->addr.mac = func_base + priv->offsets.nMacRegOffs;
	priv->addr.rx = func_base + priv->offsets.nRxMemOffs;
	priv->addr.tx = func_base + priv->offsets.nTxMemOffs;
	priv->addr.misc = func_base + priv->offsets.nMiscOffs;
	
	memcpy_fromio(&priv->mii, priv->addr.mii, sizeof(priv->mii));
	memcpy_fromio(&priv->tx_fifo, priv->addr.tx_fifo, sizeof(priv->tx_fifo));
	memcpy_fromio(&priv->rx_fifo, priv->addr.rx_fifo, sizeof(priv->rx_fifo));
	memcpy_fromio(&priv->mac, priv->addr.mac, sizeof(priv->mac));
}

static const char* CCatFunctionTypes[CCATINFO_MAX+1] = {
	"not used",
	"Informationblock",
	"EtherCAT Slave",
	"EtherCAT Master without DMA",
	"Ethernet MAC without DMA",
	"Ethernet Switch",
	"Sercos III",
	"Profibus",
	"CAN Controller",
	"KBUS Master",
	"IP-Link Master (planned)",
	"SPI Master",
	"I²C",
	"GPIO",
	"Drive",
	"CCAT Update",
	"Systemtime",
	"Interrupt Controller",
	"EEPROM Controller",
	"DMA Controller",
	"EtherCAT Master with DMA",
	"Ethernet MAC with DMA",
	"SRAM Interface",
	"Internal Copy block",
	"unknown"
};

static void print_CCatDmaTxRxFiFo(const CCatDmaTxFifo* pTxFifo, const CCatDmaRxActBuf* pRxFifo, const void *const base_addr)
{
	printk(KERN_INFO "%s: FIFO base address: %p\n", DRV_NAME, base_addr);
	printk(KERN_INFO "%s:     Tx Frame Header start:   0x%08x\n", DRV_NAME, pTxFifo->startAddr);
	printk(KERN_INFO "%s:     # 64 bit words:          %10d\n", DRV_NAME, pTxFifo->numQuadWords);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, pTxFifo->reserved1);
	printk(KERN_INFO "%s:     FIFO reset:              0x%08x\n", DRV_NAME, pTxFifo->fifoReset);
	printk(KERN_INFO "%s:     Rx Frame Header start:   0x%08x\n", DRV_NAME, pRxFifo->startAddr);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, pRxFifo->reserved1);
	printk(KERN_INFO "%s:     Rx start address:        %s\n", DRV_NAME, pRxFifo->nextValid ? "valid" : "invalid");
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, pRxFifo->reserved2);
	printk(KERN_INFO "%s:     FIFO level:              0x%08x\n", DRV_NAME, pRxFifo->FifoLevel);
	printk(KERN_INFO "%s:     Buffer level:            0x%08x\n", DRV_NAME, pRxFifo->bufferLevel);
	printk(KERN_INFO "%s:     next address:            0x%08x\n", DRV_NAME, pRxFifo->nextAddr);	
}

static void print_CCatInfoBlock(const CCatInfoBlock *pInfo, const void *const base_addr)
{
	const size_t index = min((int)pInfo->eCCatInfoType, CCATINFO_MAX);
	printk(KERN_INFO "%s: %s\n", DRV_NAME, CCatFunctionTypes[index]);
	printk(KERN_INFO "%s:     revision:     0x%x\n", DRV_NAME, pInfo->nRevision);
	printk(KERN_INFO "%s:     RX channel:   %d\n", DRV_NAME, pInfo->rxDmaChn);
	printk(KERN_INFO "%s:     TX channel:   %d\n", DRV_NAME, pInfo->txDmaChn);
	printk(KERN_INFO "%s:     baseaddr:     0x%lx\n", DRV_NAME, pInfo->nAddr);
	printk(KERN_INFO "%s:     size:         0x%lx\n", DRV_NAME, pInfo->nSize);
	printk(KERN_INFO "%s:     subfunction:  %p\n", DRV_NAME, base_addr);
}

static void print_CCatMacRegs(const CCatMacRegs *pMac, const void *const base_addr)
{
	printk(KERN_INFO "%s: MAC base address: %p\n", DRV_NAME, base_addr);
	printk(KERN_INFO "%s:     frame length error count:   %10d\n", DRV_NAME, pMac->frameLenErrCnt);
	printk(KERN_INFO "%s:     RX error count:             %10d\n", DRV_NAME, pMac->rxErrCnt);
	printk(KERN_INFO "%s:     CRC error count:            %10d\n", DRV_NAME, pMac->crcErrCnt);
	printk(KERN_INFO "%s:     Link lost error count:      %10d\n", DRV_NAME, pMac->linkLostErrCnt);
	printk(KERN_INFO "%s:     reserved:                   0x%08x\n", DRV_NAME, pMac->reserved1);
	printk(KERN_INFO "%s:     RX overflow count:          %10d\n", DRV_NAME, pMac->dropFrameErrCnt);
	printk(KERN_INFO "%s:     DMA overflow count:         %10d\n", DRV_NAME, pMac->reserved2[0]);
	//printk(KERN_INFO "%s:     reserverd:         %10d\n", DRV_NAME, pMac->reserved2[1]);
	printk(KERN_INFO "%s:     TX frame counter:           %10d\n", DRV_NAME, pMac->txFrameCnt);
	printk(KERN_INFO "%s:     RX frame counter:           %10d\n", DRV_NAME, pMac->rxFrameCnt);
	printk(KERN_INFO "%s:     TX-FIFO level:              0x%08x\n", DRV_NAME, pMac->txFifoLevel);
	printk(KERN_INFO "%s:     MII connection:             0x%08x\n", DRV_NAME, pMac->miiConnected);
}

static void print_CCatMii(const CCatMii *const pMii, const void *const base_addr)
{
	printk(KERN_INFO "%s: MII base address: %p\n", DRV_NAME, base_addr);
	printk(KERN_INFO "%s:     MII cycle:    %s\n", DRV_NAME, pMii->startMiCycle ? "running" : "no cycle");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved1);
	printk(KERN_INFO "%s:     cmd valid:    %s\n", DRV_NAME, pMii->cmdErr ? "no" : "yes");
	printk(KERN_INFO "%s:     cmd:          0x%x\n", DRV_NAME, pMii->cmd);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved2);
	printk(KERN_INFO "%s:     PHY addr:     0x%x\n", DRV_NAME, pMii->phyAddr);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved3);
	printk(KERN_INFO "%s:     PHY reg:      0x%x\n", DRV_NAME, pMii->phyReg);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved4);
	printk(KERN_INFO "%s:     PHY write:    0x%x\n", DRV_NAME, pMii->phyWriteData);
	printk(KERN_INFO "%s:     PHY read:     0x%x\n", DRV_NAME, pMii->phyReadData);
	printk(KERN_INFO "%s:     MAC addr:     %02x:%02x:%02x:%02x:%02x:%02x\n", DRV_NAME, pMii->macAddr.b[0], pMii->macAddr.b[1], pMii->macAddr.b[2], pMii->macAddr.b[3], pMii->macAddr.b[4], pMii->macAddr.b[5]);
	printk(KERN_INFO "%s:     MAC filter:   %s\n", DRV_NAME, pMii->macFilterEnabled ? "enabled" : "disabled");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved6);
	printk(KERN_INFO "%s:     Link State:   %s\n", DRV_NAME, pMii->linkStatus ? "link" : "no link");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved7);
	//printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, pMii->reserved8);
	//TODO add leds, systemtime insertion and interrupts
}

static void ccat_print_function_info(struct ccat_eth_priv* priv)
{
	print_CCatInfoBlock(&priv->info, priv->bar[0].ioaddr);
	print_CCatMii(&priv->mii, priv->addr.mii);
	print_CCatDmaTxRxFiFo(&priv->tx_fifo, &priv->rx_fifo, priv->addr.tx_fifo);	
	print_CCatMacRegs(&priv->mac, priv->addr.mac);
	printk(KERN_INFO "%s:  RX window:    %p\n", DRV_NAME, priv->addr.rx);
	printk(KERN_INFO "%s:  TX memory:    %p\n", DRV_NAME, priv->addr.tx);
	printk(KERN_INFO "%s:  misc:         %p\n", DRV_NAME, priv->addr.misc);
}

#define PCI_DEVICE_ID_BECKHOFF_CCAT 0x5000
#define PCI_VENDOR_ID_BECKHOFF 0x15EC

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BECKHOFF, PCI_DEVICE_ID_BECKHOFF_CCAT) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pci_ids);


static int ccat_eth_init_one(struct pci_dev *pdev, const struct pci_device_id *id);
static int ccat_eth_init_pci(struct ccat_eth_priv *priv);
static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage);
static int ccat_eth_open(struct net_device *dev);
static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ccat_eth_remove_one(struct pci_dev *pdev);
static void ccat_eth_remove_pci(struct ccat_eth_priv *priv);
static int ccat_eth_stop(struct net_device *dev);


static const struct net_device_ops ccat_eth_netdev_ops = {
	.ndo_get_stats64 = ccat_eth_get_stats64,
	.ndo_open = ccat_eth_open,
	.ndo_start_xmit = ccat_eth_start_xmit,
	.ndo_stop = ccat_eth_stop,
};

static struct net_device *ccat_eth_dev;

struct ccat_eth_tx_header {
	uint64_t notused;
	uint16_t length;
	uint8_t port;
	uint8_t wait_timestamp :1;
	uint8_t wait_event0 :1;
	uint8_t wait_event1 :1;
	uint8_t reserved1 :5;
	uint32_t was_read :1;
	uint32_t reserved2 : 31;
	uint64_t timestamp;
};

static const UINT8 frameForwardEthernetFrames[] = { 0x01, 0x01, 0x05, 0x01, 0x00, 0x00, 
							0x00, 0x1b, 0x21, 0x36, 0x1b, 0xce, 
							0x88, 0xa4, 0x0e, 0x10,
							0x08,		
							0x00,	
							0x00, 0x00,
							0x00, 0x01,
							0x02,	0x00,
							0x00, 0x00,
							0x00, 0x00,
							0x00, 0x00
	};

static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage)
{
	#if 0
	struct ccat_eth_priv *priv = netdev_priv(dev);
	struct ccat_eth_tx_header *header = priv->tx_virt;
	
	const unsigned char *next = (const unsigned char *)header + 1;
	const unsigned char *const end = next + sizeof(frameForwardEthernetFrames);
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	printk(KERN_INFO "%s: link is %s\n", DRV_NAME, priv->mii.linkStatus ? "up" : "down");
	printk(KERN_INFO "%s: next rx frame at %x.\n", DRV_NAME, ioread32(priv->addr.rx_fifo));
	printk(KERN_INFO "%s: old tx frame was %s.\n", DRV_NAME, header->was_read ? "read" : "not read");
	
	do {
		//printk(KERN_INFO "%02x ", *next);
	} while (++next < end);
	
	memcpy(header + 1, frameForwardEthernetFrames, sizeof(frameForwardEthernetFrames));
	
	header->length = sizeof(frameForwardEthernetFrames);
	header->port = 0x01;
	header->wait_timestamp = 0;
	header->wait_event0 = 0;
	header->wait_event1 = 0;
	#endif
	//TODO
	return storage;
}

static int ccat_eth_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ccat_eth_priv *priv;
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	ccat_eth_dev = alloc_etherdev(sizeof(struct ccat_eth_priv));
	if(!ccat_eth_dev) {
		printk(KERN_INFO "%s: mem alloc failed.\n", DRV_NAME);
		return -ENOMEM;
	}
	priv = netdev_priv(ccat_eth_dev);
	priv->pdev = pdev;
	
	/* pci initialization */
	if(ccat_eth_init_pci(priv)) {
		printk(KERN_INFO "%s: CCAT pci init failed.\n", DRV_NAME);
		ccat_eth_remove_one(priv->pdev);		
		return -1;		
	}
	
	/* complete ethernet device initialization */
	ccat_eth_dev->netdev_ops = &ccat_eth_netdev_ops;
	if(0 != register_netdev(ccat_eth_dev)) {
		printk(KERN_INFO "%s: unable to register network device.\n", DRV_NAME);
		ccat_eth_remove_one(pdev);
		return -ENODEV;
	}
	printk(KERN_INFO "%s: registered %s as network device.\n", DRV_NAME, ccat_eth_dev->name);
	return 0;
}

static int ccat_eth_init_pci(struct ccat_eth_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	void* addr;
	size_t i;
	int status;
	u8 revision;
	status = pci_enable_device (pdev);
	if(status) {
		printk(KERN_INFO "%s: enable CCAT pci device %s failed with %d\n", DRV_NAME, pdev->dev.kobj.name, status);
		return status;
	}
	
	status = pci_read_config_byte(pdev, PCI_REVISION_ID, &revision);
	if(status) {
		printk(KERN_INFO "%s: read CCAT pci revision failed with %d\n", DRV_NAME, status);
		return status;
	}
	
	/* FIXME upgrade to a newer kernel to get support of dma_set_mask_and_coherent()
	if (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64))) { */
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
		return -1;
	}
	
	if(ccat_bar_init(&priv->bar[2], 2, priv->pdev)) {
		printk(KERN_WARNING "%s: initialization of bar2 failed.\n", DRV_NAME);
		return -1;
	}
	
	priv->num_functions = ioread8(priv->bar[0].ioaddr + 4); /* jump to CCatInfoBlock.nMaxEntries */
	
	/* find CCATINFO_ETHERCAT_MASTER_DMA function */
	for(i = 0, addr = priv->bar[0].ioaddr; i < priv->num_functions; ++i, addr += sizeof(priv->info)) {
		if(CCATINFO_ETHERCAT_MASTER_DMA == ioread16(addr)) {
			memcpy_fromio(&priv->info, addr, sizeof(priv->info));
			ccat_eth_priv_init_mappings(priv);
			ccat_print_function_info(priv);
			status = ccat_eth_priv_init_dma(priv);
			break;
		}
	}
	
	//TODO
	return status;
}

static int ccat_eth_open(struct net_device *dev)
{
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	//TODO
	return 0;
}

static void ccat_eth_remove_one(struct pci_dev *pdev)
{
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	if(ccat_eth_dev) {
		unregister_netdev(ccat_eth_dev);
		ccat_eth_remove_pci(netdev_priv(ccat_eth_dev));
		free_netdev(ccat_eth_dev);
		printk(KERN_INFO "%s: cleanup done.\n\n", DRV_NAME);
	}
	//TODO
}

static void ccat_eth_remove_pci(struct ccat_eth_priv *priv)
{
	//TODO
	ccat_dma_free(&priv->tx_dma, &priv->pdev->dev);
	ccat_dma_free(&priv->rx_dma, &priv->pdev->dev);
	ccat_bar_free(&priv->bar[2]);
	ccat_bar_free(&priv->bar[0]);
	pci_disable_device (priv->pdev);
	priv->pdev = NULL;
}

static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	//printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	//TODO
	return NETDEV_TX_BUSY;
}

static int ccat_eth_stop(struct net_device *dev)
{
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	//TODO
	return 0;
}


static struct pci_driver pci_driver = {
	.name = DRV_NAME,
	.id_table = pci_ids,
	.probe = ccat_eth_init_one,
	.remove = ccat_eth_remove_one,
};

static void ccat_eth_exit_module(void) {
	pci_unregister_driver(&pci_driver);
}

static int ccat_eth_init_module(void) {
	return pci_register_driver(&pci_driver);
}


module_exit(ccat_eth_exit_module);
module_init(ccat_eth_init_module);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Bruenn <p.bruenn@beckhoff.com>");
MODULE_DESCRIPTION("Beckhoff CCAT ethernet driver");

