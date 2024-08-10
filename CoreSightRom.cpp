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

#include <embedded-utils/Logger.h>
#include <embedded-utils/CoreSightRom.h>
#include <stdint.h>

extern Logger g_log;

void PrintRomTables()
{
	g_log("Printing CoreSight ROM tables\n");
	LogIndenter li(g_log);

	//All Cortex-M7's start here (?)
	uint32_t* ppbRomBase = reinterpret_cast<uint32_t*>(0xe00f'e000);
	PrintComponent(ppbRomBase);
}

void PrintComponent(uint32_t* p)
{
	g_log("CoreSight ROM at %08x\n", reinterpret_cast<uintptr_t>(p));
	LogIndenter li(g_log);

	//Peripheral ID
	uint32_t pid[2];
	pid[1] =
		(p[1015] << 24) |
		(p[1014] << 16) |
		(p[1013] << 8) |
		p[1012];
	pid[0] =
		(p[1019] << 24) |
		(p[1018] << 16) |
		(p[1017] << 8) |
		p[1016];

	uint32_t cid =
		(p[1023] << 24) |
		(p[1022] << 16) |
		(p[1021] << 8) |
		p[1020];

	g_log("PID = %08x %08x\n", pid[1], pid[0]);
	g_log("CID = %08x\n", cid);

	//ADIv5 13.2.1
	if( (cid & 0xff) != 0x0d)
	{
		g_log(Logger::ERROR, "Invalid preamble byte in ID0 (expected 0x0d)\n");
		return;
	}

	//ADIv5 13.2.2
	uint8_t componentClass = (cid >> 12) & 0xf;
	if( ( (cid >> 8) & 0xf) != 0x0)
	{
		g_log(Logger::ERROR, "Invalid preamble nibble in ID1 (expected 0x0)\n");
		return;
	}

	//ADIv5 13.2.3
	if( ( (cid >> 16) & 0xff) != 0x05)
	{
		g_log(Logger::ERROR, "Invalid preamble byte in ID2 (expected 0x55)\n");
		return;
	}

	//ADIv5 13.2.4
	if( ( (cid >> 24) & 0xff) != 0xb1)
	{
		g_log(Logger::ERROR, "Invalid preamble byte in ID3 (expected 0xb1)\n");
		return;
	}

	//ADIv5 table 13-3
	switch(componentClass)
	{
		case 0:
			g_log("Generic verification component\n");
			break;

		case 1:
			{
				g_log("ROM table\n");
				LogIndenter li2(g_log);
				PrintRomTable(p);
			}
			break;

		case 9:
			g_log("Debug component\n");
			break;

		case 0xb:
			g_log("Peripheral Test Block\n");
			break;

		case 0xe:
			g_log("Generic IP\n");
			break;

		case 0xf:
			g_log("PrimeCell peripheral\n");
			break;

		default:
			g_log("Unknown component class 0x%02x\n", componentClass);
			break;
	}
}

void PrintRomTable(uint32_t* p)
{
	//DAP memory type
	uint32_t memtype = p[1011];
	g_log("MEMTYPE = %08x (%s)\n", memtype, (memtype & 1) ? "system memory present" : "dedicated debug bus");

	//ROM table entry
	for(int i=0; i<960; i++)
	{
		uint32_t entry = p[i];

		//Skip non-present entries (but there may be more valid ones after them
		if((entry & 1) == 0)
			continue;
		if(entry == 0)
			break;

		//Base address of the component (shift offset to be words)
		uint32_t* base = p + ( (entry & 0xfffff000) >> 2);

		if(entry & 2)
		{
			g_log("[%d] 32-bit ROM table at %08x\n", i, reinterpret_cast<uintptr_t>(base));
			PrintComponent(base);
		}
		else
		{
			g_log("[%d] 8-bit ROM table at %08x\n", i, reinterpret_cast<uintptr_t>(base));
			//TODO implement 8 bit format?
		}
	}
}
