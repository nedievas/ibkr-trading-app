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
#ifndef TWS_API_CLIENT_EXECUTION_H
#define TWS_API_CLIENT_EXECUTION_H

#include <list>
#include <string>
#include "Decimal.h"
#include "CommonDefs.h"

struct Execution
{
	Execution()
	{
		shares = ZERO_DECIMAL;
		price = 0;
		permId = 0;
		clientId = 0;
		orderId = 0;
		cumQty = ZERO_DECIMAL;
		avgPrice = 0;
		evMultiplier = 0;
        lastLiquidity = 0;
        pendingPriceRevision = false;
		optExerciseOrLapseType = OptionExerciseType::None;
	}

	std::string	execId;
	std::string	time;
	std::string	acctNumber;
	std::string	exchange;
	std::string	side;
	Decimal 	shares;
	double		price;
	long long	permId;
	int 		clientId;
	int 		orderId;
	int			liquidation;
	Decimal		cumQty;
	double		avgPrice;
	std::string	orderRef;
	std::string	evRule;
	double		evMultiplier;
	std::string modelCode;
    int         lastLiquidity;
    bool        pendingPriceRevision;
	std::string submitter;
	OptionExerciseType optExerciseOrLapseType;
};

struct ExecutionFilter
{
	ExecutionFilter()
		: m_clientId(0)
	{
	}

	// Filter fields
	int 		m_clientId;
	std::string	m_acctCode;
	std::string	m_time;
	std::string	m_symbol;
	std::string	m_secType;
	std::string	m_exchange;
	std::string	m_side;
	int         m_lastNDays = UNSET_INTEGER;
	std::list<int> m_specificDates;
};

#endif // execution_def
