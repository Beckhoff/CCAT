#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "CCatDefinitions.h"

#define DRV_NAME "ccat_eth"

struct ccat_eth_priv {
	struct pci_dev *pdev;
	void *ioaddr;
	unsigned long bar0_start;
	unsigned long bar0_end;
	unsigned long bar0_len;
	unsigned long bar0_flags;
	unsigned char num_functions;
	CCatInfoBlock info;
	CCatInfoBlockOffs offsets;
	CCatMii mii;
	CCatDmaTxFifo tx_fifo;
	CCatDmaRxActBuf rx_fifo;
	CCatMacRegs mac;
};

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
	//TODO add leds, systimeinsertion and interrupts
}

static void ccat_print_function_info(struct ccat_eth_priv* priv)
{
	const CCatInfoBlock *pInfo = &priv->info;
	void* func_base = priv->ioaddr + pInfo->nAddr;
	memcpy_fromio(&priv->offsets, func_base, sizeof(priv->offsets));
	memcpy_fromio(&priv->mii, func_base + priv->offsets.nMMIOffs, sizeof(priv->mii));
	memcpy_fromio(&priv->tx_fifo, func_base + priv->offsets.nTxFifoOffs, sizeof(priv->tx_fifo));
	memcpy_fromio(&priv->rx_fifo, func_base + priv->offsets.nTxFifoOffs + 0x10, sizeof(priv->rx_fifo));
	memcpy_fromio(&priv->mac, func_base + priv->offsets.nMacRegOffs, sizeof(priv->mac));
	
	print_CCatInfoBlock(&priv->info, priv->ioaddr);
	print_CCatMii(&priv->mii, func_base + priv->offsets.nMMIOffs);
	print_CCatDmaTxRxFiFo(&priv->tx_fifo, &priv->rx_fifo, func_base + priv->offsets.nTxFifoOffs);	
	print_CCatMacRegs(&priv->mac, func_base + priv->offsets.nMacRegOffs);
	
	printk(KERN_INFO "%s:  RX offset:    0x%04x\n", DRV_NAME, ioread32(func_base + 16));
	printk(KERN_INFO "%s:  TX offset:    0x%04x\n", DRV_NAME, ioread32(func_base + 20));
	printk(KERN_INFO "%s:  misc:         0x%04x\n", DRV_NAME, ioread32(func_base + 24));
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

static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage)
{
	//printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
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
	
	//TODO pci initialization
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
	struct resource *res;
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
	
	priv->bar0_start = pci_resource_start(pdev, 0);
	priv->bar0_end = pci_resource_end(pdev, 0);
	priv->bar0_len = pci_resource_len(pdev, 0);
	priv->bar0_flags = pci_resource_flags(pdev, 0);
	if(!(IORESOURCE_MEM & priv->bar0_flags)) {
		printk(KERN_INFO "%s: bar0 should be memory space, but it isn't -> abort CCAT initialization.\n", DRV_NAME);
		return -1;
	}
	
	res = request_mem_region(priv->bar0_start, priv->bar0_len, DRV_NAME);
	if(!res) {
		printk(KERN_INFO "%s: allocate mem_region failed.\n", DRV_NAME);
		return -1;
	}
	printk(KERN_INFO "%s: bar0 at [%lx,%lx] len=%lu.\n", DRV_NAME, priv->bar0_start, priv->bar0_end, priv->bar0_len);
	printk(KERN_INFO "%s: mem_region resource allocated as %p.\n", DRV_NAME, res);
	
	priv->ioaddr = ioremap(priv->bar0_start, priv->bar0_len);
	if(!priv->ioaddr) {
		printk(KERN_INFO "%s: ioremap failed.\n", DRV_NAME);
		release_mem_region(priv->bar0_start, priv->bar0_len);
		return -1;
	}
	printk(KERN_INFO "%s: I/O mem mapped to %p.\n", DRV_NAME, priv->ioaddr);
	priv->num_functions = ioread8(priv->ioaddr + 4); /* jump to CCatInfoBlock.nMaxEntries */
	
	/* find CCATINFO_ETHERCAT_MASTER_DMA function */
	for(i = 0, addr = priv->ioaddr; i < priv->num_functions; ++i, addr += sizeof(priv->info)) {
		if(CCATINFO_ETHERCAT_MASTER_DMA == ioread16(addr)) {
			memcpy_fromio(&priv->info, addr, sizeof(priv->info));
			ccat_print_function_info(priv);
			break;
		}
	}
	
	//TODO
	return 0;
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
		printk(KERN_INFO "%s: cleanup done.\n", DRV_NAME);
	}
	//TODO
}

static void ccat_eth_remove_pci(struct ccat_eth_priv *priv)
{
	//TODO
	iounmap(priv->ioaddr);
	priv->ioaddr = NULL;
	release_mem_region(priv->bar0_start, priv->bar0_len);
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

