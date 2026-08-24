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
#ifndef TWS_API_CLIENT_EMUTEX_H
#define TWS_API_CLIENT_EMUTEX_H

#if defined(IB_POSIX) && defined(IBAPI_STD_MUTEX)
# include  <mutex>
#endif

#include "platformspecific.h"
#ifdef IB_WIN32
#include <Windows.h>
#endif


class TWSAPIDLLEXP EMutex
{
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    pthread_mutex_t cs;
# else
    std::mutex cs;
# endif
#elif defined(IB_WIN32)
    CRITICAL_SECTION cs;
#else
#error "Not implemented on this platform"
#endif

public:
    EMutex();
    ~EMutex();
    bool TryEnter();
    void Enter();
    void Leave();
};


class TWSAPIDLLEXP EMutexGuard
{
    EMutex& m_mutex;
public:
    EMutexGuard(EMutex& m);
    ~EMutexGuard();

private:
    // disable copy ctor (compatible with pre C++11 compiler hence =delete not used)
    EMutexGuard(const EMutex&);
};

#endif