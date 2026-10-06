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
#ifndef TWS_API_CLIENT_OPERATORCONDITION_H
#define TWS_API_CLIENT_OPERATORCONDITION_H

#include "OrderCondition.h"

class TWSAPIDLLEXP OperatorCondition : public OrderCondition {
	bool m_isMore{false};

protected:
	virtual std::string valueToString() const = 0;
	virtual void valueFromString(const std::string &v) = 0;

public:
	virtual const char* readExternal(const char* ptr, const char* endPtr);
	virtual std::string toString();
	virtual void writeExternal(std::ostream &out) const;

	bool isMore();
	void isMore(bool isMore);
};

#endif