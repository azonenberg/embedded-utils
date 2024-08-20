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

	//Read CFI data (TODO: support SFDP for SFDP flashes)
	//The flash on the crossbar doesn't support it
	uint8_t cfi[512];
	SetCS(0);
	SendByte(0x9f);
	for(uint16_t i=0; i<512; i++)
		cfi[i] = ReadByte();
	SetCS(1);

	const char* vendor = "unknown";
	switch(cfi[0])
	{
		case 0x01:
			vendor = "Cypress";
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
