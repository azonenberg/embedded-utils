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

#ifndef SpiFlashInterfaceBase_h
#define SpiFlashInterfaceBase_h

class SpiFlashInterfaceBase
{
public:
	SpiFlashInterfaceBase();

	uint32_t GetSectorSize()
	{ return m_sectorSize; }

	uint32_t GetFlashSize()
	{ return m_capacityBytes; }

protected:

	//CFI parsing
	bool ParseCFI(uint8_t* cfi);

	//SFDP parsing
	void ReadSFDP();
	virtual void ReadSFDPBlock(uint32_t addr, uint8_t* buf, uint32_t size) =0;
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
		VENDOR_MICRON	= 0x20,
		VENDOR_PUYA		= 0x85,
		VENDOR_ISSI 	= 0x9d,
		VENDOR_WINBOND	= 0xef
	} m_vendor;

	//TODO: non-DMA option

	const char* GetMicronPartName(uint16_t npart);
	const char* GetCypressPartName(uint16_t npart);
	const char* GetISSIPartName(uint16_t npart);
	const char* GetWinbondPartName(uint16_t npart);

	uint32_t m_capacityBytes;
	uint32_t m_maxWriteBlock;

	//Erase configuration
	uint8_t m_sectorEraseOpcode;
	uint32_t m_sectorSize;

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
