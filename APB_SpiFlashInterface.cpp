/***********************************************************************************************************************
*                                                                                                                      *
* embedded-utils                                                                                                       *
*                                                                                                                      *
* Copyright (c) 2020-2024 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/
#if !defined(SIMULATION) && !defined(SOFTCORE_NO_IRQ)

#include <core/platform.h>
#include <APB_SPIHostInterface.h>
#include "APB_SpiFlashInterface.h"

APB_SpiFlashInterface::APB_SpiFlashInterface(volatile APB_SPIHostInterface* device, uint32_t clkdiv)
	: m_device(device)
	, m_capacityBytes(0)
	, m_maxWriteBlock(0)
	, m_sectorEraseOpcode(0)
	, m_sectorSize(0)
	, m_addressLength(ADDR_3BYTE)
	, m_fastReadInstruction(0x0b)
	, m_quadReadAvailable(false)
	, m_quadReadInstruction(0)
	, m_quadReadDummyClocks(0)
{
	//Hold CS# high for a bit before trying to talk to it
	SetCS(1);
	g_logTimer.Sleep(500);

	//Reset the flash
	SetCS(0);
	SendByte(0xf0);
	SetCS(1);

	//Wait a bit
	g_logTimer.Sleep(250);

	//Set the clock divider
	m_device->clkdiv = clkdiv;

	//See if we're quad capable
	if(m_device->quad_capable)
	{
		g_log("Host PHY is QSPI capable\n");
		m_quadCapable = true;
	}
	else
	{
		g_log("Host PHY is not QSPI capable\n");
		m_quadCapable = false;
	}

	//Read CFI data
	uint8_t cfi[512];
	SetCS(0);
	SendByte(0x9f);
	for(uint16_t i=0; i<512; i++)
		cfi[i] = ReadByte();
	SetCS(1);

	const char* vendor = "unknown";
	const char* part = "unknown";
	m_vendor = static_cast<vendor_t>(cfi[0]);
	uint16_t npart = (cfi[1] << 8)  | cfi[2];
	switch(m_vendor)
	{
		case VENDOR_CYPRESS:
			vendor = "Cypress";
			part = GetCypressPartName(npart);
			break;

		case VENDOR_MICRON:
			vendor = "Micron";
			part = GetMicronPartName(npart);
			break;

		case VENDOR_ISSI:
			vendor = "ISSI";
			part = GetISSIPartName(npart);
			break;

		case VENDOR_PUYA:
			vendor = "Puya";
			//part = GetCypressPartName(npart);
			break;

		case VENDOR_WINBOND:
			vendor = "Winbond";
			part = GetWinbondPartName(npart);
			break;

		default:
			break;
	}
	g_log("Flash part: 0x%02x %02x %02x (%s %s)\n", cfi[0], cfi[1], cfi[2], vendor, part);
	m_capacityBytes = (1 << cfi[2]);
	uint32_t mbytes = m_capacityBytes / (1024 * 1024);
	uint32_t kbytes = m_capacityBytes / (1024);
	uint32_t mbits = m_capacityBytes / (1024 * 128);
	if(mbytes == 0)
		g_log("Capacity: %d kB (%d Mb)\n", kbytes, mbits);
	else
		g_log("Capacity: %d MB (%d Mb)\n", mbytes, mbits);

	//Default initialize some sector configuration
	m_sectorSize = 0;
	m_sectorEraseOpcode = 0xdc;

	switch(m_vendor)
	{
		//For now, none of our standard Cypress/Infineon parts support SFDP
		case VENDOR_CYPRESS:
			//Sector architecture
			if(cfi[4] == 0x00)
			{
				g_log("Uniform 256 kB sectors\n");
				m_sectorSize = 256 * 1024;
			}
			else
			{
				g_log("4 kB parameter + 64 kB data sectors\n");
				m_sectorSize = 64 * 1024;
			}

			if(cfi[5] == 0x80)
				g_log("Part ID: S25FL%dS%c%c\n", mbits, cfi[6], cfi[7]);

			m_maxWriteBlock = 1 << (cfi[0x2a]);
			g_log("Max write block: %d bytes\n", m_maxWriteBlock);

			//Assume 4 byte addressing
			m_addressLength = ADDR_4BYTE;
			break;

		//but all of our ISSI, Micron, and Winbond ones do
		case VENDOR_ISSI:
		case VENDOR_WINBOND:
		case VENDOR_MICRON:
			ReadSFDP();
			break;

		default:
			break;
	}

	//TODO: figure out how to do quad mode stuffs

	//If it's an ISSI part, set the QE bit in status register 1
	if(m_vendor == VENDOR_ISSI)
	{
		//Read the status register
		SetCS(0);
		SendByte(0x05);
		uint8_t sr = ReadByte();
		SetCS(1);
		//g_log("Initial status register: %02x\n", sr);

		if( (sr & 0x40) == 0)
		{
			sr |= 0x40;
			g_log("Enable QE, write %02x\n", sr);

			//Write it
			SetCS(0);
			SendByte(0x01);
			SendByte(sr);
			SetCS(1);
		}
		else
			g_log("QE bit already set\n");
	}
}

const char* APB_SpiFlashInterface::GetMicronPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0xbb19:	return "MT25QU256";
		default:		return "Unknown";
	}
}

const char* APB_SpiFlashInterface::GetCypressPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0x0217:	return "S25FS064S";
		case 0x0219:	return "S25FL256S";
		case 0x2018:	return "S25FL128S";
		default:		return "Unknown";
	}
}

const char* APB_SpiFlashInterface::GetISSIPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0x6019:	return "IS25LP256D (3.3V)";
		case 0x7019:	return "IS25WP256D (1.8V)";
		default:		return "Unknown";
	}
}

const char* APB_SpiFlashInterface::GetWinbondPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0x4014:	return "W25Q80BV";
		case 0x4018:	return "W25Q128FV/JV";
		case 0x4019:	return "W25R256JV";
		case 0x6015:	return "W25Q16DW";
		case 0x6016:	return "W25Q32FW";
		case 0x6018:	return "W25Q128FV QPI";
		case 0x7018:	return "W25Q128JV-IM/JM";
		case 0xaa21:	return "W25N01GV";			//TODO: NAND is probably not fully supported
		default:		return "Unknown";
	}
}

/**
	@brief Run a test to verify SPI bus signal integrity
 */
