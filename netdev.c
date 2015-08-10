/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014 - 2015  Beckhoff Automation GmbH
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "module.h"

/**
 * EtherCAT frame to enable forwarding on EtherCAT Terminals
 */
static const u8 frameForwardEthernetFrames[] = {
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
#define POLL_TIME ktime_set(0, 100 * NSEC_PER_USEC)

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
	u8 placeholder[0x800];
};

struct frame_header_dma {
	__le32 reserved1;
	__le32 rx_flags;
#define CCAT_FRAME_RECEIVED 0x1
	__le16 length;
	__le16 reserved3;
	__le32 tx_flags;
#define CCAT_FRAME_SENT 0x1
	__le64 timestamp;
};

struct frame_dma {
	struct frame_header_dma hdr;
	u8 data[0x800 - sizeof(struct frame_header_dma)];
};

struct frame_header_nodma {
#define CCAT_FRAME_RECEIVED 0x1
	__le16 length;
	__le16 reserved3;
	__le32 tx_flags;
#define CCAT_FRAME_SENT 0x1
	__le64 timestamp;
};

struct frame_nodma {
	struct frame_header_nodma hdr;
	u8 data[0x800 - sizeof(struct frame_header_nodma)];
};

#define MAX_PAYLOAD_SIZE \
	(sizeof(struct ccat_eth_frame) - max(sizeof(struct frame_header_dma), sizeof(struct frame_header_nodma)))

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
	void (*add) (struct ccat_eth_dma_fifo *, void *);
	void (*copy_to_skb)(struct ccat_eth_dma_fifo *, struct sk_buff *, size_t);
	void (*queue_skb)(struct ccat_eth_dma_fifo *const, struct sk_buff *);
	void __iomem *reg;
	const struct ccat_eth_frame *end;
	struct ccat_eth_frame *next;
	struct ccat_dma dma;
};

/**
 * same as: typedef struct _CCatInfoBlockOffs from CCatDefinitions.h
 * TODO add some checking facility outside of the linux tree
 */
struct ccat_mac_infoblock {
	u32 reserved;
	u32 mii;
	u32 tx_fifo;
	u32 mac;
	u32 rx_mem;
	u32 tx_mem;
	u32 misc;
};

/**
 * struct ccat_eth_priv - CCAT Ethernet/EtherCAT Master function (netdev)
 * @func: pointer to the parent struct ccat_function
 * @netdev: the net_device structure used by the kernel networking stack
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
	void (*free)(struct ccat_eth_priv *priv);
	bool (*tx_ready)(const struct ccat_eth_priv *);
	size_t (*rx_ready)(void *);
	struct ccat_function *func;
	struct net_device *netdev;
	struct ccat_eth_register reg;
	struct ccat_eth_dma_fifo rx_fifo;
	struct ccat_eth_dma_fifo tx_fifo;
	struct hrtimer poll_timer;
	atomic64_t rx_bytes;
	atomic64_t rx_dropped;
	atomic64_t tx_bytes;
	atomic64_t tx_dropped;
};

struct ccat_mac_register {
	/** MAC error register     @+0x0 */
	u8 frame_len_err;
	u8 rx_err;
	u8 crc_err;
	u8 link_lost_err;
	u32 reserved1;
	/** Buffer overflow errors @+0x8 */
	u8 rx_mem_full;
	u8 reserved2[7];
	/** MAC frame counter      @+0x10 */
	u32 tx_frames;
	u32 rx_frames;
	u64 reserved3;
	/** MAC fifo level         @+0x20 */
	u8 tx_fifo_level:7;
	u8 reserved4:1;
	u8 reserved5[7];
	/** TX memory full error   @+0x28 */
	u8 tx_mem_full;
	u8 reserved6[7];
	u64 reserved8[9];
	/** Connection             @+0x78 */
	u8 mii_connected;
};

static inline bool ccat_eth_tx_ready_nodma(const struct ccat_eth_priv *const priv)
{
	static const u8 TX_FIFO_LEVEL_MASK = 0x3F;
	return !(ioread8(priv->reg.mac + 0x20) & TX_FIFO_LEVEL_MASK);
}

