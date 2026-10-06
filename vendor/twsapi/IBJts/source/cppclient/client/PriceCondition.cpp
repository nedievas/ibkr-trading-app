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
#include "PriceCondition.h"
#include "EDecoder.h"
#include "EClient.h"

#include <sstream>

std::string PriceCondition::valueToString() const {
	std::stringstream tmp;

	tmp << m_price;

	return tmp.str();
}

void PriceCondition::valueFromString(const std::string & v) {
	std::stringstream tmp;

	tmp << v;
	tmp >> m_price;
}

double PriceCondition::price() {
	return m_price;
}

void PriceCondition::price(double price) {
	m_price = price;
}

std::string PriceCondition::toString() {
	return strTriggerMethod() + " " + ContractCondition::toString();
}

PriceCondition::Method PriceCondition::triggerMethod() {
	return (Method)m_triggerMethod;
}

std::string PriceCondition::strTriggerMethod() {
	static std::string mthdNames[] = { "default", "double bid/ask", "last", "double last", "bid/ask", "", "", "last of bid/ask", "mid-point" };
	int idx = triggerMethod();

	if (idx < 0 || idx >= (int)(sizeof(mthdNames) / sizeof(mthdNames[0])))
		return "";

	return mthdNames[idx];
}

void PriceCondition::triggerMethod(Method triggerMethod) {
	m_triggerMethod = triggerMethod;
}

void PriceCondition::triggerMethod(int triggerMethod) {
	m_triggerMethod = triggerMethod;
}

const char* PriceCondition::readExternal(const char* ptr, const char* endPtr) {
	if (!(ptr = ContractCondition::readExternal(ptr, endPtr)))
		return 0;

	DECODE_FIELD(m_triggerMethod)

	return ptr;
}

void PriceCondition::writeExternal(std::ostream & msg) const {
	ContractCondition::writeExternal(msg);

	ENCODE_FIELD(m_triggerMethod);
}
