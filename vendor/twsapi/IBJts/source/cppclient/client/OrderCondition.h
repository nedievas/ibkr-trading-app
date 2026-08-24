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
#ifndef TWS_API_CLIENT_ORDERCONDITION_H
#define TWS_API_CLIENT_ORDERCONDITION_H

#include "IExternalizable.h"
#include <string>
#include "platformspecific.h"


class TWSAPIDLLEXP OrderCondition : public IExternalizable {
public:
	enum OrderConditionType {
		Price = 1,
		Time = 3,
		Margin = 4,
		Execution = 5,
		Volume = 6,
		PercentChange = 7
	};

private:
	OrderConditionType m_type{Price};
	bool m_isConjunctionConnection{false};

public:
	virtual ~OrderCondition() {}
	virtual const char* readExternal(const char* ptr, const char* endPtr);
	virtual void writeExternal(std::ostream &out) const;

	virtual std::string toString();
	bool conjunctionConnection() const;
	void conjunctionConnection(bool isConjunctionConnection);
	OrderConditionType type();
	std::string typeName();

	static OrderCondition *create(OrderConditionType type);
};

#endif