static inline size_t ccat_eth_rx_ready_nodma(void *const frame)
{
	static const size_t overhead = sizeof(struct frame_header_nodma);
	const size_t len = le16_to_cpu(ioread16(frame));

	return (len < overhead) ? 0 : len - overhead;
}

static void ccat_eth_fifo_inc(struct ccat_eth_dma_fifo *fifo)
{
	if (++fifo->next > fifo->end)
		fifo->next = fifo->dma.virt;
}

static void ccat_eth_rx_fifo_nodma_add(struct ccat_eth_dma_fifo *const fifo,
				 void __iomem *const frame)
{
	iowrite16(0, frame);
	wmb();
}

static void ccat_eth_tx_fifo_nodma_add_free(struct ccat_eth_dma_fifo *const fifo,
				      void *const frame)
{
	struct frame_header_nodma *hdr = frame;
	/* mark frame as ready to use for tx */
	hdr->tx_flags = cpu_to_le32(CCAT_FRAME_SENT);
}

static void iofifo_copy_to_linear_skb(struct ccat_eth_dma_fifo *const fifo, struct sk_buff *skb, const size_t len)
{
	struct frame_nodma *frame = (struct frame_nodma *)fifo->next;
	memcpy_fromio(skb->data, frame->data, len);
}

static void iofifo_queue_skb(struct ccat_eth_dma_fifo *const fifo, struct sk_buff *skb)
{
	struct frame_nodma *frame = (struct frame_nodma *)fifo->next;
	const u32 addr_and_length = (void*)frame - fifo->dma.virt;

	frame->hdr.tx_flags = cpu_to_le32(0);
	frame->hdr.length = cpu_to_le16(skb->len);

	memcpy_toio(frame->data, skb->data, skb->len);
	iowrite32(addr_and_length, fifo->reg);
}

static void ccat_eth_priv_free_nodma(struct ccat_eth_priv *priv)
{
	/* reset hw fifo's */
	iowrite32(0, priv->tx_fifo.reg + 0x8);
	wmb();
}

static void ccat_eth_fifo_reset(struct ccat_eth_dma_fifo *const fifo)
{
	/* reset hw fifo */
	if (fifo->reg) {
		iowrite32(0, fifo->reg + 0x8);
		wmb();
	}

	if (fifo->add) {
		fifo->next = fifo->dma.virt;
		do {
			fifo->add(fifo, fifo->next);
			ccat_eth_fifo_inc(fifo);
		} while (fifo->next != fifo->dma.virt);
	}
}
#ifndef BUILD_CX9020
static inline bool ccat_eth_tx_ready_dma(const struct ccat_eth_priv *const priv)
{
	const struct frame_header_dma *hdr = (struct frame_header_dma *)priv->tx_fifo.next;
	return le32_to_cpu(hdr->tx_flags) & CCAT_FRAME_SENT;
}

static inline size_t ccat_eth_rx_ready_dma(void *const __frame)
{
	static const size_t overhead = sizeof(struct frame_header_dma) - offsetof(struct frame_header_dma, rx_flags);
	const struct frame_header_dma *const frame = __frame;

	if (le32_to_cpu(frame->rx_flags) & CCAT_FRAME_RECEIVED) {
		const size_t len = le16_to_cpu(frame->length);
		return (len < overhead) ? 0 : len - overhead;
	}
	return 0;
}

static void ccat_eth_rx_fifo_dma_add(struct ccat_eth_dma_fifo *const fifo,
				 void *const frame)
{
	struct frame_header_dma *hdr = frame;
	const size_t offset = frame - fifo->dma.virt;
	const u32 addr_and_length = (1 << 31) | offset;

	hdr->rx_flags = cpu_to_le32(0);
	iowrite32(addr_and_length, fifo->reg);
}

static void ccat_eth_tx_fifo_dma_add_free(struct ccat_eth_dma_fifo *const fifo,
				      void *const frame)
{
	struct frame_header_dma *hdr = frame;
	/* mark frame as ready to use for tx */
	hdr->tx_flags = cpu_to_le32(CCAT_FRAME_SENT);
}

