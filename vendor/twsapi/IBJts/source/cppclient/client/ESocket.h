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
#ifndef TWS_API_CLIENT_ESOCKET_H
#define TWS_API_CLIENT_ESOCKET_H

#include "ETransport.h"
#include "EPosixClientSocketPlatform.h"
#include <vector>

class ESocket : public ETransport
{
    SOCKET m_fd;
    std::vector<char> m_outBuffer;

    int bufferedSend(const char* buf, size_t sz);
    int send(const char* buf, size_t sz);
    void CleanupBuffer(std::vector<char>& buffer, int processed);

public:
    ESocket();
    virtual ~ESocket(void);

    int send(EMessage *pMsg);
    bool isOutBufferEmpty() const;
    int sendBufferedData();
    void fd(SOCKET fd);
};

#endif