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
#ifndef TWS_API_CLIENT_DECIMAL_H
#define TWS_API_CLIENT_DECIMAL_H

#include "platformspecific.h"

#include <climits>
#include <cstdint>
#include <string>

// Opaque wrapper around a BID64 decimal floating-point value (Intel DFP library).
// No implicit conversions to/from integer or floating-point types; use DecimalFunctions.
class TWSAPIDLLEXP Decimal {
public:
    constexpr Decimal() : value(ULLONG_MAX) {}

    static constexpr Decimal fromBits(std::uint64_t bits) {
        return Decimal(bits);
    }
    std::uint64_t bits() const {
        return value;
    }
    bool isUnset() const { // check if value is `UNSET_DECIMAL`
        return bits() == ULLONG_MAX;
    }

private:
    explicit constexpr Decimal(std::uint64_t bits) : value(bits) {}
    std::uint64_t value;
};

static const Decimal UNSET_DECIMAL = Decimal::fromBits(ULLONG_MAX);
static const Decimal ZERO_DECIMAL = Decimal::fromBits(0x31c0000000000000ULL);

class TWSAPIDLLEXP DecimalFunctions {
public:
    static Decimal add(Decimal decimal1, Decimal decimal2);
    static Decimal sub(Decimal decimal1, Decimal decimal2);
    static Decimal mul(Decimal decimal1, Decimal decimal2);
    static Decimal div(Decimal decimal1, Decimal decimal2);
    static double decimalToDouble(Decimal decimal);
    static Decimal doubleToDecimal(double d);
    static Decimal stringToDecimal(std::string str);
    static std::string decimalToString(Decimal value);
    static std::string decimalStringToDisplay(Decimal value);
};

#endif
