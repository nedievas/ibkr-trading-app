#pragma once

#include <string>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// Portable money / thousands formatting.
//
// The printf grouping flag `%'` is a POSIX/glibc extension: on Linux it only
// groups when the C locale's LC_NUMERIC is set (otherwise it silently prints
// ungrouped), and on MSVC it is unsupported entirely — the CRT prints the
// literal format text ("$':2f") instead of the number. To get consistent
// thousands separators on every platform we group manually here.
namespace core::services {

// Formats `value` with `decimals` fractional digits and comma thousands
// separators on the integer part. Returns e.g. "18,742.00" for (18742.0, 2)
// or "1,234" for (1234.0, 0). Negative values keep the leading '-'.
// Decimals for a price: 2 at $1 and above, and under $1 as many as the price
// has past the cent (a sub-dollar stock's $0.0001 tick, an index option's
// 0.725), up to 4. Orders at $1 and above can't be priced finer than a cent,
// so their extra digits are only sub-penny trade prints and averages
// (136.355, 129.9524), shown to the cent. For a tick under $1 it gives the
// decimals a stepper needs to show each step.
inline int PriceDecimals(double price) {
    if (!std::isfinite(price)) return 2;
    const double a = std::fabs(price);
    if (a >= 1.0) return 2;
    for (int d = 2; d < 4; ++d) {
        const double scaled = a * std::pow(10.0, d);
        if (std::fabs(scaled - std::round(scaled)) < 1e-6 * std::max(1.0, scaled))
            return d;
    }
    return 4;
}

inline std::string FormatThousands(double value, int decimals = 2) {
    if (!std::isfinite(value)) return "0";

    bool negative = value < 0.0;
    double av = negative ? -value : value;

    // Render the raw fixed-point string first, then splice in separators.
    char raw[64];
    std::snprintf(raw, sizeof(raw), "%.*f", decimals, av);

    std::string s(raw);
    std::string intPart, fracPart;
    auto dot = s.find('.');
    if (dot == std::string::npos) {
        intPart = s;
    } else {
        intPart  = s.substr(0, dot);
        fracPart = s.substr(dot);   // includes the '.'
    }

    // Insert commas every three digits from the right of the integer part.
    std::string grouped;
    int count = 0;
    for (auto it = intPart.rbegin(); it != intPart.rend(); ++it) {
        if (count != 0 && count % 3 == 0) grouped.push_back(',');
        grouped.push_back(*it);
        ++count;
    }
    std::string result(grouped.rbegin(), grouped.rend());
    result += fracPart;
    if (negative) result.insert(result.begin(), '-');
    return result;
}

} // namespace core::services
