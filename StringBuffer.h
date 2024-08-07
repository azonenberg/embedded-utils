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

#ifndef StringBuffer_h
#define StringBuffer_h

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "CharacterDevice.h"

/**
	@brief Helper for formatting strings
 */
class StringBuffer : public CharacterDevice
{
public:
	StringBuffer(char* buf, size_t size)
	: m_buf(buf)
	, m_size(size)
	, m_wptr(0)
	{
	}

	virtual void PrintBinary(char ch) override
	{
		if(m_wptr < m_size)
			m_buf[m_wptr ++] = ch;

		//Null terminate
		if(m_wptr < m_size)
			m_buf[m_wptr] = '\0';
		else
			m_buf[m_size-1] = '\0';
	}

	///@brief not used, but has to be defined because base class needs it
	virtual char BlockingRead() override
	{ return 0; }

	size_t length()
	{ return m_wptr; }

	///@brief Resets the buffer to an empty state
	void Clear()
	{
		m_wptr = 0;
		memset(m_buf, 0, m_size);
	}

protected:
	char* m_buf;
	size_t m_size;
	size_t m_wptr;
};

#endif
