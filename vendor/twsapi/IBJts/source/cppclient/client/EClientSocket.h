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
#ifndef TWS_API_CLIENT_ECLIENTSOCKET_H
#define TWS_API_CLIENT_ECLIENTSOCKET_H

#include <atomic>
#include "EClient.h"
#include "EClientMsgSink.h"
#include "ESocket.h"

class EWrapper;
class EReader;
struct EReaderSignal;

class TWSAPIDLLEXP EClientSocket : public EClient, public EClientMsgSink
{
protected:
    virtual void prepareBufferImpl(std::ostream&) const;
	virtual void prepareBuffer(std::ostream&) const;
	virtual bool closeAndSend(std::string msg, unsigned offset = 0);

public:

	explicit EClientSocket(EWrapper *ptr, EReaderSignal *pSignal = 0);
	virtual ~EClientSocket();

	// override virtual funcs from EClient
	bool eConnect(const char *host, int port, int clientId = 0, bool extraAuth = false);
	void eDisconnect(bool resetState = true);

	bool isSocketOK() const;
	SOCKET fd() const;
    bool asyncEConnect() const;
    void asyncEConnect(bool val);
    ESocket *getTransport();


private:

	bool eConnectImpl(int clientId, bool extraAuth, ConnState* stateOutPt);

private:
	void encodeMsgLen(std::string& msg, unsigned offset) const;
public:
	bool handleSocketError();
	int receive( char* buf, size_t sz);

public:
	// callback from socket
	void onSend();
	void onError();

private:

	void onClose();

private:

    std::atomic<SOCKET> m_fd;
    bool m_sockInit;
    bool m_asyncEConnect;
    EReaderSignal *m_pSignal;

//EClientMsgSink implementation
public:
    void serverVersion(int version, const char *time);

		// Register EReader for safe thread shutdown.
public:
	void registerEReader(EReader* reader);

private:
	EReader* m_pEReader{ nullptr };
};

#endif
