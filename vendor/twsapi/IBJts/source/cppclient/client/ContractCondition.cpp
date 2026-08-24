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
#include "ContractCondition.h"
#include "EDecoder.h"
#include "EClient.h"

std::string ContractCondition::toString() {
    std::string strContract = std::to_string(conId()) + "";

    return typeName() + " of " + strContract + OperatorCondition::toString();
}

const char* ContractCondition::readExternal(const char* ptr, const char* endPtr) {
	if (!(ptr = OperatorCondition::readExternal(ptr, endPtr)))
		return 0;

	DECODE_FIELD(m_conId);
	DECODE_FIELD(m_exchange);

	return ptr;
}

void ContractCondition::writeExternal(std::ostream & msg) const {
	OperatorCondition::writeExternal(msg);

	ENCODE_FIELD(m_conId);
	ENCODE_FIELD(m_exchange);
}

int ContractCondition::conId() {
	return m_conId;
}

void ContractCondition::conId(int conId) {
	m_conId = conId;
}

std::string ContractCondition::exchange() {
	return m_exchange;
}

void ContractCondition::exchange(const std::string & exchange) {
	m_exchange = exchange;
}
