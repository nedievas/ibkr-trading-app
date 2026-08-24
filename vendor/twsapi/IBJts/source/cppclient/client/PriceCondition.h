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
#ifndef TWS_API_CLIENT_PRICECONDITION_H
#define TWS_API_CLIENT_PRICECONDITION_H

#include "ContractCondition.h"

class TWSAPIDLLEXP PriceCondition : public ContractCondition {
	friend OrderCondition;

	double m_price{0.0};
	int m_triggerMethod{0};

	virtual std::string valueToString() const;
	virtual void valueFromString(const std::string &v);

public:
	PriceCondition() { };

public:
	static const OrderConditionType conditionType = OrderConditionType::Price;
	enum Method {
		Default = 0,
		DoubleBidAsk = 1,
		Last = 2,
		DoubleLast = 3,
		BidAsk = 4,
		LastBidAsk = 7,
		MidPoint = 8
	};

	double price();
	void price(double price);

	virtual std::string toString();
	virtual const char* readExternal(const char* ptr, const char* endPtr);
	virtual void writeExternal(std::ostream & out) const;

	Method triggerMethod();
	std::string strTriggerMethod();
	void triggerMethod(int triggerMethod);
	void triggerMethod(Method triggerMethod);
};

#endif