bool APB_SpiFlashInterface::SFDPMultipleReadTest(uint32_t niter)
{
	g_log("SFDP multiple read test (%u iterations)\n", niter);
	LogIndenter li(g_log);

	//Read SFDP data
	uint8_t golden[512] = {0};
	SetCS(0);
	SendByte(0x5a);
	SendByte(0x00);				//3-byte address regardless of addressing mode
	SendByte(0x00);
	SendByte(0x00);
	SendByte(0x00);				//dummy byte
	for(uint16_t i=0; i<512; i++)
		golden[i] = ReadByte();
	SetCS(1);

	for(uint32_t i=0; i<niter; i++)
	{
		uint8_t test[512] = {0};
		SetCS(0);
		SendByte(0x5a);
		SendByte(0x00);				//3-byte address regardless of addressing mode
		SendByte(0x00);
		SendByte(0x00);
		SendByte(0x00);				//dummy byte
		for(uint16_t j=0; j<512; j++)
			test[j] = ReadByte();
		SetCS(1);

		for(uint32_t j=0; j<512; j++)
		{
			if(test[j] != golden[j])
			{
				g_log(Logger::ERROR, "Fail on iteration %u, byte %u: got %02x, expected %02x\n",
					i, j, test[j], golden[j]);
				return false;
			}
		}
	}

	g_log("Test passed\n");
	return true;
}

void APB_SpiFlashInterface::ReadSFDP()
{
	//Read SFDP data
	uint8_t sfdp[512] = {0};
	SetCS(0);
	SendByte(0x5a);
	SendByte(0x00);				//3-byte address regardless of addressing mode
	SendByte(0x00);
	SendByte(0x00);
	SendByte(0x00);				//dummy byte

	for(uint16_t i=0; i<512; i++)
		sfdp[i] = ReadByte();
	SetCS(1);

	//See if we got valid SFDP data
	const char* expectedHeader = "SFDP";
	if(memcmp(sfdp, expectedHeader, 4) != 0)
		return;

	int sfdpMajor = sfdp[5];
	int sfdpMinor = sfdp[4];
	int sfdpParams = sfdp[6] + 1;
	g_log("Found valid SFDP %d.%d header, %d parameter header(s)\n", sfdpMajor, sfdpMinor, sfdpParams);
	LogIndenter li(g_log);

	for(int i=0; i<sfdpParams; i++)
	{
		int base = 8 + i*8;
		if(base > 504)
		{
			g_log(Logger::WARNING, "Skipping SFDP header %d (invalid offset %x)\n", i, base);
			break;
		}

		uint32_t offset =
			(sfdp[base + 6] << 16) |
			(sfdp[base + 5] << 8) |
			(sfdp[base + 4] << 0);
		uint16_t id = (sfdp[base + 7] << 8) | sfdp[base + 0];
		uint8_t nwords = sfdp[base+3];
		uint8_t major = sfdp[base+2];
		uint8_t minor = sfdp[base+1];
		g_log("Parameter %d: ID %04x, rev %d.%d, length %d words, offset %08x\n",
			i,
			id,
			major, minor,
			nwords,
			offset);

		//Read the parameter table
		LogIndenter li2(g_log);
		ReadSFDPParameter(id, offset, nwords, major, minor);
	}
}