static void dmafifo_copy_to_linear_skb(struct ccat_eth_dma_fifo *const fifo, struct sk_buff *skb, const size_t len)
{
	struct frame_dma *frame = (struct frame_dma *)fifo->next;

	skb_copy_to_linear_data(skb, frame->data, len);
}

static void dmafifo_queue_skb(struct ccat_eth_dma_fifo *const fifo, struct sk_buff *skb)
{
	struct frame_dma *frame = (struct frame_dma *)fifo->next;
	u32 addr_and_length;

	frame->hdr.tx_flags = cpu_to_le32(0);
	frame->hdr.length = cpu_to_le16(skb->len);

	memcpy(frame->data, skb->data, skb->len);

	/* Queue frame into CCAT TX-FIFO, CCAT ignores the first 8 bytes of the tx descriptor */
	addr_and_length = offsetof(struct frame_header_dma, length);
	addr_and_length += ((void *)fifo->next - fifo->dma.virt);
	addr_and_length += ((skb->len + sizeof(struct frame_header_dma)) / 8) << 24;
	iowrite32(addr_and_length, fifo->reg);
}

static void ccat_eth_priv_free_dma(struct ccat_eth_priv *priv)
{
	/* reset hw fifo's */
	iowrite32(0, priv->rx_fifo.reg + 0x8);
	iowrite32(0, priv->tx_fifo.reg + 0x8);
	wmb();

	/* release dma */
	ccat_dma_free(&priv->rx_fifo.dma);
	ccat_dma_free(&priv->tx_fifo.dma);
}

/**
 * Initalizes both (Rx/Tx) DMA fifo's and related management structures
 */
static int ccat_eth_priv_init_dma(struct ccat_eth_priv *priv)
{
	struct pci_dev *pdev = priv->func->ccat->pdev;
	priv->rx_ready = ccat_eth_rx_ready_dma;
	priv->tx_ready = ccat_eth_tx_ready_dma;
	priv->free = ccat_eth_priv_free_dma;

	if (0 != ccat_dma_init(&priv->rx_fifo.dma, priv->func->info.rx_dma_chan, priv->func->ccat->bar_2,
			       &pdev->dev)) {
		pr_info("init RX DMA memory failed.\n");
		return -1;
	}

	if (0 != ccat_dma_init(&priv->tx_fifo.dma, priv->func->info.tx_dma_chan, priv->func->ccat->bar_2,
			       &pdev->dev)) {
		pr_info("init TX DMA memory failed.\n");
		ccat_dma_free(&priv->rx_fifo.dma);
		return -1;
	}

	priv->rx_fifo.add = ccat_eth_rx_fifo_dma_add;
	priv->rx_fifo.copy_to_skb = dmafifo_copy_to_linear_skb;
	priv->rx_fifo.queue_skb = NULL;
	priv->rx_fifo.end = ((struct ccat_eth_frame *)priv->rx_fifo.dma.virt) + FIFO_LENGTH - 1;
	priv->rx_fifo.reg = priv->reg.rx_fifo;
	ccat_eth_fifo_reset(&priv->rx_fifo);

	priv->tx_fifo.add = ccat_eth_tx_fifo_dma_add_free;
	priv->tx_fifo.copy_to_skb = NULL;
	priv->tx_fifo.queue_skb = dmafifo_queue_skb;
	priv->tx_fifo.end = ((struct ccat_eth_frame *)priv->tx_fifo.dma.virt) + FIFO_LENGTH - 1;
	priv->tx_fifo.reg = priv->reg.tx_fifo;
	ccat_eth_fifo_reset(&priv->tx_fifo);

	/* disable MAC filter */
	iowrite8(0, priv->reg.mii + 0x8 + 6);
	wmb();
	return 0;
}
#endif /* #ifndef BUILD_CX9020 */

