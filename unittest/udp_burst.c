// SPDX-License-Identifier: MIT
/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014 - 2018 Beckhoff Automation GmbH & Co. KG
    Author: Patrick Bruenn <p.bruenn@beckhoff.com>
*/

#include <pcap.h>
#include <stdio.h>
#include <stdint.h>

/**
 * EtherCAT frame to enable forwarding on EtherCAT Terminals
 */
static const uint8_t frameForwardEthernetFrames[] = {
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

int main()
{
	int i;
	char pcap_errbuf[PCAP_ERRBUF_SIZE];
	pcap_errbuf[0]='\0';
	pcap_t* pcap=pcap_open_live("eth2", 96, 0, 0, pcap_errbuf);
	if (pcap_errbuf[0]!='\0') {
		fprintf(stderr,"%s",pcap_errbuf);
	}
	if (!pcap) {
		return 1;
	}
	for (i = 0; i < 1000; ++i) {
		if (pcap_inject(pcap,&frameForwardEthernetFrames,sizeof(frameForwardEthernetFrames))==-1) {
			pcap_perror(pcap,0);
		}
	}
	pcap_close(pcap);
}
