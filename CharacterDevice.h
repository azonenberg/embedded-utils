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

#ifndef characterdevice_h
#define characterdevice_h

#include <embedded-utils/FIFO.h>
#include <embedded-utils/StringHelpers.h>

/**
	@file
	@author Andrew D. Zonenberg
	@brief Abstract character device base class
 */
class CharacterDevice
{
public:
	CharacterDevice()
	{}

	//Driver methods for derived classes
	virtual void PrintBinary(char ch) =0;
	virtual char BlockingRead() =0;

	//Pretty printing
public:
	void WritePadded(const char* str, int minlen, char padding, int prepad);

	void Printf(const char* format, ...)
	{
		__builtin_va_list list;
		__builtin_va_start(list, format);
		DoPrintf(this, format, list);
		__builtin_va_end(list);
	}

	void Printf(const char* format, __builtin_va_list list)
	{ DoPrintf(this, format, list); }

	//Convenience wrappers
public:
	virtual void PrintText(char ch)
	{
		if(ch == '\n')
			PrintBinary('\r');
		PrintBinary(ch);
	}

	void Write16(uint16_t n)
	{ Write((const char*)&n, 2); }

	void Write32(uint32_t n)
	{ Write((const char*)&n, 4); }

	uint32_t BlockingRead32()
	{
		uint32_t tmp;
		BlockingRead((char*)&tmp, 4);
		return tmp;
	}

	uint16_t BlockingRead16()
	{
		uint16_t tmp;
		BlockingRead((char*)&tmp, 2);
		return tmp;
	}

	void BlockingRead(char* data, uint32_t len)
	{
		for(uint32_t i=0; i<len; i++)
			data[i] = BlockingRead();
	}

	void Write(const char* data, uint32_t len)
	{
		for(uint32_t i=0; i<len; i++)
			PrintBinary(data[i]);
	}

	virtual void PrintString(const char* str)
	{
		while(*str)
			PrintText(*(str++));
	}

	virtual void Flush()
	{}
};

#endif
