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
#include "EReaderOSSignal.h"

#if defined(IB_POSIX)
#if defined(IBAPI_MONOTONIC_TIME)
#include <time.h>
#else
#include <sys/time.h>
#endif
#endif


EReaderOSSignal::EReaderOSSignal(unsigned long waitTimeout)
{
    bool ok = true;
    m_waitTimeout = waitTimeout;
#if defined(IB_POSIX)
    ok = ok && !pthread_mutex_init(&m_mutex, NULL);
    ok = ok && !pthread_condattr_init(&m_condattr);
#if defined(IBAPI_MONOTONIC_TIME)
    ok = ok && !pthread_condattr_setclock(&m_condattr, CLOCK_MONOTONIC);
#endif
    ok = ok && !pthread_cond_init(&m_evMsgs, &m_condattr);
    open = false;
#elif defined(IB_WIN32)
	m_evMsgs = CreateEvent(0, false, false, 0);
    ok = (NULL != m_evMsgs);
#else
#   error "Not implemented on this platform"
#endif
	if (!ok)
		throw std::runtime_error("Failed to create event");
}


EReaderOSSignal::~EReaderOSSignal(void)
{
#if defined(IB_POSIX)
    pthread_cond_destroy(&m_evMsgs);
    pthread_condattr_destroy(&m_condattr);
    pthread_mutex_destroy(&m_mutex);
#elif defined(IB_WIN32)
	CloseHandle(m_evMsgs);
#else
#   error "Not implemented on this platform"
#endif
}


void EReaderOSSignal::issueSignal() {
#if defined(IB_POSIX)
    pthread_mutex_lock(&m_mutex);
    open = true;
    pthread_cond_signal(&m_evMsgs);
    pthread_mutex_unlock(&m_mutex);
#elif defined(IB_WIN32)
	SetEvent(m_evMsgs);
#else
#   error "Not implemented on this platform"
#endif
}

void EReaderOSSignal::waitForSignal() {
#if defined(IB_POSIX)
    pthread_mutex_lock(&m_mutex);
    if (!open) {
        if ( m_waitTimeout == INFINITE ) {
            pthread_cond_wait(&m_evMsgs, &m_mutex);
        }
        else {
            struct timespec ts;
#if defined(IBAPI_MONOTONIC_TIME)
            clock_gettime(CLOCK_MONOTONIC, &ts);
#else
// on Mac OS X, clock_gettime is not available, stick to gettimeofday for the moment
            struct timeval tv;
            gettimeofday(&tv, NULL);

            ts.tv_sec = tv.tv_sec;
            ts.tv_nsec = tv.tv_usec * 1000;
#endif
            ts.tv_sec += m_waitTimeout / 1000;
            ts.tv_nsec += 1000 * 1000 * (m_waitTimeout % 1000);
            ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
            ts.tv_nsec %= (1000 * 1000 * 1000);
            pthread_cond_timedwait(&m_evMsgs, &m_mutex, &ts);
        }
    }
    open = false;
    pthread_mutex_unlock(&m_mutex);
#elif defined(IB_WIN32)
	WaitForSingleObject(m_evMsgs, m_waitTimeout);
#else
#   error "Not implemented on this platform"
#endif
}
