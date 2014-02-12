#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/kthread.h>

#include "CCatDefinitions.h"

#define DRV_NAME "ccat_eth"

static int run_rx_thread(void *data); /* rx handler thread function */

struct ccat_eth_register {
	void *mii;
	void *tx_fifo;
	void *rx_fifo;
	void *mac;
	void *rx_mem;
	void *tx_mem;
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
	void *frame;
	uint64_t addr;
	uint32_t translateAddr;
	uint32_t memTranslate;
	uint32_t memSize;
	uint32_t data = 0xffffffff;
	uint32_t offset = (sizeof(uint64_t) * channel) + 0x1000;
	
	/* calculate size and alignments */
	iowrite32(data, ioaddr + offset);
	data = ioread32(ioaddr + offset);	
	memTranslate = data & 0xfffffffc;
	memSize = (~memTranslate) + 1;
	dma->size = 2*memSize - PAGE_SIZE;
	
	dma->virt = dma_zalloc_coherent(dev, dma->size, &dma->phys, GFP_KERNEL | __GFP_DMA);
	if(!dma->virt || !dma->phys) {
		printk(KERN_INFO "%s: init DMA memory failed.\n", DRV_NAME);
		return -1;
	}
	
	translateAddr = (dma->phys + memSize - PAGE_SIZE) & memTranslate;
	addr = translateAddr;
#if 0
	memcpy_toio(ioaddr + offset, &addr, sizeof(addr));
#else
	iowrite32(translateAddr, ioaddr + offset);
#endif
	frame = dma->virt + translateAddr - dma->phys;
	
	printk(KERN_INFO "%s: 0x%x.\n", DRV_NAME, memTranslate);
	printk(KERN_INFO "%s: DMA mem initialized %p/%p -> %llx/%llx %u bytes.\n", DRV_NAME, dma->virt, frame, (uint64_t)(dma->phys), addr, dma->size);
	printk(KERN_INFO "%s: 0x%08x%x.\n", DRV_NAME, ioread32(ioaddr + offset + 4), ioread32(ioaddr + offset));
	return 0;
}

struct ccat_eth_priv {
	struct pci_dev *pdev;
	struct task_struct *rx_thread;
	struct ccat_bar bar[3];
	CCatInfoBlock info;
	struct ccat_dma rx_dma;
	struct ccat_dma tx_dma;
	struct ccat_eth_register reg;
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
	
	/* disable MAC filter */
	iowrite8(0, priv->reg.mii + 0x8 + 6);
	
	/* reset tx fifo */
	iowrite8(1, priv->reg.tx_fifo + 0x8);

	/* start rx dma fifo */
	iowrite32(0, priv->reg.rx_fifo + 12);
	iowrite32(0, priv->reg.rx_fifo + 8);
	iowrite32((1 << 31) | 0, priv->reg.rx_fifo);
	return 0;
}

/*
 * Initializes the CCat... members of the ccat_eth_priv structure.
 * Call this function only if info and ioaddr are already initialized!
 */
static void ccat_eth_priv_init_mappings(struct ccat_eth_priv *priv)
{
	CCatInfoBlockOffs offsets;
	void *const func_base = priv->bar[0].ioaddr + priv->info.nAddr;
	memcpy_fromio(&offsets, func_base, sizeof(offsets));
	
	priv->reg.mii = func_base + offsets.nMMIOffs;
	priv->reg.tx_fifo = func_base + offsets.nTxFifoOffs;
	priv->reg.rx_fifo = func_base + offsets.nTxFifoOffs + 0x10;
	priv->reg.mac = func_base + offsets.nMacRegOffs;
	priv->reg.rx_mem = func_base + offsets.nRxMemOffs;
	priv->reg.tx_mem = func_base + offsets.nTxMemOffs;
	priv->reg.misc = func_base + offsets.nMiscOffs;
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

static void print_CCatDmaRxActBuf(const struct ccat_eth_priv *const priv)
{
	CCatDmaRxActBuf rx_fifo;
	memcpy_fromio(&rx_fifo, priv->reg.rx_fifo, sizeof(rx_fifo));
	printk(KERN_INFO "%s: Rx FIFO base address: %p\n", DRV_NAME, priv->reg.rx_fifo);
	printk(KERN_INFO "%s:     Rx Frame Header start:   0x%08x\n", DRV_NAME, rx_fifo.startAddr);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, rx_fifo.reserved1);
	printk(KERN_INFO "%s:     Rx start address:        %s\n", DRV_NAME, rx_fifo.nextValid ? "valid" : "invalid");
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, rx_fifo.reserved2);
	printk(KERN_INFO "%s:     FIFO level:              0x%08x\n", DRV_NAME, rx_fifo.FifoLevel);
	printk(KERN_INFO "%s:     Buffer level:            0x%08x\n", DRV_NAME, rx_fifo.bufferLevel);
	printk(KERN_INFO "%s:     next address:            0x%08x\n", DRV_NAME, rx_fifo.nextAddr);	
}