static int ccat_eth_priv_init_nodma(struct ccat_eth_priv *priv)
{
	priv->rx_ready = ccat_eth_rx_ready_nodma;
	priv->tx_ready = ccat_eth_tx_ready_nodma;
	priv->free = ccat_eth_priv_free_nodma;

	priv->rx_fifo.dma.phys = 0; /* unused */
	priv->rx_fifo.dma.channel = 0; /* unused */
	priv->rx_fifo.dma.dev = NULL; /* unused &priv->func->ccat->pdev->dev; */

	priv->rx_fifo.dma.virt = priv->reg.rx_mem;
	priv->rx_fifo.dma.size = priv->func->info.rx_size;

	priv->rx_fifo.add = ccat_eth_rx_fifo_nodma_add;
	priv->rx_fifo.copy_to_skb = iofifo_copy_to_linear_skb;
	priv->rx_fifo.queue_skb = NULL;
	priv->rx_fifo.end = priv->rx_fifo.dma.virt;
	priv->rx_fifo.reg = NULL;
	ccat_eth_fifo_reset(&priv->rx_fifo);

	priv->tx_fifo.dma.phys = 0; /* unused */
	priv->tx_fifo.dma.channel = 0; /* unused */
	priv->tx_fifo.dma.dev = NULL; /* unused &priv->func->ccat->pdev->dev; */

	priv->tx_fifo.dma.virt = priv->reg.tx_mem;
	priv->tx_fifo.dma.size = priv->func->info.tx_size;

	priv->tx_fifo.add = ccat_eth_tx_fifo_nodma_add_free;
	priv->tx_fifo.copy_to_skb = NULL;
	priv->tx_fifo.queue_skb = iofifo_queue_skb;
	priv->tx_fifo.end = priv->tx_fifo.dma.virt + priv->tx_fifo.dma.size - sizeof(*priv->tx_fifo.end);
	priv->tx_fifo.reg = priv->reg.tx_fifo;
	ccat_eth_fifo_reset(&priv->tx_fifo);

	/* disable MAC filter */
	iowrite8(0, priv->reg.mii + 0x8 + 6);
	wmb();
	return 0;
}

/**
 * Initializes a struct ccat_eth_register with data from a corresponding
 * CCAT function.
 */
static void ccat_eth_priv_init_reg(struct ccat_eth_register *const reg,
				   const struct ccat_function *const func)
{
	struct ccat_mac_infoblock offsets;
	void __iomem *const func_base = func->ccat->bar_0 + func->info.addr;

	memcpy_fromio(&offsets, func_base, sizeof(offsets));
	reg->mii = func_base + offsets.mii;
	reg->tx_fifo = func_base + offsets.tx_fifo;
	reg->rx_fifo = func_base + offsets.tx_fifo + 0x10;
	reg->mac = func_base + offsets.mac;
	reg->rx_mem = func_base + offsets.rx_mem;
	reg->tx_mem = func_base + offsets.tx_mem;
	reg->misc = func_base + offsets.misc;
}

