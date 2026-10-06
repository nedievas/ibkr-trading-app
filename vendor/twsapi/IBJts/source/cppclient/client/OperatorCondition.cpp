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
#include "OperatorCondition.h"
#include "EDecoder.h"
#include "EClient.h"

const char* OperatorCondition::readExternal(const char* ptr, const char* endPtr) {
	if (!(ptr = OrderCondition::readExternal(ptr, endPtr)))
		return 0;

	DECODE_FIELD(m_isMore);

	std::string str;

	DECODE_FIELD(str);

	valueFromString(str);

	return ptr;
}

std::string OperatorCondition::toString() {
	return " is " + std::string(isMore() ? ">= " : "<= ") + valueToString();
}

void OperatorCondition::writeExternal(std::ostream & msg) const {
	OrderCondition::writeExternal(msg);

	ENCODE_FIELD(m_isMore);
	ENCODE_FIELD(valueToString());
}

bool OperatorCondition::isMore() {
	return m_isMore;
}

void OperatorCondition::isMore(bool isMore) {
	m_isMore = isMore;
}
