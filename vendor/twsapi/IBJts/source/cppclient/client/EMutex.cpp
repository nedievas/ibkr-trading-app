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
#include "EMutex.h"

EMutex::EMutex() {
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    pthread_mutex_init(&cs, NULL);
# endif
#elif defined(IB_WIN32)
    InitializeCriticalSection(&cs);
#else
#   error "Not implemented on this platform"
#endif
}

EMutex::~EMutex(void) {
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    pthread_mutex_destroy(&cs);
# endif
#elif defined(IB_WIN32)
    DeleteCriticalSection(&cs);
#else
#   error "Not implemented on this platform"
#endif
}

bool EMutex::TryEnter() {
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    return pthread_mutex_trylock(&cs) == 0;
# else
    return cs.try_lock();
# endif
#elif defined(IB_WIN32)
    return TryEnterCriticalSection(&cs);
#else
#   error "Not implemented on this platform"
#endif
}

void EMutex::Enter() {
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    pthread_mutex_lock(&cs);
# else
    cs.lock();
# endif
#elif defined(IB_WIN32)
    EnterCriticalSection(&cs);
#else
#   error "Not implemented on this platform"
#endif
}

void EMutex::Leave() {
#if defined(IB_POSIX)
# if !defined(IBAPI_STD_MUTEX)
    pthread_mutex_unlock(&cs);
# else
    cs.unlock();
# endif
#elif defined(IB_WIN32)
    LeaveCriticalSection(&cs);
#else
#   error "Not implemented on this platform"
#endif
}


EMutexGuard::EMutexGuard(EMutex& m) : m_mutex(m) {
    m_mutex.Enter();
}

EMutexGuard::~EMutexGuard() {
    m_mutex.Leave();
}

