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

#include <core/platform.h>
#include "QuadSPI_SpiFlashInterface.h"

#ifdef HAVE_QUADSPI

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

QuadSPI_SpiFlashInterface::QuadSPI_SpiFlashInterface(volatile quadspi_t* lane, uint32_t sizeBytes, uint8_t prescale)
	: QuadSPI(lane, sizeBytes, prescale)
{
	//QUADSPI block is always quad capable
	//TODO: arguments to say we didn't wire up the board in quad mode??
	m_quadCapable = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

void QuadSPI_SpiFlashInterface::Discover()
{
	g_log("Identifying QSPI flash\n");
	LogIndenter li(g_log);

	//Save existing configuration
	uint32_t ccr = m_ccrBase;

	//Wait a little while with stable pins before talking to it
	g_logTimer.Sleep(500);

	//Reset it, then wait a bit longer (use two different resets depending on what we might have)
	SendSingleByteCommand(0xf0);
	g_logTimer.Sleep(250);
	SendSingleByteCommand(0xff);
	g_logTimer.Sleep(250);

	//Read CFI data
	uint8_t cfi[512];
	SetAddressMode(MODE_NONE, 0);
	SetDataMode(MODE_SINGLE);
	SetAltBytesMode(MODE_NONE);
	SetDummyCycleCount(0);
	BlockingRead(0x9f, 0x0, cfi, 512);
	bool shouldReadSFDP = ParseCFI(cfi);

	//5 bit device size field, offset by 1
	//TODO: can this be done in a less ugly fashion?
	int nbits = 0;
	for(int i=0; i<32; i++)
	{
		if( (m_capacityBytes >> i) & 1)
			nbits = i;
	}
	nbits --;
	m_lane->DCR = (nbits << 16);

	if(shouldReadSFDP)
		ReadSFDP();

	//Enable quad mode bit in volatile configuration register.
	EnableQuadMode();

	//Restore default hardware config
	m_ccrBase = ccr;
}

void QuadSPI_SpiFlashInterface::ReadSFDPBlock(uint32_t addr, uint8_t* buf, uint32_t size)
{
	//SFDP always uses this configuration no matter how we have data bus configured otherwise
	uint32_t ccr = m_ccrBase;
	SetAddressMode(MODE_SINGLE, 3);
	SetDataMode(MODE_SINGLE);
	SetAltBytesMode(MODE_NONE);
	SetDummyCycleCount(8);

	BlockingRead(0x5a, addr, buf, size);

	m_ccrBase = ccr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flash access

void QuadSPI_SpiFlashInterface::EnableQuadMode()
{
	//winbond flash has quad enable as bit 1 of status register 2
	//TODO: this is for w25q80bv may not be universal?
	if(m_vendor == VENDOR_WINBOND)
	{
		SetAddressMode(QuadSPI::MODE_NONE, 0);
		SetDataMode(QuadSPI::MODE_SINGLE);

		//Read status register as two separate read commands
		uint8_t sr[2];
		BlockingRead(0x05, 0, &sr[0], 1);
		BlockingRead(0x35, 0, &sr[1], 1);
		if( (sr[1] & 0x2) == 0)
		{
			sr[1] |= 0x2;
			g_log("Enable QE, write %02x %02x\n", sr[0], sr[1]);

			BlockingWrite(0x01, 0, sr, 2);
		}
		else
			g_log("QE bit already set\n");
	}
}

void QuadSPI_SpiFlashInterface::MemoryMap()
{
	SetInstructionMode(QuadSPI::MODE_SINGLE);
	SetAddressMode(QuadSPI::MODE_SINGLE, 3);

	//for now run in single mode
	SetDataMode(QuadSPI::MODE_SINGLE);
	SetDummyCycleCount(8);
	SetMemoryMapMode(m_fastReadInstruction);

	//TODO: get this working
	/*
	SetDataMode(QuadSPI::MODE_QUAD);
	SetDummyCycleCount(m_quadReadDummyClocks);
	SetMemoryMapMode(m_quadReadInstruction);
	*/
}

void QuadSPI_SpiFlashInterface::EraseSector(uint8_t* addr)
{
	//Turn off memory map mode if active
	Abort();

	//Save existing config
	uint32_t ccr = m_ccrBase;

	//Send write enable with no arguments
	SendSingleByteCommand(0x06);

	//Erase the block (in x1 mode with no data)
	SetAddressMode(QuadSPI::MODE_SINGLE, 3);
	SetDataMode(QuadSPI::MODE_NONE);
	SetDummyCycleCount(0);
	BlockingWrite(m_sectorEraseOpcode, reinterpret_cast<uint32_t>(addr) & 0x0fffffff, nullptr, 0);
	PollUntilWriteDone();

	m_ccrBase = ccr;

	//Write disable is automatic, no need to do this manually
	//Return to normal operation mode
	MemoryMap();

	//Flush cache
	CleanDataCache(addr, m_sectorSize);
}

void QuadSPI_SpiFlashInterface::PollUntilWriteDone()
{
	SetAddressMode(QuadSPI::MODE_NONE, 0);
	SetDataMode(QuadSPI::MODE_SINGLE);
	uint8_t status = 1;
	while(status & 3)	//we want write enable latch and busy bits clear
		BlockingRead(0x05, 0, &status, 1);
}

void QuadSPI_SpiFlashInterface::Write(uint8_t* addr, const uint8_t* buf, uint32_t size)
{
	//Turn off memory map mode if active
	Abort();

	//Write it in blocks of up to 32 bytes for now
	//(need to account for page alignment long term)
	SetDummyCycleCount(0);
	const uint32_t maxPage = 32;
	for(size_t base = 0; base < size; base += maxPage)
	{
		uint32_t bytesLeft = size - base;
		uint32_t blockSize = bytesLeft;
		if(blockSize > maxPage)
			blockSize = maxPage;

		//Send write enable with no arguments
		SendSingleByteCommand(0x06);

		//Do the actual write
		BlockingWrite(0x02, reinterpret_cast<uint32_t>(addr + base) & 0x0fffffff, buf + base, bytesLeft);
		PollUntilWriteDone();
	}

	//Write disable is automatic, no need to do this manually
	//Return to normal operation mode
	MemoryMap();

	//Flush caches in case we had old flash data in there
	CleanDataCache(addr, size);
}

#endif
