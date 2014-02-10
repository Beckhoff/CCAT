#ifndef _CCAT_DEFINITIONS_H_
#define _CCAT_DEFINITIONS_H_

#ifdef __gnu_linux__
typedef unsigned char BYTE;
typedef unsigned long ULONG;
typedef unsigned short USHORT;
#endif

typedef enum
{
	CCATINFO_NOTUSED				= 0,
	CCATINFO_BLOCK					= 1,
	CCATINFO_ETHERCAT_SLAVE		= 2,
	CCATINFO_ETHERCAT_MASTER	= 3,
	CCATINFO_ETHERNET_MAC		= 4,
	CCATINFO_ETHERNET_SWITCH	= 5,
	CCATINFO_SERCOS3				= 6,
	CCATINFO_PROFIBUS				= 7,
	CCATINFO_CAN_CONTROLLER		= 8,
	CCATINFO_KBUS_MASTER			= 9,
	CCATINFO_IP_LINK				= 10,
	CCATINFO_SPI_MASTER			= 11,
	CCATINFO_I2C_MASTER			= 12,
	CCATINFO_GPIO					= 13,
	CCATINFO_DRIVEIP				= 14,
	CCATINFO_EPCS_PROM			= 15,
	CCATINFO_SYSTIME				= 16,
	CCATINFO_INTCTRL				= 17,
	CCATINFO_EEPROM				= 18,
	CCATINFO_DMA					= 19,
	CCATINFO_ETHERCAT_MASTER_DMA	= 20,
	CCATINFO_ETHERNET_MAC_DMA		= 21,
	CCATINFO_SRAM					= 22,
	CCATINFO_COPY_BLOCK			= 23,
	CCATINFO_MAX
} CCatInfoTypes;

typedef struct
{
	USHORT	eCCatInfoType;
	USHORT	nRevision;
	union
	{
		ULONG		nParam;
		struct 
		{
			BYTE nMaxEntries;
			BYTE compileDay;
			BYTE compileMonth;
			BYTE compileYear;
		};
		struct 
		{
			BYTE txDmaChn;
			BYTE rxDmaChn;
		};
		struct
		{
			BYTE		nExternalDataSize	: 2;
			BYTE		reserved1			: 6;
			BYTE		nRamSize; //size = 2^ramSize
			USHORT	reserved2;
		};
	};
	ULONG		nAddr;
	ULONG		nSize;
} CCatInfoBlock, *PCCatInfoBlock;
#endif /* #ifndef _CCAT_DEFINITIONS_H_ */

