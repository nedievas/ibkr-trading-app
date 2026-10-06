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
#ifndef TWS_API_CLIENT_PLATFORMSPECIFIC_H
#define TWS_API_CLIENT_PLATFORMSPECIFIC_H

#if defined(_WIN32)

#ifdef TWSAPIDLL
#ifndef TWSAPIDLLEXP
#define TWSAPIDLLEXP __declspec(dllexport)
#endif
#endif

#define assert ASSERT
#if defined(_MSC_VER) && _MSC_VER<=1800
#define snprintf _snprintf
#endif

#include <WinSock2.h>
#include <Windows.h>
#include <time.h>

#define IB_WIN32
#define atoll _atoi64

#else

#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <mutex>
#include <unistd.h> // defines _POSIX_THREADS, @see http://bit.ly/1pWJ8KQ#tag_13_80_03_02

#if defined(_POSIX_THREADS) && (_POSIX_THREADS > 0)
#include <pthread.h>
#define IB_POSIX
#if __cplusplus >= 201103L // strict C++11 standard std::mutex is available
#define IBAPI_STD_MUTEX
#endif
#else
#error "Not supported on this platform"
#endif

#endif // #ifdef _MSC_VER

#ifndef TWSAPIDLLEXP
#define TWSAPIDLLEXP
#endif

#endif
