/*
 * C++ TWS API Client
 *
 * Copyright (C) 2013-2026  Interactive Brokers LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "StdAfx.h"
#include "EMessage.h"
#include "ESocket.h"

#include <assert.h>

#if defined(IB_POSIX)
#include <sys/socket.h>
#endif


ESocket::ESocket() {
}

void ESocket::fd(SOCKET fd) {
    m_fd = fd;
}

ESocket::~ESocket(void) {
}

int ESocket::send(EMessage *pMsg) {
    return bufferedSend(pMsg->begin(), pMsg->end() - pMsg->begin());
}

int ESocket::bufferedSend(const char* buf, size_t sz)
{
	if( sz <= 0)
		return 0;

	if( !m_outBuffer.empty()) {
		m_outBuffer.insert( m_outBuffer.end(), buf, buf + sz);
		return sendBufferedData();
	}

	int nResult = send(buf, sz);

	if( nResult < (int)sz) {
		int sent = (std::max)( nResult, 0);
		m_outBuffer.insert( m_outBuffer.end(), buf + sent, buf + sz);
	}

	return nResult;
}

int ESocket::sendBufferedData()
{
	if( m_outBuffer.empty())
		return 0;

	int nResult = send( &m_outBuffer[0], m_outBuffer.size());
	if( nResult <= 0) {
		return nResult;
	}
	CleanupBuffer( m_outBuffer, nResult);
	return nResult;
}

int ESocket::send(const char* buf, size_t sz)
{
	if( sz <= 0)
		return 0;

	int nResult = ::send( m_fd, buf, sz, 0);

	if( nResult == -1) {
		return -1;
	}
	if( nResult <= 0) {
		return 0;
	}
	return nResult;
}

static const size_t BufferSizeHighMark = 1 * 1024 * 1024; // 1Mb

void ESocket::CleanupBuffer(std::vector<char>& buffer, int processed)
{
	assert( buffer.empty() || processed <= (int)buffer.size());

	if( buffer.empty())
		return;

	if( processed <= 0)
		return;

	if( (size_t)processed == buffer.size()) {
		if( buffer.capacity() >= BufferSizeHighMark) {
			std::vector<char>().swap(buffer);
		}
		else {
			buffer.clear();
		}
	}
	else {
		buffer.erase( buffer.begin(), buffer.begin() + processed);
	}
}

bool ESocket::isOutBufferEmpty() const
{
	return m_outBuffer.empty();
}