void APB_SpiFlashInterface::ReadSFDPParameter(
	uint16_t type,
	uint32_t offset,
	uint8_t nwords,
	uint8_t major,
	uint8_t minor)
{
	//Skip anything we don't understand
	if(type != 0xff00)
		return;

	uint32_t param[256] = {0};
	SetCS(0);
	SendByte(0x5a);
	SendByte(offset >> 16);				//3-byte address
	SendByte((offset >> 8) & 0xff);
	SendByte((offset >> 0) & 0xff);
	SendByte(0x00);						//dummy byte

	for(uint16_t i=0; i<nwords; i++)
	{
		param[i] = ReadByte();
		param[i] |= ReadByte() << 8;
		param[i] |= ReadByte() << 16;
		param[i] |= ReadByte() << 24;
	}
	SetCS(1);

	ReadSFDPParameter_JEDEC(param, nwords, major, minor);
}

/**
	@brief Parse SFDP typical erase time
 */
uint32_t APB_SpiFlashInterface::GetEraseTime(uint32_t code)
{
	uint32_t timescale = 0;
	switch( (code >> 5) & 3)
	{
		case 0:
			timescale = 1;
			break;

		case 1:
			timescale = 16;
			break;

		case 2:
			timescale = 128;
			break;

		case 3:
		default:
			timescale = 1000;
			break;
	}
	return ( (code & 0x1f ) + 1) * timescale;
}

