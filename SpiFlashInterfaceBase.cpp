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
#include "SpiFlashInterfaceBase.h"

SpiFlashInterfaceBase::SpiFlashInterfaceBase()
	: m_addressLength(ADDR_3BYTE)
	, m_capacityBytes(0)
	, m_maxWriteBlock(0)
	, m_sectorEraseOpcode(0)
	, m_sectorSize(0)
	, m_fastReadInstruction(0x0b)
	, m_quadReadAvailable(false)
	, m_quadReadInstruction(0)
	, m_quadReadDummyClocks(0)
{
}

/**
	@brief Parse Common Flash Interface headers

	@return True if we should read SFDP headers, false if not
 */
bool SpiFlashInterfaceBase::ParseCFI(uint8_t* cfi)
{
	//Read the basic stuff
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

	//See if we should read SFDP
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
			return false;
			break;

		//but all of our ISSI, Micron, and Winbond ones do
		case VENDOR_ISSI:
		case VENDOR_WINBOND:
		case VENDOR_MICRON:
			return true;

		//Hope anything unknown supports SFDP
		default:
			return true;
	}
}

const char* SpiFlashInterfaceBase::GetMicronPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0xbb19:	return "MT25QU256";
		default:		return "Unknown";
	}
}

const char* SpiFlashInterfaceBase::GetCypressPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0x0217:	return "S25FS064S";
		case 0x0219:	return "S25FL256S";
		case 0x2018:	return "S25FL128S";
		default:		return "Unknown";
	}
}

const char* SpiFlashInterfaceBase::GetISSIPartName(uint16_t npart)
{
	switch(npart)
	{
		case 0x6019:	return "IS25LP256D (3.3V)";
		case 0x7019:	return "IS25WP256D (1.8V)";
		default:		return "Unknown";
	}
}

const char* SpiFlashInterfaceBase::GetWinbondPartName(uint16_t npart)
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

void SpiFlashInterfaceBase::ReadSFDP()
{
	//Read SFDP data
	uint8_t sfdp[512] = {0};
	ReadSFDPBlock(0x00000000, sfdp, sizeof(sfdp));

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

void SpiFlashInterfaceBase::ReadSFDPParameter(
	uint16_t type,
	uint32_t offset,
	uint8_t nwords,
	uint8_t major,
	uint8_t minor)
{
	//Skip anything we don't understand
	if(type != 0xff00)
		return;

	//Read and parse it
	uint32_t param[256] = {0};
	ReadSFDPBlock(offset, reinterpret_cast<uint8_t*>(param), sizeof(param));
	ReadSFDPParameter_JEDEC(param, nwords, major, minor);
}

void SpiFlashInterfaceBase::ReadSFDPParameter_JEDEC(
	uint32_t* param,
	uint8_t nwords,
	uint8_t major,
	[[maybe_unused]] uint8_t minor)
{
	if(major != 1)
		return;
	if(nwords < 9)
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
			//m_quadReadAvailable = true;
			//m_quadReadInstruction = (param[2] >> 8) & 0xff;
			//m_quadReadDummyClocks = param[2] & 0x1f;
			//g_log("1-4-4 fast read supported, opcode %02x, %d dummy clocks\n", m_quadReadInstruction, m_quadReadDummyClocks);

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

	if(nwords >= 11)
	{
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
}

/**
	@brief Parse SFDP typical erase time
 */
uint32_t SpiFlashInterfaceBase::GetEraseTime(uint32_t code)
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
