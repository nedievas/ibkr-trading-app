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
#ifndef TWS_API_CLIENT_EREADEROSSIGNAL_H
#define TWS_API_CLIENT_EREADEROSSIGNAL_H

#include "EReaderSignal.h"
#include <stdexcept>
#include "platformspecific.h"

#if !defined(INFINITE)
#define INFINITE ((unsigned long)-1)
#endif

class TWSAPIDLLEXP EReaderOSSignal :
	public EReaderSignal
{
#if defined(IB_POSIX)
    pthread_condattr_t m_condattr;
    pthread_cond_t m_evMsgs;
    pthread_mutex_t m_mutex;
    bool open;
#elif defined(IB_WIN32)
	HANDLE m_evMsgs;
#else
#   error "Not implemented on this platform"
#endif
    unsigned long m_waitTimeout; // in milliseconds

public:
	EReaderOSSignal(unsigned long waitTimeout = INFINITE);
	virtual ~EReaderOSSignal(void);

	virtual void issueSignal();
	virtual void waitForSignal();
};

#endif