static void print_CCatDmaTxFifo(const struct ccat_eth_priv *const priv)
{
	CCatDmaTxFifo tx_fifo;
	memcpy_fromio(&tx_fifo, priv->reg.tx_fifo, sizeof(tx_fifo));
	printk(KERN_INFO "%s: Tx FIFO base address: %p\n", DRV_NAME, priv->reg.tx_fifo);
	printk(KERN_INFO "%s:     Tx Frame Header start:   0x%08x\n", DRV_NAME, tx_fifo.startAddr);
	printk(KERN_INFO "%s:     # 64 bit words:          %10d\n", DRV_NAME, tx_fifo.numQuadWords);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME, tx_fifo.reserved1);
	printk(KERN_INFO "%s:     FIFO reset:              0x%08x\n", DRV_NAME, tx_fifo.fifoReset);
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

static void print_CCatMacRegs(const struct ccat_eth_priv *const priv)
{
	CCatMacRegs mac;
	memcpy_fromio(&mac, priv->reg.mac, sizeof(mac));
	printk(KERN_INFO "%s: MAC base address: %p\n", DRV_NAME, priv->reg.mac);
	printk(KERN_INFO "%s:     frame length error count:   %10d\n", DRV_NAME, mac.frameLenErrCnt);
	printk(KERN_INFO "%s:     RX error count:             %10d\n", DRV_NAME, mac.rxErrCnt);
	printk(KERN_INFO "%s:     CRC error count:            %10d\n", DRV_NAME, mac.crcErrCnt);
	printk(KERN_INFO "%s:     Link lost error count:      %10d\n", DRV_NAME, mac.linkLostErrCnt);
	printk(KERN_INFO "%s:     reserved:                   0x%08x\n", DRV_NAME, mac.reserved1);
	printk(KERN_INFO "%s:     RX overflow count:          %10d\n", DRV_NAME, mac.dropFrameErrCnt);
	printk(KERN_INFO "%s:     DMA overflow count:         %10d\n", DRV_NAME, mac.reserved2[0]);
	//printk(KERN_INFO "%s:     reserverd:         %10d\n", DRV_NAME, mac.reserved2[1]);
	printk(KERN_INFO "%s:     TX frame counter:           %10d\n", DRV_NAME, mac.txFrameCnt);
	printk(KERN_INFO "%s:     RX frame counter:           %10d\n", DRV_NAME, mac.rxFrameCnt);
	printk(KERN_INFO "%s:     TX-FIFO level:              0x%08x\n", DRV_NAME, mac.txFifoLevel);
	printk(KERN_INFO "%s:     MII connection:             0x%08x\n", DRV_NAME, mac.miiConnected);
}

static void print_CCatMii(const struct ccat_eth_priv *const priv)
{
	CCatMii mii;
	memcpy_fromio(&mii, priv->reg.mii, sizeof(mii));
	printk(KERN_INFO "%s: MII base address: %p\n", DRV_NAME, priv->reg.mii);
	printk(KERN_INFO "%s:     MII cycle:    %s\n", DRV_NAME, mii.startMiCycle ? "running" : "no cycle");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved1);
	printk(KERN_INFO "%s:     cmd valid:    %s\n", DRV_NAME, mii.cmdErr ? "no" : "yes");
	printk(KERN_INFO "%s:     cmd:          0x%x\n", DRV_NAME, mii.cmd);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved2);
	printk(KERN_INFO "%s:     PHY addr:     0x%x\n", DRV_NAME, mii.phyAddr);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved3);
	printk(KERN_INFO "%s:     PHY reg:      0x%x\n", DRV_NAME, mii.phyReg);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved4);
	printk(KERN_INFO "%s:     PHY write:    0x%x\n", DRV_NAME, mii.phyWriteData);
	printk(KERN_INFO "%s:     PHY read:     0x%x\n", DRV_NAME, mii.phyReadData);
	printk(KERN_INFO "%s:     MAC addr:     %02x:%02x:%02x:%02x:%02x:%02x\n", DRV_NAME, mii.macAddr.b[0], mii.macAddr.b[1], mii.macAddr.b[2], mii.macAddr.b[3], mii.macAddr.b[4], mii.macAddr.b[5]);
	printk(KERN_INFO "%s:     MAC filter:   %s\n", DRV_NAME, mii.macFilterEnabled ? "enabled" : "disabled");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved6);
	printk(KERN_INFO "%s:     Link State:   %s\n", DRV_NAME, mii.linkStatus ? "link" : "no link");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved7);
	//printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved8);
	//TODO add leds, systemtime insertion and interrupts
}

