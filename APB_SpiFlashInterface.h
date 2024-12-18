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

#ifndef APB_SpiFlashInterface_h
#define APB_SpiFlashInterface_h

#if !defined(SIMULATION) && !defined(SOFTCORE_NO_IRQ)

#include <APB_SPIHostInterface.h>

#define FLASH_USE_MDMA

#ifdef FLASH_USE_MDMA
#include <peripheral/MDMA.h>
#endif

/**
	@file
	@brief Declaration of APB_SpiFlashInterface
 */
class APB_SpiFlashInterface
{
public:
	APB_SpiFlashInterface(volatile APB_SPIHostInterface* device, uint32_t clkdiv);

	void WriteEnable()
	{
		SetCS(0);
		SendByte(0x06);
		SetCS(1);
	}

	void WriteDisable()
	{
		SetCS(0);
		SendByte(0x04);
		SetCS(1);
	}

	bool EraseSector(uint32_t start);

	uint8_t GetStatusRegister1();
	uint8_t GetStatusRegister2();
	uint8_t GetConfigRegister();

	uint32_t GetEraseBlockSize()
	{ return m_sectorSize; }

	void ReadData(
		uint32_t addr,
		uint8_t* data,
		uint32_t len

		#ifdef FLASH_USE_MDMA
			, MDMAChannel* dmaChannel = nullptr
		#endif
		);

	bool WriteData(uint32_t addr, const uint8_t* data, uint32_t len);

	uint32_t GetMinWriteBlockSize()
	{ return 16; }

	uint32_t GetMaxWriteBlockSize()
	{ return m_maxWriteBlock; }

	uint32_t GetCapacity()
	{ return m_capacityBytes; }

protected:

	virtual void WaitUntilIdle()
	{
		#ifdef QSPI_CACHE_WORKAROUND

			asm("dmb st");

			while(true)
			{
				uint32_t va = m_device->status;
				uint32_t vb = m_device->status2;
				if(!va && !vb)
					break;
			}

		#else
			while(m_device->status)
			{}
		#endif
	}

	void SetCS(bool b)
	{ m_device->cs_n = b; }

	void SendByte(uint8_t data)
	{
		m_device->data = data;
		WaitUntilIdle();
	}

	uint8_t ReadByte()
	{
		m_device->data = 0;
		WaitUntilIdle();

		return m_device->data;
	}

	volatile APB_SPIHostInterface*	m_device;

	uint32_t m_capacityBytes;
	uint32_t m_maxWriteBlock;

	//Erase configuration
	uint8_t m_sectorEraseOpcode;
	uint32_t m_sectorSize;

	void ReadSFDP();
	void ReadSFDPParameter(uint16_t type, uint32_t offset, uint8_t nwords, uint8_t major, uint8_t minor);
	void ReadSFDPParameter_JEDEC(uint32_t* param, uint8_t nwords, uint8_t major, uint8_t minor);

	uint32_t GetEraseTime(uint32_t code);

	//Address mode
	enum
	{
		ADDR_3BYTE,
		ADDR_4BYTE
	} m_addressLength;

	enum vendor_t
	{
		VENDOR_CYPRESS	= 0x01,
		VENDOR_ISSI 	= 0x9d,
		VENDOR_WINBOND	= 0xef
	} m_vendor;

	//TODO: non-DMA option

	const char* GetCypressPartName(uint16_t npart);
	const char* GetISSIPartName(uint16_t npart);
	const char* GetWinbondPartName(uint16_t npart);

	//Indicates peripheral is quad capable
	bool m_quadCapable;

	//Normal fast read instruction
	uint8_t m_fastReadInstruction;

	//Quad read instruction
	bool m_quadReadAvailable;
	uint8_t m_quadReadInstruction;
	uint8_t m_quadReadDummyClocks;
};

#endif

#endif
