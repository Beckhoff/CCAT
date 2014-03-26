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

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include "ccat.h"
#include "netdev.h"
#include "print.h"

/**
 * EtherCAT frame to enable forwarding on EtherCAT Terminals
 */
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

#define FIFO_LENGTH 64
#define DMA_POLL_DELAY_RANGE_USECS 100, 100	/* time to sleep between rx/tx DMA polls */
#define POLL_DELAY_RANGE_USECS 500, 1000	/* time to sleep between link state polls */

static int run_poll_thread(void *data);
static int run_rx_thread(void *data);
static int run_tx_thread(void *data);

static struct rtnl_link_stats64 *ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64
						      *storage);
static int ccat_eth_open(struct net_device *dev);
static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb,
				       struct net_device *dev);
static int ccat_eth_stop(struct net_device *dev);
static void ccat_eth_xmit_raw(struct net_device *dev, const char *data,
			      size_t len);

static const struct net_device_ops ccat_eth_netdev_ops = {
	.ndo_get_stats64 = ccat_eth_get_stats64,
	.ndo_open = ccat_eth_open,
	.ndo_start_xmit = ccat_eth_start_xmit,
	.ndo_stop = ccat_eth_stop,
};

typedef void (*fifo_add_function) (struct ccat_eth_frame *,
				   struct ccat_eth_dma_fifo *);

static void ccat_eth_rx_fifo_add(struct ccat_eth_frame *frame,
				 struct ccat_eth_dma_fifo *fifo)
{
	const size_t offset = ((void *)(frame) - fifo->dma.virt);
	const uint32_t addr_and_length = (1 << 31) | offset;
	frame->received = 0;
	iowrite32(addr_and_length, fifo->reg);
}

static void ccat_eth_tx_fifo_add_free(struct ccat_eth_frame *frame,
				      struct ccat_eth_dma_fifo *fifo)
{
	/* mark frame as ready to use for tx */
	frame->sent = 1;
}

static void ccat_eth_tx_fifo_full(struct net_device *const dev,
				  const struct ccat_eth_frame *const frame)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	netif_stop_queue(dev);
	priv->next_tx_frame = frame;
	wake_up_process(priv->tx_thread);
}

static void ccat_eth_dma_fifo_reset(struct ccat_eth_dma_fifo *fifo)
{
	struct ccat_eth_frame *frame = fifo->dma.virt;
	const struct ccat_eth_frame *const end = frame + FIFO_LENGTH;

	/* reset hw fifo */
	iowrite32(0, fifo->reg + 0x8);
	wmb();

	if (fifo->add) {
		while (frame < end) {
			fifo->add(frame, fifo);
			++frame;
		}
	}
}

static int ccat_eth_dma_fifo_init(struct ccat_eth_dma_fifo *fifo,
				  void __iomem * const fifo_reg,
				  fifo_add_function add, size_t channel,
				  struct ccat_eth_priv *const priv)
{
	if (0 !=
	    ccat_dma_init(&fifo->dma, channel, priv->ccatdev->bar[2].ioaddr,
			  &priv->ccatdev->pdev->dev)) {
		pr_info("init DMA%d memory failed.\n", channel);
		return -1;
	}
	fifo->add = add;
	fifo->reg = fifo_reg;
	return 0;
}

/**
 * Stop both (Rx/Tx) DMA fifo's and free related management structures
 */
static void ccat_eth_priv_free_dma(struct ccat_eth_priv *priv)
{
	/* reset hw fifo's */
	iowrite32(0, priv->rx_fifo.reg + 0x8);
	iowrite32(0, priv->tx_fifo.reg + 0x8);
	wmb();

	/* release dma */
	ccat_dma_free(&priv->rx_fifo.dma);
	ccat_dma_free(&priv->tx_fifo.dma);
	pr_info("DMA fifo's stopped.\n");
}

/**
 * Initalizes both (Rx/Tx) DMA fifo's and related management structures
 */
