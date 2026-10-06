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
#ifndef TWS_API_CLIENT_UTILS_H
#define TWS_API_CLIENT_UTILS_H

#include <string>
#include <ctime>
#include "CommonDefs.h"
#include "Decimal.h"

class Utils {

public:
    static bool stringIsEmpty(std::string str);
    static bool isValidValue(int value);
    static bool isValidValue(long long value);
    static bool isValidValue(double value);
    static bool isValidValue(Decimal value) { return !value.isUnset(); }
    static bool isPegBenchOrder(std::string orderType);
    static bool isPegMidOrder(std::string orderType);
    static bool isPegBestOrder(std::string orderType);
    static FundDistributionPolicyIndicator getFundDistributionPolicyIndicator(std::string value);
    static FundAssetType getFundAssetType(std::string value);
    static long long currentTimeMillis();
    static OptionExerciseType getOptionExerciseType(int val);

private:
    static bool isValidValue(long value);
};

#endif

