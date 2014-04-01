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
#include "module.h"
#include "print.h"

#define TESTING_ENABLED 1
void print_mem(const unsigned char *p, size_t lines)
{
#if TESTING_ENABLED
	pr_info("mem at: %p\n", p);
	pr_info(" 0  1  2  3  4  5  6  7   8  9  A  B  C  D  E  F\n");
	while (lines > 0) {
		pr_info
		    ("%02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n",
		     p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
		     p[10], p[11], p[12], p[13], p[14], p[15]);
		p += 16;
		--lines;
	}
#endif /* #if TESTING_ENABLED */
}

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
	pr_debug("Rx FIFO base address: %p\n", priv->reg.rx_fifo);
	pr_debug("     Rx Frame Header start:   0x%08x\n", rx_fifo.startAddr);
	pr_debug("     reserved:                0x%08x\n", rx_fifo.reserved1);
	pr_debug("     Rx start address valid:    %8u\n", rx_fifo.nextValid);
	pr_debug("     reserved:                0x%08x\n", rx_fifo.reserved2);
	pr_debug("     FIFO level:              0x%08x\n", rx_fifo.FifoLevel);
	pr_debug("     Buffer level:            0x%08x\n", rx_fifo.bufferLevel);
	pr_debug("     next address:            0x%08x\n", rx_fifo.nextAddr);
}

static void print_CCatDmaTxFifo(const struct ccat_eth_priv *const priv)
{
	CCatDmaTxFifo tx_fifo;
	memcpy_fromio(&tx_fifo, priv->reg.tx_fifo, sizeof(tx_fifo));
	pr_debug("Tx FIFO base address: %p\n", priv->reg.tx_fifo);
	pr_debug("     Tx Frame Header start:   0x%08x\n", tx_fifo.startAddr);
	pr_debug("     # 64 bit words:          %10d\n", tx_fifo.numQuadWords);
	pr_debug("     reserved:                0x%08x\n", tx_fifo.reserved1);
	pr_debug("     FIFO reset:              0x%08x\n", tx_fifo.fifoReset);
}

static void print_CCatInfoBlock(const CCatInfoBlock * info,
				const void __iomem * const base_addr)
{
	const size_t index = min((int)info->eCCatInfoType, CCATINFO_MAX);
	pr_debug("%s\n", CCatFunctionTypes[index]);
	pr_debug("     revision:     0x%x\n", info->nRevision);
	pr_debug("     RX channel:   %d\n", info->rxDmaChn);
	pr_debug("     TX channel:   %d\n", info->txDmaChn);
	pr_debug("     baseaddr:     0x%x\n", info->nAddr);
	pr_debug("     size:         0x%x\n", info->nSize);
	pr_debug("     subfunction:  %p\n", base_addr);
}

static void print_CCatMacRegs(const struct ccat_eth_priv *const priv)
{
	CCatMacRegs mac;
	memcpy_fromio(&mac, priv->reg.mac, sizeof(mac));
	pr_debug("MAC base address: %p\n", priv->reg.mac);
	pr_debug("     frame length error count:   %10d\n", mac.frameLenErrCnt);
	pr_debug("     RX error count:             %10d\n", mac.rxErrCnt);
	pr_debug("     CRC error count:            %10d\n", mac.crcErrCnt);
	pr_debug("     Link lost error count:      %10d\n", mac.linkLostErrCnt);
	pr_debug("     reserved:                   0x%08x\n", mac.reserved1);
	pr_debug("     RX overflow count:          %10d\n",
		 mac.dropFrameErrCnt);
	pr_debug("     DMA overflow count:         %10d\n", mac.reserved2[0]);
	//pr_debug("     reserverd:         %10d\n", DRV_NAME, mac.reserved2[1]);
	pr_debug("     TX frame counter:           %10d\n", mac.txFrameCnt);
	pr_debug("     RX frame counter:           %10d\n", mac.rxFrameCnt);
	pr_debug("     TX-FIFO level:              0x%08x\n", mac.txFifoLevel);
	pr_debug("     MII connection:             0x%08x\n", mac.miiConnected);
}

static void print_CCatMii(const struct ccat_eth_priv *const priv)
{
	CCatMii mii;
	memcpy_fromio(&mii, priv->reg.mii, sizeof(mii));
	pr_debug("MII base address: %p\n", priv->reg.mii);
	pr_debug("     MII cycle:    %s\n",
		 mii.startMiCycle ? "running" : "no cycle");
	pr_debug("     reserved:     0x%x\n", mii.reserved1);
	pr_debug("     cmd valid:    %s\n", mii.cmdErr ? "no" : "yes");
	pr_debug("     cmd:          0x%x\n", mii.cmd);
	pr_debug("     reserved:     0x%x\n", mii.reserved2);
	pr_debug("     PHY addr:     0x%x\n", mii.phyAddr);
	pr_debug("     reserved:     0x%x\n", mii.reserved3);
	pr_debug("     PHY reg:      0x%x\n", mii.phyReg);
	pr_debug("     reserved:     0x%x\n", mii.reserved4);
	pr_debug("     PHY write:    0x%x\n", mii.phyWriteData);
	pr_debug("     PHY read:     0x%x\n", mii.phyReadData);
	pr_debug("     MAC addr:     %02x:%02x:%02x:%02x:%02x:%02x\n",
		 mii.macAddr.b[0], mii.macAddr.b[1], mii.macAddr.b[2],
		 mii.macAddr.b[3], mii.macAddr.b[4], mii.macAddr.b[5]);
	pr_debug("     MAC filter enable:   %s\n",
		 mii.macFilterEnabled ? "enabled" : "disabled");
	pr_debug("     reserved:     0x%x\n", mii.reserved6);
	pr_debug("     Link State:   %s\n",
		 mii.linkStatus ? "link" : "no link");
	pr_debug("     reserved:     0x%x\n", mii.reserved7);
	//pr_debug("     reserved:     0x%x\n", DRV_NAME, mii.reserved8);
	//TODO add leds, systemtime insertion and interrupts
}

void ccat_print_function_info(struct ccat_eth_priv *priv)
{
	print_CCatInfoBlock(&priv->info, priv->ccatdev->bar[0].ioaddr);
	print_CCatMii(priv);
	print_CCatDmaTxFifo(priv);
	print_CCatDmaRxActBuf(priv);
	print_CCatMacRegs(priv);
	pr_debug("  RX window:    %p\n", priv->reg.rx_mem);
	pr_debug("  TX memory:    %p\n", priv->reg.tx_mem);
	pr_debug("  misc:         %p\n", priv->reg.misc);
}

void print_update_info(const CCatInfoBlock * const info,
		       void __iomem * const ioaddr)
{
	const size_t index = min((int)info->eCCatInfoType, CCATINFO_MAX);
	pr_debug("%s\n", CCatFunctionTypes[index]);
	pr_debug("     revision:     0x%x\n", info->nRevision);
	pr_debug("     baseaddr:     0x%x\n", info->nAddr);
	pr_debug("     size:         0x%x\n", info->nSize);
	pr_debug("     PROM ID is:   0x%x\n", ccat_get_prom_id(ioaddr));
}