static int ccat_eth_priv_init_dma(struct ccat_eth_priv *priv)
{
	if (ccat_eth_dma_fifo_init
	    (&priv->rx_fifo, priv->reg.rx_fifo, ccat_eth_rx_fifo_add,
	     priv->info.rxDmaChn, priv)) {
		pr_warn("init Rx DMA fifo failed.\n");
		return -1;
	}

	if (ccat_eth_dma_fifo_init
	    (&priv->tx_fifo, priv->reg.tx_fifo, ccat_eth_tx_fifo_add_free,
	     priv->info.txDmaChn, priv)) {
		pr_warn("init Tx DMA fifo failed.\n");
		ccat_dma_free(&priv->rx_fifo.dma);
		return -1;
	}

	/* disable MAC filter */
	iowrite8(0, priv->reg.mii + 0x8 + 6);
	wmb();
	return 0;
}

/**
 * Initializes the CCat... members of the ccat_eth_priv structure.
 * Call this function only if info and ioaddr are already initialized!
 */
static void ccat_eth_priv_init_mappings(struct ccat_eth_priv *priv)
{
	CCatInfoBlockOffs offsets;
	void __iomem *const func_base =
	    priv->ccatdev->bar[0].ioaddr + priv->info.nAddr;
	memcpy_fromio(&offsets, func_base, sizeof(offsets));
	priv->reg.mii = func_base + offsets.nMMIOffs;
	priv->reg.tx_fifo = func_base + offsets.nTxFifoOffs;
	priv->reg.rx_fifo = func_base + offsets.nTxFifoOffs + 0x10;
	priv->reg.mac = func_base + offsets.nMacRegOffs;
	priv->reg.rx_mem = func_base + offsets.nRxMemOffs;
	priv->reg.tx_mem = func_base + offsets.nTxMemOffs;
	priv->reg.misc = func_base + offsets.nMiscOffs;
}

/**
 * Read link state from CCAT hardware
 * @return 1 if link is up, 0 if not
 */
inline static size_t ccat_eth_priv_read_link_state(const struct ccat_eth_priv
						   *const priv)
{
	return (1 << 24) == (ioread32(priv->reg.mii + 0x8 + 4) & (1 << 24));
}

static struct rtnl_link_stats64 *ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64
						      *storage)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	CCatMacRegs mac;
	memcpy_fromio(&mac, priv->reg.mac, sizeof(mac));
	storage->rx_packets = mac.rxFrameCnt;	/* total packets received       */
	storage->tx_packets = mac.txFrameCnt;	/* total packets transmitted    */
	storage->rx_bytes = atomic64_read(&priv->rx_bytes);	/* total bytes received         */
	storage->tx_bytes = atomic64_read(&priv->tx_bytes);	/* total bytes transmitted      */
	storage->rx_errors = mac.frameLenErrCnt + mac.dropFrameErrCnt + mac.crcErrCnt + mac.rxErrCnt;	/* bad packets received         */
	//TODO __u64    tx_errors;              /* packet transmit problems     */
	storage->rx_dropped = atomic64_read(&priv->rx_dropped);	/* no space in linux buffers    */
	storage->tx_dropped = atomic64_read(&priv->tx_dropped);	/* no space available in linux  */
	//TODO __u64    multicast;              /* multicast packets received   */
	//TODO __u64    collisions;

	/* detailed rx_errors: */
	storage->rx_length_errors = mac.frameLenErrCnt;
	storage->rx_over_errors = mac.dropFrameErrCnt;	/* receiver ring buff overflow  */
	storage->rx_crc_errors = mac.crcErrCnt;	/* recved pkt with crc error    */
	storage->rx_frame_errors = mac.rxErrCnt;	/* recv'd frame alignment error */
	storage->rx_fifo_errors = mac.dropFrameErrCnt;	/* recv'r fifo overrun          */
	//TODO __u64    rx_missed_errors;       /* receiver missed packet       */

	/* detailed tx_errors */
	//TODO __u64    tx_aborted_errors;
	//TODO __u64    tx_carrier_errors;
	//TODO __u64    tx_fifo_errors;
	//TODO __u64    tx_heartbeat_errors;
	//TODO __u64    tx_window_errors;

	/* for cslip etc */
	//TODO __u64    rx_compressed;
	//TODO __u64    tx_compressed;
	return storage;
}