void APB_SpiFlashInterface::ReadSFDPParameter_JEDEC(
	uint32_t* param,
	uint8_t nwords,
	uint8_t major,
	[[maybe_unused]] uint8_t minor)
{
	if(major != 1)
		return;
	if(nwords < 10)
		return;

	switch( (param[0] >> 17) & 3)
	{
		case 0:
			g_log("3-byte addressing\n");
			m_fastReadInstruction = 0x0b;
			m_addressLength = ADDR_3BYTE;
			break;

		case 1:
			g_log("3/4 byte switchable addressing\n");
			m_fastReadInstruction = 0x0b;
			m_addressLength = ADDR_3BYTE;
			break;

		case 2:
			g_log("4-byte addressing\n");
			m_fastReadInstruction = 0x0c;
			m_addressLength = ADDR_4BYTE;
			break;
	}

	//Only check for quad read instructions if the peripheral is quad capable
	if(m_quadCapable)
	{
		if( (param[0] >> 21) & 1)
		{
			/*
			m_quadReadAvailable = true;
			m_quadReadInstruction = (param[2] >> 8) & 0xff;
			m_quadReadDummyClocks = param[2] & 0x1f;
			g_log("1-4-4 fast read supported, opcode %02x, %d dummy clocks\n", m_quadReadInstruction, m_quadReadDummyClocks);
			*/

			uint8_t op = (param[2] >> 8) & 0xff;
			uint8_t dummy = param[2] & 0x1f;
			g_log("1-4-4 fast read supported, opcode %02x, %d dummy clocks\n", op, dummy);
		}

		//for now we only support 1-1-4 read
		if( (param[0] >> 22) & 1)
		{
			m_quadReadAvailable = true;
			m_quadReadInstruction = (param[2] >> 24) & 0xff;
			m_quadReadDummyClocks = (param[2] >> 16) & 0x1f;
			g_log("1-1-4 fast read supported, opcode %02x, %d dummy clocks\n", m_quadReadInstruction, m_quadReadDummyClocks);

			//WORKAROUND: if this is an ISSI flash chip, it will probably claim to only have 3 byte addressing
			//even if it's >128 Mbits. Use 4FRQO 0x6c
			if( (m_vendor == VENDOR_ISSI) && (m_capacityBytes > 0x100'0000) && (m_addressLength == ADDR_3BYTE) )
			{
				g_log(
					"Buggy ISSI >128 Mbit flash which claims 3-byte address in SFDP but may be 3/4 depending on mode bits. "
					"Using 4-byte 4FRQO and 4FRD instructions instead\n");
				m_addressLength = ADDR_4BYTE;
				m_fastReadInstruction = 0x0c;
				m_quadReadInstruction = 0x6c;
			}
		}
	}

	uint8_t eraseOpcode = (param[0] >> 8) & 0xff;
	if(eraseOpcode == 0xff)
		g_log("4 kB erase not available\n");
	else
		g_log("4 kB erase opcode: %02x\n", eraseOpcode);

	uint8_t param4logsize = (param[8] >> 16) & 0xff;
	uint8_t param4op = (param[8] >> 24) & 0xff;
	if(param4logsize)
	{
		uint32_t param4SizeBytes = (1 << param4logsize);
		uint32_t param4SizeKbytes = param4SizeBytes / 1024;
		uint32_t param4EraseTimeMs = GetEraseTime(param[9] >> 25);
		g_log("Type 4 sector erase: op=%02x, size=%d kB, typical %d ms\n", param4op, param4SizeKbytes, param4EraseTimeMs);

		//TODO: support multiple sector sizes
		//For now just use the highest numbered (normally largest) one we support
		m_sectorEraseOpcode = param4op;
		m_sectorSize = param4SizeBytes;
	}

	uint8_t param3logsize = (param[8] >> 0) & 0xff;
	uint8_t param3op = (param[8] >> 8) & 0xff;
	if(param3logsize)
	{
		uint32_t param3SizeBytes = (1 << param3logsize);
		uint32_t param3SizeKbytes = param3SizeBytes / 1024;
		uint32_t param3EraseTimeMs = GetEraseTime(param[9] >> 18);
		g_log("Type 3 sector erase: op=%02x, size=%d kB, typical %d ms\n", param3op, param3SizeKbytes, param3EraseTimeMs);

		//TODO: support multiple sector sizes
		//For now just use the highest numbered (normally largest) one we support
		if(!m_sectorSize)
		{
			m_sectorEraseOpcode = param3op;
			m_sectorSize = param3SizeBytes;
		}
	}

	uint8_t param2logsize = (param[7] >> 16) & 0xff;
	uint8_t param2op = (param[7] >> 24) & 0xff;
	if(param2logsize)
	{
		uint32_t param2SizeBytes = (1 << param2logsize);
		uint32_t param2SizeKbytes = param2SizeBytes / 1024;
		uint32_t param2EraseTimeMs = GetEraseTime(param[9] >> 11);
		g_log("Type 2 sector erase: op=%02x, size=%d kB, typical %d ms\n", param2op, param2SizeKbytes, param2EraseTimeMs);

		//TODO: support multiple sector sizes
		//For now just use the highest numbered (normally largest) one we support
		if(!m_sectorSize)
		{
			m_sectorEraseOpcode = param2op;
			m_sectorSize = param2SizeBytes;
		}
	}

	uint8_t param1logsize = (param[7] >> 0) & 0xff;
	uint8_t param1op = (param[7] >> 8) & 0xff;
	if(param1logsize)
	{
		uint32_t param1SizeBytes = (1 << param1logsize);
		uint32_t param1SizeKbytes = param1SizeBytes / 1024;
		uint32_t param1EraseTimeMs = GetEraseTime(param[9] >> 4);
		g_log("Type 1 sector erase: op=%02x, size=%d kB, typical %d ms\n", param1op, param1SizeKbytes, param1EraseTimeMs);

		//TODO: support multiple sector sizes
		//For now just use the highest numbered (normally largest) one we support
		if(!m_sectorSize)
		{
			m_sectorEraseOpcode = param1op;
			m_sectorSize = param1SizeBytes;
		}
	}

	g_log("Selecting opcode 0x%02x for %d kB sector as default erase opcode\n",
		m_sectorEraseOpcode, m_sectorSize / 1024);

	uint32_t maxEraseTime = (param[10] >> 24) & 0x1f;
	switch( (param[10] >> 29) & 3)
	{
		case 0:
			maxEraseTime *= 16;
			break;

		case 1:
			maxEraseTime *= 256;
			break;

		case 2:
			maxEraseTime *= 4000;
			break;

		case 3:
		default:
			maxEraseTime *= 64000;
			break;
	}
	g_log("Full chip erase time: typical %d ms\n", maxEraseTime);

	uint32_t maxEraseScale = ((param[9] & 0xf) + 1) * 2;
	g_log("Worst case erase time is %d times typical\n", maxEraseScale);

	uint32_t pagelog = (param[10] >> 4) & 0xf;
	m_maxWriteBlock = 1 << (pagelog);
	g_log("Max write block: %d bytes\n", m_maxWriteBlock);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// High level read/write algorithms

bool APB_SpiFlashInterface::EraseSector(uint32_t start)
{
	WriteEnable();

	//Erase the page
	SetCS(0);
	SendByte(m_sectorEraseOpcode);
	if(m_addressLength == ADDR_4BYTE)
		SendByte( (start >> 24) & 0xff);
	SendByte( (start >> 16) & 0xff);
	SendByte( (start >> 8) & 0xff);
	SendByte( (start >> 0) & 0xff);
	SetCS(1);

	//Read status register and poll until write-in-progress bit is cleared
	while( (GetStatusRegister1() & 1) == 1)
	{}

	WriteDisable();

	//Check for erase failure
	if( (GetStatusRegister1() & 0x20) == 0x20)
		return false;

	//If we get here, all good
	return true;
}

uint8_t APB_SpiFlashInterface::GetStatusRegister1()
{
	SetCS(0);
	SendByte(0x05);
	uint8_t ret = ReadByte();
	SetCS(1);
	return ret;
}

uint8_t APB_SpiFlashInterface::GetStatusRegister2()
{
	SetCS(0);
	SendByte(0x07);
	uint8_t ret = ReadByte();
	SetCS(1);
	return ret;
}

uint8_t APB_SpiFlashInterface::GetConfigRegister()
{
	SetCS(0);
	SendByte(0x35);
	uint8_t ret = ReadByte();
	SetCS(1);
	return ret;
}

/**
	@brief Get the nonvolatile configuration register (may be Micron specific, TBD)
 */
uint16_t APB_SpiFlashInterface::GetNVCR()
{
	uint8_t nvcr[2] = {0};

	SetCS(0);
	SendByte(0xb5);
	nvcr[0] = ReadByte();
	nvcr[1] = ReadByte();
	SetCS(1);

	return (nvcr[1] << 8) | nvcr[0];
}

/**
	@brief Write the nonvolatile configuration register (may be Micron specific, TBD)
 */
void APB_SpiFlashInterface::WriteNVCR(uint16_t nvcr)
{
	WriteEnable();

	SetCS(0);
	SendByte(0xb1);
	SendByte(nvcr & 0xff);
	SendByte(nvcr >> 8);
	SetCS(1);

	WriteDisable();

	WaitUntilIdle();
}

/**
	@brief Write the volatile configuration register (may be Micron specific, TBD)
 */
void APB_SpiFlashInterface::WriteVCR(uint16_t vcr)
{
	WriteEnable();

	SetCS(0);
	SendByte(0x81);
	SendByte(vcr & 0xff);
	SendByte(vcr >> 8);
	SetCS(1);

	WriteDisable();

	WaitUntilIdle();
}


#ifdef HAVE_ITCM
__attribute__((section(".tcmtext")))
#endif
void APB_SpiFlashInterface::ReadData(
	uint32_t addr,
	uint8_t* data,
	uint32_t len

	#ifdef FLASH_USE_MDMA
		, MDMAChannel* dmaChannel
	#endif
	)
{
	SetCS(0);

	//If we have a 1-1-4 quad read available, use it
	if(m_quadReadAvailable)
	{
		SendByte(m_quadReadInstruction);

		if(m_addressLength == ADDR_4BYTE)
			SendByte( (addr >> 24) & 0xff);
	}

	//3 or 4-byte 1-1-1 fast read instruction as appropriate
	else
	{
		SendByte(m_fastReadInstruction);

		if(m_addressLength == ADDR_4BYTE)
			SendByte( (addr >> 24) & 0xff);
	}

	//Send rest of address
	SendByte( (addr >> 16) & 0xff);
	SendByte( (addr >> 8) & 0xff);
	SendByte( (addr >> 0) & 0xff);

	//Read and discard dummy bytes without using DMA for now
	if(m_quadReadAvailable)
	{
		m_device->quad_burst_rdlen = (m_quadReadDummyClocks / 2);
		WaitUntilIdle();
	}
	else
		ReadByte();	//throw away dummy byte

	#ifdef FLASH_USE_MDMA
		//Configure the DMA
		//TODO: we should only have to do this once, outside of the flash read or something?
		//Use 8-bit writes so we can write to unaligned buffers.
		//The FMC is slow enough that the DMA read is the bottleneck, writes won't be as much of an issue.
		//TODO: add argument so we can enable 32-bit if we know the dest is aligned??
		auto& tc = dmaChannel->GetTransferConfig();
		tc.EnableWriteBuffer();
		tc.SetSoftwareRequestMode();
		tc.EnablePackMode();
		tc.SetTriggerMode(MDMATransferConfig::MODE_LINKED_LIST);
		tc.SetSourcePointerMode(
			MDMATransferConfig::SOURCE_INCREMENT,
			MDMATransferConfig::SOURCE_INC_32,
			MDMATransferConfig::SOURCE_SIZE_32);
		tc.SetDestPointerMode(
			MDMATransferConfig::DEST_INCREMENT,
			MDMATransferConfig::DEST_INC_8,
			MDMATransferConfig::DEST_SIZE_8);
		tc.SetBufferTransactionLength(4);
		tc.SetTransferBytes(4);
		tc.SetSourceBurstLength(MDMATransferConfig::SOURCE_BURST_4);

		//For now, assume the destination is always in DTCM
		tc.SetBusConfig(MDMATransferConfig::SRC_AXI, MDMATransferConfig::DST_TCM);
	#endif

	//Read data in blocks of up to 256 bytes for better performance
	const uint32_t block = 256;
	for(uint32_t i=0; i<len; i += block)
	{
		//Get size of this block
		uint32_t curblock = block;
		if(i+block >= len)
			curblock = len - i;

		//Request the read
		//Only block until idle for x1 SPI
		//(quad supports PREADY backpressure when reading too soon)
		if(m_quadReadAvailable)
			m_device->quad_burst_rdlen = curblock;
		else
		{
			m_device->burst_rdlen = curblock;
			WaitUntilIdle();
		}

		uint32_t wordblock = curblock / 4;
		if(curblock & 3)
			wordblock ++;

		#ifdef FLASH_USE_MDMA

			tc.SetTransferBlockConfig(4, wordblock);
			tc.SetSourcePointer(&m_device->burst_rxbuf[0]);
			tc.SetDestPointer(data + i);
			tc.AppendTransfer(nullptr);
			dmaChannel->Start();
			dmaChannel->WaitIdle();

		#else

			//Read the reply
			for(uint32_t j=0; j<wordblock; j++)
			{
				uint32_t tmp = m_device->burst_rxbuf[j];

				//bounds check burst read
				uint32_t base = i + j*4;
				uint32_t rdlen = 4;
				if(base+4 >= len)
					rdlen = len - base;

				memcpy(data + base, &tmp, rdlen);
			}
		#endif

	}

	//for(uint32_t i=0; i<len; i++)
	//	data[i] = ReadByte();

	SetCS(1);
}

//TODO: read back ECCSR to verify?
bool APB_SpiFlashInterface::WriteData(uint32_t addr, const uint8_t* data, uint32_t len)
{
	//TODO: chunk big writes
	if(len > m_maxWriteBlock)
	{
		g_log(Logger::ERROR, "Length out of range\n");
		return false;
	}

	WriteEnable();

	//Write it
	SetCS(0);

	//4-byte 1-1-1 write instruction is always 0x12
	if(m_addressLength == ADDR_4BYTE)
	{
		SendByte(0x12);
		SendByte( (addr >> 24) & 0xff);
	}

	///3-byte 1-1-1 write instruction is always 0x02
	else
		SendByte(0x02);

	SendByte( (addr >> 16) & 0xff);
	SendByte( (addr >> 8) & 0xff);
	SendByte( (addr >> 0) & 0xff);
	for(uint32_t i=0; i<len; i++)
		SendByte(data[i]);
	SetCS(1);

	//Read status register and poll until write-in-progress bit is cleared
	while( (GetStatusRegister1() & 1) == 1)
	{}

	WriteDisable();

	//Check for write failure on Cypress parts
	//(this bit is block protection on ISSI, checking it will lead to false failure detections!)
	if(m_vendor == VENDOR_CYPRESS)
	{
		if( (GetStatusRegister1() & 0x40) == 0x40)
			return false;
	}

	return true;
}

#endif
