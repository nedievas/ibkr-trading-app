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
#ifndef TWS_API_CLIENT_SCANNERSUBSCRIPTION_H
#define TWS_API_CLIENT_SCANNERSUBSCRIPTION_H

#include <float.h>
#include <limits.h>
#include <string>

#define NO_ROW_NUMBER_SPECIFIED -1;

struct ScannerSubscription {
	ScannerSubscription() {
		numberOfRows = NO_ROW_NUMBER_SPECIFIED;
		abovePrice = DBL_MAX;
		belowPrice = DBL_MAX;
		aboveVolume = INT_MAX;
		marketCapAbove = DBL_MAX;
		marketCapBelow = DBL_MAX;
		couponRateAbove = DBL_MAX;
		couponRateBelow = DBL_MAX;
		excludeConvertible = INT_MAX;
		averageOptionVolumeAbove = INT_MAX;
	}
    int numberOfRows;
    std::string instrument;
    std::string locationCode;
    std::string scanCode;
    double abovePrice;
    double belowPrice;
    int aboveVolume;
    double marketCapAbove;
    double marketCapBelow;
    std::string moodyRatingAbove;
    std::string moodyRatingBelow;
    std::string spRatingAbove;
    std::string spRatingBelow;
    std::string maturityDateAbove;
    std::string maturityDateBelow;
    double couponRateAbove;
    double couponRateBelow;
    int excludeConvertible;
	int averageOptionVolumeAbove;
	std::string scannerSettingPairs;
	std::string stockTypeFilter;
};

#endif
