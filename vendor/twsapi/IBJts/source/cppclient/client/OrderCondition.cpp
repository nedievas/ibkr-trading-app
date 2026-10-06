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
#include "OrderCondition.h"
#include "ExecutionCondition.h"
#include "MarginCondition.h"
#include "TimeCondition.h"
#include "PercentChangeCondition.h"
#include "PriceCondition.h"
#include "VolumeCondition.h"
#include "EDecoder.h"
#include "EClient.h"

const char* OrderCondition::readExternal(const char* ptr, const char* endPtr) {
	std::string connector;

	DECODE_FIELD(connector)

	conjunctionConnection(connector == "a");

	return ptr;
}

void OrderCondition::writeExternal(std::ostream & msg) const {
	ENCODE_FIELD(conjunctionConnection() ? std::string("a") : std::string("o"))
}

std::string OrderCondition::toString() {
	return conjunctionConnection() ? "<AND>" : "<OR>";
}

bool OrderCondition::conjunctionConnection() const {
	return m_isConjunctionConnection;
}

void OrderCondition::conjunctionConnection(bool isConjunctionConnection) {
	m_isConjunctionConnection = isConjunctionConnection;
}

OrderCondition::OrderConditionType OrderCondition::type() { return m_type; }

std::string OrderCondition::typeName() {
	switch (m_type) {
		case Execution:
			return "Execution";
			break;
		case Margin:
			return "Margin";
			break;
		case PercentChange:
			return "PercentChange";
			break;
		case Price:
			return "Price";
			break;
		case Time:
			return "Time";
			break;
		case Volume:
			return "Volume";
			break;
	}
	return "";
}

OrderCondition *OrderCondition::create(OrderConditionType type) {
	OrderCondition *rval = 0;

	switch (type) {
	case Execution:
		rval = new ExecutionCondition();
		break;

	case Margin:
		rval = new MarginCondition();
		break;

	case PercentChange:
		rval = new PercentChangeCondition();
		break;

	case Price:
		rval = new PriceCondition();
		break;

	case Time:
		rval = new TimeCondition();
		break;

	case Volume:
		rval = new VolumeCondition();
		break;
	}

	if (rval != 0)
		rval->m_type = type;

	return rval;
}