static netdev_tx_t ccat_eth_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	struct ccat_eth_dma_fifo *const fifo = &priv->tx_fifo;

	if (skb_is_nonlinear(skb)) {
		pr_warn("Non linear skb not supported -> drop frame.\n");
		atomic64_inc(&priv->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len > MAX_PAYLOAD_SIZE) {
		pr_warn("skb.len %llu exceeds dma buffer %llu -> drop frame.\n",
			(u64) skb->len, (u64) MAX_PAYLOAD_SIZE);
		atomic64_inc(&priv->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (!priv->tx_ready(priv)) {
		netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		netif_stop_queue(priv->netdev);
		return NETDEV_TX_BUSY;
	}

	/* prepare frame in DMA memory */
	fifo->queue_skb(fifo, skb);

	/* update stats */
	atomic64_add(skb->len, &priv->tx_bytes);

	dev_kfree_skb_any(skb);

	ccat_eth_fifo_inc(fifo);
	/* stop queue if tx ring is full */
	if (!priv->tx_ready(priv)) {
		netif_stop_queue(priv->netdev);
	}
	return NETDEV_TX_OK;
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

static void ccat_eth_receive(struct net_device *const dev, const size_t len)
{
	struct sk_buff *const skb = dev_alloc_skb(len + NET_IP_ALIGN);
	struct ccat_eth_priv *const priv = netdev_priv(dev);

	if (!skb) {
		pr_info("%s() out of memory :-(\n", __FUNCTION__);
		atomic64_inc(&priv->rx_dropped);
		return;
	}
	skb->dev = dev;
	skb_reserve(skb, NET_IP_ALIGN);
	priv->rx_fifo.copy_to_skb(&priv->rx_fifo, skb, len);
	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	atomic64_add(len, &priv->rx_bytes);
	netif_rx(skb);
}

static void ccat_eth_link_down(struct net_device *const dev)
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

	ccat_eth_fifo_reset(&priv->rx_fifo);
	ccat_eth_fifo_reset(&priv->tx_fifo);

	/* TODO reset CCAT MAC register */

	ccat_eth_xmit_raw(dev, frameForwardEthernetFrames,
			  sizeof(frameForwardEthernetFrames));
	netif_carrier_on(dev);
	netif_start_queue(dev);
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

/**
 * Poll for link state changes
 */
static void poll_link(struct ccat_eth_priv *const priv)
{
	const size_t link = ccat_eth_priv_read_link_state(priv);

	if (link != netif_carrier_ok(priv->netdev)) {
		if (link)
			ccat_eth_link_up(priv->netdev);
		else
			ccat_eth_link_down(priv->netdev);
	}
}

/**
 * Poll for available rx dma descriptors in ethernet operating mode
 */
static void poll_rx(struct ccat_eth_priv *const priv)
{
	struct ccat_eth_dma_fifo *const fifo = &priv->rx_fifo;
	/* TODO omit possible deadlock in situations with heavy traffic */
	size_t len = priv->rx_ready(fifo->next);
	while (len) {
		ccat_eth_receive(priv->netdev, len);
		fifo->add(fifo, fifo->next);
		ccat_eth_fifo_inc(fifo);
		len = priv->rx_ready(fifo->next);
	}
}

/**
 * Poll for available tx dma descriptors in ethernet operating mode
 */
static void poll_tx(struct ccat_eth_priv *const priv)
{
	if (priv->tx_ready(priv)) {
		netif_wake_queue(priv->netdev);
	}
}

/**
 * Since CCAT doesn't support interrupts until now, we have to poll
 * some status bits to recognize things like link change etc.
 */
static enum hrtimer_restart poll_timer_callback(struct hrtimer *timer)
{
	struct ccat_eth_priv *const priv =
	    container_of(timer, struct ccat_eth_priv, poll_timer);

	poll_link(priv);
	poll_rx(priv);
	poll_tx(priv);
	hrtimer_forward_now(timer, POLL_TIME);
	return HRTIMER_RESTART;
}

static struct rtnl_link_stats64 *ccat_eth_get_stats64(struct net_device *dev, struct rtnl_link_stats64
						      *storage)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);
	struct ccat_mac_register mac;

	memcpy_fromio(&mac, priv->reg.mac, sizeof(mac));
	storage->rx_packets = mac.rx_frames;	/* total packets received       */
	storage->tx_packets = mac.tx_frames;	/* total packets transmitted    */
	storage->rx_bytes = atomic64_read(&priv->rx_bytes);	/* total bytes received         */
	storage->tx_bytes = atomic64_read(&priv->tx_bytes);	/* total bytes transmitted      */
	storage->rx_errors = mac.frame_len_err + mac.rx_mem_full + mac.crc_err + mac.rx_err;	/* bad packets received         */
	storage->tx_errors = mac.tx_mem_full;	/* packet transmit problems     */
	storage->rx_dropped = atomic64_read(&priv->rx_dropped);	/* no space in linux buffers    */
	storage->tx_dropped = atomic64_read(&priv->tx_dropped);	/* no space available in linux  */
	//TODO __u64    multicast;              /* multicast packets received   */
	//TODO __u64    collisions;

	/* detailed rx_errors: */
	storage->rx_length_errors = mac.frame_len_err;
	storage->rx_over_errors = mac.rx_mem_full;	/* receiver ring buff overflow  */
	storage->rx_crc_errors = mac.crc_err;	/* recved pkt with crc error    */
	storage->rx_frame_errors = mac.rx_err;	/* recv'd frame alignment error */
	storage->rx_fifo_errors = mac.rx_mem_full;	/* recv'r fifo overrun          */
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

static int ccat_eth_open(struct net_device *dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);

	hrtimer_init(&priv->poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	priv->poll_timer.function = poll_timer_callback;
	hrtimer_start(&priv->poll_timer, POLL_TIME, HRTIMER_MODE_REL);
	return 0;
}

static int ccat_eth_stop(struct net_device *dev)
{
	struct ccat_eth_priv *const priv = netdev_priv(dev);

	netif_stop_queue(dev);
	hrtimer_cancel(&priv->poll_timer);
	return 0;
}

static const struct net_device_ops ccat_eth_netdev_ops = {
	.ndo_get_stats64 = ccat_eth_get_stats64,
	.ndo_open = ccat_eth_open,
	.ndo_start_xmit = ccat_eth_start_xmit,
	.ndo_stop = ccat_eth_stop,
};

static struct ccat_eth_priv* ccat_eth_alloc_netdev(struct ccat_function *func)
{
	struct ccat_eth_priv *priv = NULL;
	struct net_device *const netdev = alloc_etherdev(sizeof(*priv));

	if (netdev) {
		priv = netdev_priv(netdev);
		priv->netdev = netdev;
		priv->func = func;
		/* ccat register mappings */
		ccat_eth_priv_init_reg(&priv->reg, func);
	}
	return priv;
}

static int ccat_eth_init_netdev(struct ccat_eth_priv *priv)
{
	/* init netdev with MAC and stack callbacks */
	memcpy_fromio(priv->netdev->dev_addr, priv->reg.mii + 8, priv->netdev->addr_len);
	priv->netdev->netdev_ops = &ccat_eth_netdev_ops;
	netif_carrier_off(priv->netdev);

	if (register_netdev(priv->netdev)) {
		pr_info("unable to register network device.\n");
		priv->free(priv);
		free_netdev(priv->netdev);
		return -1;	// TODO return better error code
	}
	pr_info("registered %s as network device.\n", priv->netdev->name);
	priv->func->private_data = priv;
	return 0;
}

#ifndef BUILD_CX9020
static int ccat_eth_probe_dma(struct ccat_function *func)
{
	struct ccat_eth_priv *priv = ccat_eth_alloc_netdev(func);

	if (!priv)
		return -ENOMEM;

	if (ccat_eth_priv_init_dma(priv)) {
		pr_warn("%s(): DMA initialization failed.\n", __FUNCTION__);
		free_netdev(priv->netdev);
		return -1;	// TODO return better error code
	}
	return ccat_eth_init_netdev(priv);
}

static void ccat_eth_remove_dma(struct ccat_function *func)
{
	struct ccat_eth_priv *const eth = func->private_data;
	unregister_netdev(eth->netdev);
	eth->free(eth);
	free_netdev(eth->netdev);
}

struct ccat_driver eth_driver = {
	.type = CCATINFO_ETHERCAT_MASTER_DMA,
	.probe = ccat_eth_probe_dma,
	.remove = ccat_eth_remove_dma,
};
#endif /* #ifndef BUILD_CX9020 */

static int ccat_eth_probe_nodma(struct ccat_function *func)
{
	struct ccat_eth_priv *priv = ccat_eth_alloc_netdev(func);

	if (!priv)
		return -ENOMEM;

	if (ccat_eth_priv_init_nodma(priv)) {
		pr_warn("%s(): memory initialization failed.\n", __FUNCTION__);
		free_netdev(priv->netdev);
		return -1;	// TODO return better error code
	}
	return ccat_eth_init_netdev(priv);
}

static void ccat_eth_remove_nodma(struct ccat_function *func)
{
	struct ccat_eth_priv *const eth = func->private_data;
	unregister_netdev(eth->netdev);
	eth->free(eth);
	free_netdev(eth->netdev);
}

struct ccat_driver eth_nodma_driver = {
	.type = CCATINFO_ETHERCAT_NODMA,
	.probe = ccat_eth_probe_nodma,
	.remove = ccat_eth_remove_nodma,
};
