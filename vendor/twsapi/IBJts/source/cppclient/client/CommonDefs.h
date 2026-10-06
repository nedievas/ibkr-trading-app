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
#ifndef TWS_API_CLIENT_COMMONDEFS_H
#define TWS_API_CLIENT_COMMONDEFS_H

#include <cfloat>

#define UNSET_INTEGER INT_MAX
#define UNSET_DOUBLE DBL_MAX
#define UNSET_LLONG LLONG_MAX

// Compatibility shim: newer TWS API dropped the OrderId/TickerId typedefs
// (EWrapper now uses plain int, e.g. nextValidId(int)). The app (IBKRClient)
// still references OrderId/TickerId; alias them to int so its overrides bind.
typedef int OrderId;
typedef int TickerId;

enum faDataType { GROUPS=1, ALIASES=3 } ;

inline const char* faDataTypeStr ( faDataType pFaDataType )
{
	switch (pFaDataType) {
		case GROUPS:
			return "GROUPS";
		case ALIASES:
			return "ALIASES";
	}
	return 0 ;
}

enum MarketDataType {
	REALTIME = 1,
	FROZEN = 2,
	DELAYED = 3,
	DELAYED_FROZEN = 4
};

const std::string INFINITY_STR = "Infinity";

// FundAssetType
enum class FundAssetType {
    None,
    Others,
    MoneyMarket,
    FixedIncome,
    MultiAsset,
    Equity,
    Sector,
    Guaranteed,
    Alternative
};

// FundDistributionPolicyIndicator
enum class FundDistributionPolicyIndicator {
    None,
    AccumulationFund,
    IncomeFund
};

enum class OptionExerciseType {
    None,
    Exercise,
    Lapse,
    DoNothing,
    Assigned,
    AutoexerciseClearing,
    Expired,
    Netting,
    AutoexerciseTrading
};

#endif /* common_defs_h_INCLUDED */