static void ccat_print_function_info(struct ccat_eth_priv* priv)
{
	print_CCatInfoBlock(&priv->info, priv->bar[0].ioaddr);
	print_CCatMii(priv);
	print_CCatDmaTxFifo(priv);
	print_CCatDmaRxActBuf(priv);
	print_CCatMacRegs(priv);
	printk(KERN_INFO "%s:  RX window:    %p\n", DRV_NAME, priv->reg.rx_mem);
	printk(KERN_INFO "%s:  TX memory:    %p\n", DRV_NAME, priv->reg.tx_mem);
	printk(KERN_INFO "%s:  misc:         %p\n", DRV_NAME, priv->reg.misc);
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

static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage)
{
	//TODO
	return storage;
}

static int ccat_eth_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ccat_eth_priv *priv;
	if(ccat_eth_dev) {
		printk(KERN_INFO "%s: driver supports only one CCAT device at a time.\n", DRV_NAME);
		return -1;
	}
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
	
	priv->rx_thread = kthread_run(run_rx_thread, priv, "%s_rx", DRV_NAME);
	
	return 0;
}

static int ccat_eth_init_pci(struct ccat_eth_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	void* addr;
	size_t i;
	int status;
	uint8_t revision;
	uint8_t num_functions;
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
	//if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(24))) {
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
	
	num_functions = ioread8(priv->bar[0].ioaddr + 4); /* jump to CCatInfoBlock.nMaxEntries */
	
	/* find CCATINFO_ETHERCAT_MASTER_DMA function */
	for(i = 0, addr = priv->bar[0].ioaddr; i < num_functions; ++i, addr += sizeof(priv->info)) {
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
	if(ccat_eth_dev) {
		struct ccat_eth_priv *const priv = netdev_priv(ccat_eth_dev);
		if(priv && priv->rx_thread) {
			kthread_stop(priv->rx_thread);
		}
		unregister_netdev(ccat_eth_dev);
		ccat_eth_remove_pci(priv);
		free_netdev(ccat_eth_dev);
		ccat_eth_dev = NULL;
		printk(KERN_INFO "%s: cleanup done.\n\n", DRV_NAME);
	}
}

static void ccat_eth_remove_pci(struct ccat_eth_priv *priv)
{
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

static void test_rx(struct ccat_eth_priv *const priv)
{
	const CCatRxDesc *const end = (CCatRxDesc *)(priv->rx_dma.virt + priv->rx_dma.size);
	const CCatRxDesc *frame = priv->rx_dma.virt;
	
	while(frame < end) {
		if(frame->nextValid) {
			printk(KERN_INFO "%s: valid frame found at %p\n", DRV_NAME, frame);
		}
		++frame;
	}
	print_CCatDmaRxActBuf(priv);
}

static const UINT8 frameForwardEthernetFrames[] = {
	0x01, 0x01, 0x05, 0x01, 0x00, 0x00,
	0x00, 0x1b, 0x21, 0x36, 0x1b, 0xce, 
	0x88, 0xa4, 0x0e, 0x10,
	0x08,		
	0x00,	
	0x00, 0x00,
	0x00, 0x01,
	0x02, 0x00,
	0x00, 0x00,
	0x00, 0x00,
	0x00, 0x00
};

static const UINT8 frameArpReq[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0x00, 0x1b, 0x21, 0x36, 0x1b, 0xce, 
	0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 
	0x00, 0x1b, 0x21, 0x36, 0x1b, 0xce, 
	0xc0, 0xa8, 0x01, 0x04, //sender ip Address 192.168.1.4
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0xc0, 0xa8, 0x01, 0x07, //target ip Address 192.168.1.7
};

static void test_tx(struct ccat_eth_priv *const priv)
{
	uint64_t dmaAddr;
	uint32_t addr_and_length;
	CCatDmaTxFrame *const frame = priv->tx_dma.virt;
	if(frame->head.sent) {
		printk(KERN_INFO "%s: HUHU\nHUHU\nHUHU\nHUHU\nHUHU\nHUHU\n", DRV_NAME);
	} else {
		memset(frame, 0, sizeof(*frame));
		frame->head.port0 = 1;
		frame->head.length = sizeof(frameForwardEthernetFrames);
		memcpy(frame->data, frameForwardEthernetFrames, sizeof(frameForwardEthernetFrames));
		wmb();
		addr_and_length = 0;
		addr_and_length += ((frame->head.length + sizeof(frame->head) + 8) / 8) << 24;
		memcpy_fromio(&dmaAddr, priv->bar[2].ioaddr + 0x1000 + (8*priv->info.txDmaChn), sizeof(dmaAddr));
		printk(KERN_INFO "%s: phys: 0x%llx offset: 0x%x\n", DRV_NAME, dmaAddr, addr_and_length);
		print_CCatMii(priv);
		iowrite32(addr_and_length, priv->reg.tx_fifo);
	}
}

static int run_rx_thread(void *data)
{
	while(!kthread_should_stop()) {
		msleep(2000);
		test_rx((struct ccat_eth_priv *)data);
		test_tx((struct ccat_eth_priv *)data);
	}
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

