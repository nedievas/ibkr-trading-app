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
#ifndef TWS_API_CLIENT_EPOSIXCLIENTSOCKETPLATFORM_H
#define TWS_API_CLIENT_EPOSIXCLIENTSOCKETPLATFORM_H

#ifdef _WIN32
	// Windows
	// includes
	#include <WinSock2.h>
	#include <time.h>

	// defines
	#if defined(_MSC_VER)
		#if _MSC_VER < 1700
			#define EISCONN WSAEISCONN
			#define EWOULDBLOCK WSAEWOULDBLOCK
			#define ECONNREFUSED WSAECONNREFUSED
		#else
			#pragma comment(lib, "ws2_32.lib")
		#endif
	#endif

	// helpers
	inline bool SocketsInit( void) {
		WSADATA data;
		return ( !WSAStartup( MAKEWORD(2, 2), &data));
	}
	inline bool SocketsDestroy() { return ( !WSACleanup()); }
	inline int SocketClose(SOCKET sockfd) { return closesocket(sockfd); }
	inline bool SocketValid(SOCKET sockfd) { return sockfd != INVALID_SOCKET; }

	inline bool SetSocketNonBlocking(SOCKET sockfd) {
		unsigned long mode = 1;
		return ( ioctlsocket( sockfd, FIONBIO, &mode) == 0);
	}

#else
	// LINUX
	// includes

	#include <arpa/inet.h>
	#include <netdb.h>
	#include <errno.h>
	#include <sys/select.h>
	#include <sys/fcntl.h>
	#include <unistd.h>

	typedef int SOCKET;
#define INVALID_SOCKET  (SOCKET)(-1)

	// helpers
	inline bool SocketsInit() { return true; }
	inline bool SocketsDestroy() { return true; }
	inline int SocketClose(int sockfd) { return close(sockfd); }
	inline bool SocketValid(int sockfd) { return sockfd >= 0; }

	inline bool SetSocketNonBlocking(int sockfd) {
		// get socket flags
		int flags = fcntl(sockfd, F_GETFL);
		if (flags == -1)
			return false;

		// set non-blocking mode
		return ( fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0);
	}

#endif

#endif
