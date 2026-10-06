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
#include "ExecutionCondition.h"
#include "EDecoder.h"
#include "EClient.h"

const char* ExecutionCondition::readExternal(const char* ptr, const char* endPtr) {
	if (!(ptr = OrderCondition::readExternal(ptr, endPtr)))
		return 0;

	DECODE_FIELD(m_secType)
	DECODE_FIELD(m_exchange);
	DECODE_FIELD(m_symbol);

	return ptr;
}

std::string ExecutionCondition::toString() {
	return "trade occurs for " + m_symbol + " symbol on " + m_exchange + " exchange for " + m_secType + " security type";
}

void ExecutionCondition::writeExternal(std::ostream & msg) const {
	OrderCondition::writeExternal(msg);

	ENCODE_FIELD(m_secType);
	ENCODE_FIELD(m_exchange);
	ENCODE_FIELD(m_symbol);
}

std::string ExecutionCondition::exchange() {
	return m_exchange;
}

void ExecutionCondition::exchange(const std::string &exchange) {
	m_exchange = exchange;
}

std::string ExecutionCondition::secType() {
	return m_secType;
}

void ExecutionCondition::secType(const std::string &secType) {
	m_secType = secType;
}

std::string ExecutionCondition::symbol() {
	return m_symbol;
}

void ExecutionCondition::symbol(const std::string &symbol) {
	m_symbol = symbol;
}
