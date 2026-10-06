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

#pragma once
#ifndef TWS_API_CLIENT_EREADER_H
#define TWS_API_CLIENT_EREADER_H

#include <atomic>
#include <deque>
#include "platformspecific.h"
#include "EDecoder.h"
#include "EMutex.h"
#include "EReaderOSSignal.h"

class EClientSocket;
struct EReaderSignal;
class EMessage;

class TWSAPIDLLEXP EReader
{
    EClientSocket *m_pClientSocket;
    EReaderSignal *m_pEReaderSignal;
    EDecoder processMsgsDecoder_;
    std::deque<std::shared_ptr<EMessage>> m_msgQueue;
    EMutex m_csMsgQueue;
    std::vector<char> m_buf;
    std::atomic<bool> m_isAlive;
#if defined(IB_POSIX)
    pthread_t m_hReadThread;
#elif defined(IB_WIN32)
    HANDLE m_hReadThread;
#endif
	unsigned int m_nMaxBufSize;

	void onReceive();
	void onSend();
	bool bufferedRead(char *buf, unsigned int size);

public:
    EReader(EClientSocket *clientSocket, EReaderSignal *signal);
    ~EReader(void);

protected:
	bool processNonBlockingSelect();
    std::shared_ptr<EMessage> getMsg(void);
    void readToQueue();
#if defined(IB_POSIX)
    static void * readToQueueThread(void * lpParam);
#elif defined(IB_WIN32)
    static DWORD WINAPI readToQueueThread(LPVOID lpParam);
#else
#   error "Not implemented on this platform"
#endif

    EMessage * readSingleMsg();

public:
    void processMsgs(void);
	bool putMessageToQueue();
	void start();
  void stop();
};

#endif