struct ccat_eth_priv *ccat_eth_init(const struct ccat_device *const ccatdev,
				    const void __iomem * const addr)
{
	struct ccat_eth_priv *priv;
	struct net_device *const netdev = alloc_etherdev(sizeof(*priv));
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->ccatdev = ccatdev;

	/* ccat register mappings */
	memcpy_fromio(&priv->info, addr, sizeof(priv->info));
	ccat_eth_priv_init_mappings(priv);
	ccat_print_function_info(priv);

	if (ccat_eth_priv_init_dma(priv)) {
		pr_warn("%s(): DMA initialization failed.\n", __FUNCTION__);
		free_netdev(netdev);
		return NULL;
	}

	/* init netdev with MAC and stack callbacks */
	memcpy_fromio(netdev->dev_addr, priv->reg.mii + 8, 6);
	netdev->netdev_ops = &ccat_eth_netdev_ops;

	if (register_netdev(netdev)) {
		pr_info("unable to register network device.\n");
		ccat_eth_priv_free_dma(priv);
		free_netdev(netdev);
		return NULL;
	}
	pr_info("registered %s as network device.\n", netdev->name);
	priv->rx_thread = kthread_run(run_rx_thread, netdev, "%s_rx", DRV_NAME);
	priv->tx_thread = kthread_run(run_tx_thread, netdev, "%s_tx", DRV_NAME);
	return priv;
}

void ccat_eth_remove(struct ccat_eth_priv *const priv)
{
	if (priv->rx_thread) {
		kthread_stop(priv->rx_thread);
	}
	if (priv->tx_thread) {
		kthread_stop(priv->tx_thread);
	}
	unregister_netdev(priv->netdev);
	ccat_eth_priv_free_dma(priv);
	free_netdev(priv->netdev);
	pr_info("%s(): done\n", __FUNCTION__);
}

static int ccat_eth_open(struct net_device *dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	netif_carrier_off(dev);
	priv->poll_thread =
	    kthread_run(run_poll_thread, dev, "%s_poll", DRV_NAME);

	//TODO
	return 0;
}

static const size_t CCATRXDESC_HEADER_LEN = 20;
static void ccat_eth_receive(struct net_device *const dev,
			     const struct ccat_eth_frame *const frame)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	const size_t len = frame->length - CCATRXDESC_HEADER_LEN;
	struct sk_buff *skb = dev_alloc_skb(len + NET_IP_ALIGN);
	if (!skb) {
		pr_info("%s() out of memory :-(\n", __FUNCTION__);
		atomic64_inc(&priv->rx_dropped);
		return;
	}
	skb->dev = dev;
	skb_reserve(skb, NET_IP_ALIGN);
	skb_copy_to_linear_data(skb, frame->data, len);
	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	atomic64_add(len, &priv->rx_bytes);
	netif_rx(skb);
}

