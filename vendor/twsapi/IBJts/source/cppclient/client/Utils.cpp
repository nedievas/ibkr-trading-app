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
#include "Utils.h"

bool Utils::stringIsEmpty(std::string str) {
    return str.empty();
}

bool Utils::isValidValue(int value) {
    return value != UNSET_INTEGER;
}

bool Utils::isValidValue(long long value) {
    return value != UNSET_LLONG;
}

bool Utils::isValidValue(double value) {
    return value != UNSET_DOUBLE;
}

bool Utils::isPegBenchOrder(std::string orderType) {
    return orderType == "PEG BENCH" || orderType == "PEGBENCH";
}

bool Utils::isPegMidOrder(std::string orderType) {
    return orderType == "PEG MID" || orderType == "PEGMID";
}

bool Utils::isPegBestOrder(std::string orderType) {
    return orderType == "PEG BEST" || orderType == "PEGBEST";
}

FundAssetType Utils::getFundAssetType(std::string value) {
    if (value == "000") {
        return FundAssetType::Others;
    }
    else if (value == "001") {
        return FundAssetType::MoneyMarket;
    }
    else if (value == "002") {
        return FundAssetType::FixedIncome;
    }
    else if (value == "003") {
        return FundAssetType::MultiAsset;
    }
    else if (value == "004") {
        return FundAssetType::Equity;
    }
    else if (value == "005") {
        return FundAssetType::Sector;
    }
    else if (value == "006") {
        return FundAssetType::Guaranteed;
    }
    else if (value == "007") {
        return FundAssetType::Alternative;
    }
    return FundAssetType::None;
}

FundDistributionPolicyIndicator Utils::getFundDistributionPolicyIndicator(std::string value) {
    if (value == "N") {
        return FundDistributionPolicyIndicator::AccumulationFund;
    }
    else if (value == "Y") {
        return FundDistributionPolicyIndicator::IncomeFund;
    }
    return FundDistributionPolicyIndicator::None;
}

long long Utils::currentTimeMillis() {
    return time(NULL) * 1000LL;
}

OptionExerciseType Utils::getOptionExerciseType(int val) {
    if (val == -1) {
        return OptionExerciseType::None;
    }
    else if (val == 1) {
        return OptionExerciseType::Exercise;
    }
    else if (val == 2) {
        return OptionExerciseType::Lapse;
    }
    else if (val == 3) {
        return OptionExerciseType::DoNothing;
    }
    else if (val == 100) {
        return OptionExerciseType::Assigned;
    }
    else if (val == 101) {
        return OptionExerciseType::AutoexerciseClearing;
    }
    else if (val == 102) {
        return OptionExerciseType::Expired;
    }
    else if (val == 103) {
        return OptionExerciseType::Netting;
    }
    else if (val == 200) {
        return OptionExerciseType::AutoexerciseTrading;
    }
    return OptionExerciseType::None;
}
