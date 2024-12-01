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

	//Read CFI data
	uint8_t cfi[512];
	SetCS(0);
	SendByte(0x9f);
	for(uint16_t i=0; i<512; i++)
		cfi[i] = ReadByte();
	SetCS(1);

	enum vendor_t
	{
		VENDOR_CYPRESS	= 0x01,
		VENDOR_ISSI 	= 0x9d,
		VENDOR_WINBOND	= 0xef
	};

	const char* vendor = "unknown";
	auto vid = static_cast<vendor_t>(cfi[0]);
	switch(vid)
	{
		case VENDOR_CYPRESS:
			vendor = "Cypress";
			break;

		case VENDOR_ISSI:
			vendor = "ISSI";
			break;

		case VENDOR_WINBOND:
			vendor = "Winbond";
			break;

		default:
			break;
	}

	//TODO: is all of this Cypress/Spansion/Infineon specific? the CFI stuff comes later
	g_log("Flash vendor: 0x%02x (%s)\n", cfi[0], vendor);
	m_capacityBytes = (1 << cfi[2]);
	uint32_t mbytes = m_capacityBytes / (1024 * 1024);
	uint32_t mbits = mbytes*8;
	g_log("Capacity: %d MB (%d Mb)\n", mbytes, mbits);

	//For now, none of our standard Cypress/Infineon parts support SFDP
	if(vid == VENDOR_CYPRESS)
	{
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
	}

	//but all of our ISSI and WInbond ones do
	else if( (vid == VENDOR_ISSI) || (vid == VENDOR_WINBOND) )
		ReadSFDP();
}

void APB_SpiFlashInterface::ReadSFDP()
{
	//Read SFDP data
	uint8_t sfdp[512] = {0};
	SetCS(0);
	SendByte(0x5a);
	SendByte(0x00);				//3-byte address
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
		uint32_t id = sfdp[base+0];
		uint8_t nwords = sfdp[base+3];
		uint8_t major = sfdp[base+2];
		uint8_t minor = sfdp[base+1];
		g_log("Parameter %d: ID %d, rev %d.%d, length %d words, offset %08x\n",
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
	uint8_t type,
	uint32_t offset,
	uint8_t nwords,
	uint8_t major,
	uint8_t minor)
{
	//Skip anything we don't understand
	if(type != 0)
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

void APB_SpiFlashInterface::ReadSFDPParameter_JEDEC(
	uint32_t* param,
	uint8_t nwords,
	uint8_t major,
	[[maybe_unused]] uint8_t minor)
{
	if(major != 1)
		return;
	if(nwords < 9)
		return;

	if( (param[0] >> 21) & 1)
	{
		uint8_t fastReadOpcode = (param[3] >> 8) & 0xff;
		uint8_t fastReadDummy = param[3] & 0x1f;
		g_log("1-4-4 fast read supported, opcode %02x, %d dummy clocks\n", fastReadOpcode, fastReadDummy);
	}

	switch( (param[0] >> 17) & 3)
	{
		case 0:
			g_log("3-byte addressing\n");
			break;

		case 1:
			g_log("3/4 byte switchable addressing\n");
			break;

		case 2:
			g_log("4-byte addressing\n");
			break;
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
		g_log("Type 4 sector erase: op=%02x, size=%d kB\n", param4op, param4SizeKbytes);
	}

	uint8_t param3logsize = (param[8] >> 0) & 0xff;
	uint8_t param3op = (param[8] >> 8) & 0xff;
	if(param3logsize)
	{
		uint32_t param3SizeBytes = (1 << param3logsize);
		uint32_t param3SizeKbytes = param3SizeBytes / 1024;
		g_log("Type 3 sector erase: op=%02x, size=%d kB\n", param3op, param3SizeKbytes);
	}

	uint8_t param2logsize = (param[7] >> 16) & 0xff;
	uint8_t param2op = (param[7] >> 24) & 0xff;
	if(param2logsize)
	{
		uint32_t param2SizeBytes = (1 << param2logsize);
		uint32_t param2SizeKbytes = param2SizeBytes / 1024;
		g_log("Type 2 sector erase: op=%02x, size=%d kB\n", param2op, param2SizeKbytes);
	}

	uint8_t param1logsize = (param[7] >> 0) & 0xff;
	uint8_t param1op = (param[7] >> 8) & 0xff;
	if(param1logsize)
	{
		uint32_t param1SizeBytes = (1 << param1logsize);
		uint32_t param1SizeKbytes = param1SizeBytes / 1024;
		g_log("Type 1 sector erase: op=%02x, size=%d kB\n", param1op, param1SizeKbytes);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// High level read/write algorithms

bool APB_SpiFlashInterface::EraseSector(uint32_t start)
{
	WriteEnable();

	//Erase the page
	SetCS(0);
	SendByte(0xdc);
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

void APB_SpiFlashInterface::ReadData(uint32_t addr, uint8_t* data, uint32_t len)
{
	SetCS(0);
	SendByte(0x0c);
	SendByte( (addr >> 24) & 0xff);
	SendByte( (addr >> 16) & 0xff);
	SendByte( (addr >> 8) & 0xff);
	SendByte( (addr >> 0) & 0xff);
	ReadByte();	//throw away dummy byte

	for(uint32_t i=0; i<len; i++)
		data[i] = ReadByte();

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
	SendByte(0x12);
	SendByte( (addr >> 24) & 0xff);
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

	//Check for write failure
	if( (GetStatusRegister1() & 0x40) == 0x40)
		return false;

	return true;
}

#endif
