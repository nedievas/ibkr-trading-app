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
#ifndef TWS_API_CLIENT_TIMECONDITION_H
#define TWS_API_CLIENT_TIMECONDITION_H

#include "OperatorCondition.h"

class TWSAPIDLLEXP TimeCondition : public OperatorCondition {
	friend OrderCondition;

	std::string m_time;

public:
	TimeCondition() { }

protected:
	virtual std::string valueToString() const;
	virtual void valueFromString(const std::string &v);

public:
	static const OrderConditionType conditionType = OrderConditionType::Time;

	virtual std::string toString();

	std::string time();
	void time(const std::string &time);
};

#endif