#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#define DRV_NAME "ccat_eth"

#define PCI_DEVICE_ID_BECKHOFF_CCAT 0x5000
#define PCI_VENDOR_ID_BECKHOFF 0x15EC

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BECKHOFF, PCI_DEVICE_ID_BECKHOFF_CCAT) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static int ccat_eth_init_one(struct pci_dev *pdev, const struct pci_device_id *id);
static int ccat_eth_init_pci(struct pci_dev *pdev);
static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage);
static int ccat_eth_open(struct net_device *dev);
static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ccat_eth_remove_one(struct pci_dev *pdev);
static void ccat_eth_remove_pci(struct pci_dev *pdev);
static int ccat_eth_stop(struct net_device *dev);


struct ccat_eth_priv {
	int dummy;
};

static const struct net_device_ops ccat_eth_netdev_ops = {
	.ndo_get_stats64 = ccat_eth_get_stats64,
	.ndo_open = ccat_eth_open,
	.ndo_start_xmit = ccat_eth_start_xmit,
	.ndo_stop = ccat_eth_stop,
};

static struct net_device *ccat_eth_dev;

static struct rtnl_link_stats64* ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *storage)
{
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	//TODO
	return storage;
}

static int ccat_eth_init_pci(struct pci_dev *pdev)
{
	//TODO
	return pci_enable_device (pdev);
}

static int ccat_eth_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	printk(KERN_INFO "%s: %s() called.\n", DRV_NAME, __FUNCTION__);
	ccat_eth_dev = alloc_etherdev(sizeof(struct ccat_eth_priv));
	if(!ccat_eth_dev) {
		printk(KERN_INFO "%s: mem alloc failed.\n", DRV_NAME);
		return -ENOMEM;
	}
	
	//TODO pci initialization
	if(ccat_eth_init_pci(pdev)) {
		printk(KERN_INFO "%s: CCAT pci init failed.\n", DRV_NAME);
		ccat_eth_remove_one(pdev);		
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
		ccat_eth_remove_pci(pdev);
		free_netdev(ccat_eth_dev);
		printk(KERN_INFO "%s: cleanup done.\n", DRV_NAME);
	}
	//TODO
}

static void ccat_eth_remove_pci(struct pci_dev *pdev)
{
	//TODO
	pci_disable_device (pdev);
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

