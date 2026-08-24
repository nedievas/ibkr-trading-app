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
#ifndef TWS_API_CLIENT_TWSSOCKETCLIENTERRORS_H
#define TWS_API_CLIENT_TWSSOCKETCLIENTERRORS_H

#include <string>

static const int NO_VALID_ID = -1;
static const int NO_VALID_ERROR_CODE = 0;

class CodeMsgPair {
public:
	CodeMsgPair(int code, std::string msg) : m_errorCode(code), m_errorMsg(msg) {
	}
private:
	int m_errorCode;
	std::string m_errorMsg;
public:
	int	  code() const			        { return m_errorCode; }
	const std::string& msg() const			{ return m_errorMsg;  }
};

static const CodeMsgPair ALREADY_CONNECTED(501,	"Already connected.");
static const CodeMsgPair CONNECT_FAIL(502, "Couldn't connect to TWS. Confirm that \"Enable ActiveX and Socket Clients\" "
											"is enabled and connection port is the same as \"Socket Port\" on the "
											"TWS \"Edit->Global Configuration...->API->Settings\" menu. Live Trading ports: "
											"TWS: 7496; IB Gateway: 4001. Simulated Trading ports: TWS: 7497; IB Gateway: 4002. "
											"Verify that the maximum API connection threshold (default 32) is not exceeded.");
static const CodeMsgPair UPDATE_TWS(503, "The TWS is out of date and must be upgraded.");
static const CodeMsgPair NOT_CONNECTED(504, "Not connected");
static const CodeMsgPair UNKNOWN_ID(505, "Fatal Error: Unknown message id.");
static const CodeMsgPair UNSUPPORTED_VERSION(506, "Unsupported version");
static const CodeMsgPair BAD_LENGTH(507, "Bad message length");
static const CodeMsgPair BAD_MESSAGE(508, "Bad message");
static const CodeMsgPair SOCKET_EXCEPTION(509, "Exception caught while reading socket - ");
static const CodeMsgPair FAIL_CREATE_SOCK(520, "Failed to create socket");
static const CodeMsgPair INVALID_SYMBOL(579, "Invalid symbol in string - ");
static const CodeMsgPair FA_PROFILE_NOT_SUPPORTED(585, "FA Profile is not supported anymore, use FA Group instead - ");
static const CodeMsgPair ERROR_ENCODING_PROTOBUF(588, "Error encoding protobuf - ");

#endif