static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	static size_t next = 0;
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	struct ccat_eth_frame *const frame =
	    ((struct ccat_eth_frame *)priv->tx_fifo.dma.virt);
	uint32_t addr_and_length;

	if (skb_is_nonlinear(skb)) {
		pr_warn("Non linear skb not supported -> drop frame.\n");
		atomic64_inc(&priv->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len > sizeof(frame->data)) {
		pr_warn("skb.len %llu exceeds dma buffer %llu -> drop frame.\n",
			(uint64_t) skb->len, (uint64_t) sizeof(frame->data));
		atomic64_inc(&priv->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (!frame[next].sent) {
		netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		ccat_eth_tx_fifo_full(dev, &frame[next]);
		return NETDEV_TX_BUSY;
	}

	/* prepare frame in DMA memory */
	frame[next].sent = 0;
	frame[next].length = skb->len;
	memcpy(frame[next].data, skb->data, skb->len);

	dev_kfree_skb_any(skb);

	addr_and_length = 8 + (next * sizeof(*frame));
	addr_and_length +=
	    ((frame[next].length + sizeof(CCAT_HEADER_TAG) + 8) / 8) << 24;
	iowrite32(addr_and_length, priv->reg.tx_fifo);	/* add to DMA fifo */
	atomic64_add(frame[next].length, &priv->tx_bytes);	/* update stats */

	next = (next + 1) % FIFO_LENGTH;
	/* stop queue if tx ring is full */
	if (!frame[next].sent) {
		ccat_eth_tx_fifo_full(dev, &frame[next]);
	}
	return NETDEV_TX_OK;
}

static int ccat_eth_stop(struct net_device *dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	netif_stop_queue(dev);
	if (priv->poll_thread) {
		/* TODO care about smp context? */
		kthread_stop(priv->poll_thread);
		priv->poll_thread = NULL;
	}
	netdev_info(dev, "stopped.\n");
	return 0;
}

static void ccat_eth_link_down(struct net_device *dev)
{
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	netdev_info(dev, "NIC Link is Down\n");
}

static void ccat_eth_link_up(struct net_device *const dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	netdev_info(dev, "NIC Link is Up\n");
	/* TODO netdev_info(dev, "NIC Link is Up %u Mbps %s Duplex\n",
	   speed == SPEED_100 ? 100 : 10,
	   cmd.duplex == DUPLEX_FULL ? "Full" : "Half"); */

	ccat_eth_dma_fifo_reset(&priv->rx_fifo);
	ccat_eth_dma_fifo_reset(&priv->tx_fifo);
	ccat_eth_xmit_raw(dev, frameForwardEthernetFrames,
			  sizeof(frameForwardEthernetFrames));
	netif_carrier_on(dev);
	netif_start_queue(dev);
}

/**
 * Function to transmit a raw buffer to the network (f.e. frameForwardEthernetFrames)
 * @dev a valid net_device
 * @data pointer to your raw buffer
 * @len number of bytes in the raw buffer to transmit
 */
static void ccat_eth_xmit_raw(struct net_device *dev, const char *const data,
			      size_t len)
{
	struct sk_buff *skb = dev_alloc_skb(len);
	skb->dev = dev;
	skb_copy_to_linear_data(skb, data, len);
	skb_put(skb, len);
	ccat_eth_start_xmit(skb, dev);
}

/**
 * Since CCAT doesn't support interrupts until now, we have to poll
 * some status bits to recognize things like link change etc.
 */
static int run_poll_thread(void *data)
{
	struct net_device *const dev = (struct net_device *)data;
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	size_t link = 0;

	while (!kthread_should_stop()) {
		if (ccat_eth_priv_read_link_state(priv) != link) {
			link = !link;
			link ? ccat_eth_link_up(dev) : ccat_eth_link_down(dev);
		}
		usleep_range(POLL_DELAY_RANGE_USECS);
	}
	pr_info("%s() stopped.\n", __FUNCTION__);
	return 0;
}

static int run_rx_thread(void *data)
{
	struct net_device *const dev = (struct net_device *)data;
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	struct ccat_eth_frame *frame = priv->rx_fifo.dma.virt;
	const struct ccat_eth_frame *const end = frame + FIFO_LENGTH;

	while (!kthread_should_stop()) {
		/* wait until frame was used by DMA for Rx */
		while (!kthread_should_stop() && !frame->received) {
			usleep_range(DMA_POLL_DELAY_RANGE_USECS);
		}

		/* can be NULL, if we are asked to stop! */
		if (frame->received) {
			ccat_eth_receive(dev, frame);
			frame->received = 0;
			ccat_eth_rx_fifo_add(frame, &priv->rx_fifo);
		}
		if (++frame >= end) {
			frame = priv->rx_fifo.dma.virt;
		}
	}
	pr_info("%s() stopped.\n", __FUNCTION__);
	return 0;
}

/**
 * Polling of tx dma descriptors in ethernet operating mode
 */
static int run_tx_thread(void *data)
{
	struct net_device *const dev = (struct net_device *)data;
	struct ccat_eth_priv *const priv = netdev_priv(dev);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		const struct ccat_eth_frame *const frame = priv->next_tx_frame;
		if (frame) {
			while (!kthread_should_stop() && !frame->sent) {
				usleep_range(DMA_POLL_DELAY_RANGE_USECS);
			}
		}
		netif_wake_queue(dev);
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	pr_info("%s() stopped.\n", __FUNCTION__);
	return 0;
}
