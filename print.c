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

#include <linux/kernel.h>
#include "CCatDefinitions.h"
#include "ccat.h"
#include "print.h"

#define TESTING_ENABLED 0
#if TESTING_ENABLED
static void print_mem(const unsigned char *p, size_t lines)
{
	printk(KERN_INFO "%s: mem at: %p\n", DRV_NAME, p);
	while (lines > 0) {
		printk(KERN_INFO
		       "%s: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n",
		       DRV_NAME, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
		       p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
		p += 16;
		--lines;
	}
}
#endif /* #if TESTING_ENABLED */

static const char *CCatFunctionTypes[CCATINFO_MAX + 1] = {
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
	printk(KERN_INFO "%s: Rx FIFO base address: %p\n", DRV_NAME,
	       priv->reg.rx_fifo);
	printk(KERN_INFO "%s:     Rx Frame Header start:   0x%08x\n", DRV_NAME,
	       rx_fifo.startAddr);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME,
	       rx_fifo.reserved1);
	printk(KERN_INFO "%s:     Rx start address:        %s\n", DRV_NAME,
	       rx_fifo.nextValid ? "valid" : "invalid");
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME,
	       rx_fifo.reserved2);
	printk(KERN_INFO "%s:     FIFO level:              0x%08x\n", DRV_NAME,
	       rx_fifo.FifoLevel);
	printk(KERN_INFO "%s:     Buffer level:            0x%08x\n", DRV_NAME,
	       rx_fifo.bufferLevel);
	printk(KERN_INFO "%s:     next address:            0x%08x\n", DRV_NAME,
	       rx_fifo.nextAddr);
}

static void print_CCatDmaTxFifo(const struct ccat_eth_priv *const priv)
{
	CCatDmaTxFifo tx_fifo;
	memcpy_fromio(&tx_fifo, priv->reg.tx_fifo, sizeof(tx_fifo));
	printk(KERN_INFO "%s: Tx FIFO base address: %p\n", DRV_NAME,
	       priv->reg.tx_fifo);
	printk(KERN_INFO "%s:     Tx Frame Header start:   0x%08x\n", DRV_NAME,
	       tx_fifo.startAddr);
	printk(KERN_INFO "%s:     # 64 bit words:          %10d\n", DRV_NAME,
	       tx_fifo.numQuadWords);
	printk(KERN_INFO "%s:     reserved:                0x%08x\n", DRV_NAME,
	       tx_fifo.reserved1);
	printk(KERN_INFO "%s:     FIFO reset:              0x%08x\n", DRV_NAME,
	       tx_fifo.fifoReset);
}

static void print_CCatInfoBlock(const CCatInfoBlock * pInfo,
				const void __iomem * const base_addr)
{
	const size_t index = min((int)pInfo->eCCatInfoType, CCATINFO_MAX);
	printk(KERN_INFO "%s: %s\n", DRV_NAME, CCatFunctionTypes[index]);
	printk(KERN_INFO "%s:     revision:     0x%x\n", DRV_NAME,
	       pInfo->nRevision);
	printk(KERN_INFO "%s:     RX channel:   %d\n", DRV_NAME,
	       pInfo->rxDmaChn);
	printk(KERN_INFO "%s:     TX channel:   %d\n", DRV_NAME,
	       pInfo->txDmaChn);
	printk(KERN_INFO "%s:     baseaddr:     0x%lx\n", DRV_NAME,
	       pInfo->nAddr);
	printk(KERN_INFO "%s:     size:         0x%lx\n", DRV_NAME,
	       pInfo->nSize);
	printk(KERN_INFO "%s:     subfunction:  %p\n", DRV_NAME, base_addr);
}

