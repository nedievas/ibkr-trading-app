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
#ifndef TWS_API_CLIENT_CONTRACTCONDITION_H
#define TWS_API_CLIENT_CONTRACTCONDITION_H

#include "OperatorCondition.h"

class TWSAPIDLLEXP ContractCondition : public OperatorCondition {
	int m_conId{0};
	std::string m_exchange;

public:
	virtual std::string toString();
	virtual const char* readExternal(const char* ptr, const char* endPtr);
	virtual void writeExternal(std::ostream &out) const;

	int conId();
	void conId(int conId);
	std::string exchange();
	void exchange(const std::string &exchange);
};
#endif
