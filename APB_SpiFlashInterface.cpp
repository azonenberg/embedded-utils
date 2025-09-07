/***********************************************************************************************************************
*                                                                                                                      *
* embedded-utils                                                                                                       *
*                                                                                                                      *
* Copyright (c) 2020-2025 Andrew D. Zonenberg                                                                          *
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

	//If it's powered down, wake it up (Winbond)
	SetCS(0);
	SendByte(0xab);
	SendByte(0x00);
	SendByte(0x00);
	SendByte(0x00);
	SendByte(0x00);
	SetCS(1);
	g_logTimer.Sleep(250);

	//Reset the flash and wait a while
	SetCS(0);
	SendByte(0xf0);
	SetCS(1);
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
	bool shouldReadSFDP = ParseCFI(cfi);

	if(shouldReadSFDP)
	{
		ReadSFDP();
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

void APB_SpiFlashInterface::ReadSFDPBlock(uint32_t addr, uint8_t* buf, uint32_t size)
{
	SetCS(0);
	SendByte(0x5a);
	SendByte(addr >> 24);				//3-byte address regardless of addressing mode
	SendByte((addr >> 16) & 0xff);
	SendByte(addr & 0xff);
	SendByte(0x00);				//dummy byte

	for(uint16_t i=0; i<size; i++)
		buf[i] = ReadByte();
	SetCS(1);
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