static void print_CCatMacRegs(const struct ccat_eth_priv *const priv)
{
	CCatMacRegs mac;
	memcpy_fromio(&mac, priv->reg.mac, sizeof(mac));
	printk(KERN_INFO "%s: MAC base address: %p\n", DRV_NAME, priv->reg.mac);
	printk(KERN_INFO "%s:     frame length error count:   %10d\n", DRV_NAME,
	       mac.frameLenErrCnt);
	printk(KERN_INFO "%s:     RX error count:             %10d\n", DRV_NAME,
	       mac.rxErrCnt);
	printk(KERN_INFO "%s:     CRC error count:            %10d\n", DRV_NAME,
	       mac.crcErrCnt);
	printk(KERN_INFO "%s:     Link lost error count:      %10d\n", DRV_NAME,
	       mac.linkLostErrCnt);
	printk(KERN_INFO "%s:     reserved:                   0x%08x\n",
	       DRV_NAME, mac.reserved1);
	printk(KERN_INFO "%s:     RX overflow count:          %10d\n", DRV_NAME,
	       mac.dropFrameErrCnt);
	printk(KERN_INFO "%s:     DMA overflow count:         %10d\n", DRV_NAME,
	       mac.reserved2[0]);
	//printk(KERN_INFO "%s:     reserverd:         %10d\n", DRV_NAME, mac.reserved2[1]);
	printk(KERN_INFO "%s:     TX frame counter:           %10d\n", DRV_NAME,
	       mac.txFrameCnt);
	printk(KERN_INFO "%s:     RX frame counter:           %10d\n", DRV_NAME,
	       mac.rxFrameCnt);
	printk(KERN_INFO "%s:     TX-FIFO level:              0x%08x\n",
	       DRV_NAME, mac.txFifoLevel);
	printk(KERN_INFO "%s:     MII connection:             0x%08x\n",
	       DRV_NAME, mac.miiConnected);
}

static void print_CCatMii(const struct ccat_eth_priv *const priv)
{
	CCatMii mii;
	memcpy_fromio(&mii, priv->reg.mii, sizeof(mii));
	printk(KERN_INFO "%s: MII base address: %p\n", DRV_NAME, priv->reg.mii);
	printk(KERN_INFO "%s:     MII cycle:    %s\n", DRV_NAME,
	       mii.startMiCycle ? "running" : "no cycle");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved1);
	printk(KERN_INFO "%s:     cmd valid:    %s\n", DRV_NAME,
	       mii.cmdErr ? "no" : "yes");
	printk(KERN_INFO "%s:     cmd:          0x%x\n", DRV_NAME, mii.cmd);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved2);
	printk(KERN_INFO "%s:     PHY addr:     0x%x\n", DRV_NAME, mii.phyAddr);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved3);
	printk(KERN_INFO "%s:     PHY reg:      0x%x\n", DRV_NAME, mii.phyReg);
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved4);
	printk(KERN_INFO "%s:     PHY write:    0x%x\n", DRV_NAME,
	       mii.phyWriteData);
	printk(KERN_INFO "%s:     PHY read:     0x%x\n", DRV_NAME,
	       mii.phyReadData);
	printk(KERN_INFO
	       "%s:     MAC addr:     %02x:%02x:%02x:%02x:%02x:%02x\n",
	       DRV_NAME, mii.macAddr.b[0], mii.macAddr.b[1], mii.macAddr.b[2],
	       mii.macAddr.b[3], mii.macAddr.b[4], mii.macAddr.b[5]);
	printk(KERN_INFO "%s:     MAC filter:   %s\n", DRV_NAME,
	       mii.macFilterEnabled ? "enabled" : "disabled");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved6);
	printk(KERN_INFO "%s:     Link State:   %s\n", DRV_NAME,
	       mii.linkStatus ? "link" : "no link");
	printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME,
	       mii.reserved7);
	//printk(KERN_INFO "%s:     reserved:     0x%x\n", DRV_NAME, mii.reserved8);
	//TODO add leds, systemtime insertion and interrupts
}

void ccat_print_function_info(struct ccat_eth_priv *priv)
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
