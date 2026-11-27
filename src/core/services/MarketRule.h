#pragma once
// IB market rules: the price increment of a contract as a list of
// (low edge, increment) steps - a price at or above a step's low edge moves in
// that step's increment. Futures publish one per exchange in their contract
// details (marketRuleIds); /ES is a single step of 0.25 from 0.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace core::services {

struct PriceStep {
    double lowEdge   = 0.0;
    double increment = 0.0;
};
using MarketRule = std::vector<PriceStep>;

// The increment at `price` (its size: a negative spread price uses the same
// steps as a positive one). 0 for an empty rule.
inline double TickForPrice(const MarketRule& rule, double price) {
    const double a = std::fabs(price);
    double tick = 0.0, edge = -1.0;
    for (const PriceStep& s : rule) {
        if (s.increment <= 0.0) continue;
        if (s.lowEdge <= a + 1e-12 && s.lowEdge > edge) { edge = s.lowEdge; tick = s.increment; }
    }
    if (tick > 0.0) return tick;
    // Below every low edge: the lowest step.
    for (const PriceStep& s : rule)
        if (s.increment > 0.0 && (edge < 0.0 || s.lowEdge < edge)) { edge = s.lowEdge; tick = s.increment; }
    return tick;
}

namespace detail {
inline double SnapToTick(double price, double tick, int dir) {
    const double q = price / tick;
    const double r = std::round(q);
    double n;
    if (dir > 0)      n = (std::fabs(q - r) < 1e-9) ? r : std::ceil(q);
    else if (dir < 0) n = (std::fabs(q - r) < 1e-9) ? r : std::floor(q);
    else              n = r;
    const double v = n * tick;
    return std::round(v * 1e9) / 1e9;   // drop the ULP drift of n * 0.015625 etc.
}
}

// `price` on the rule's grid: dir 0 nearest, +1 up, -1 down. A price that
// snaps into a step with a coarser increment is snapped again on that one. An
// empty rule returns the price unchanged.
inline double SnapToRule(const MarketRule& rule, double price, int dir = 0) {
    double tick = TickForPrice(rule, price);
    if (tick <= 0.0) return price;
    double v = detail::SnapToTick(price, tick, dir);
    const double t2 = TickForPrice(rule, v);
    if (t2 > tick) v = detail::SnapToTick(v, t2, dir);
    return v;
}

// Decimals needed to show every step of `tick` (0.25 -> 2, 0.005 -> 3,
// 1/64 -> 6), at least 2, at most 8. 2 for a tick <= 0.
inline int TickDecimals(double tick) {
    if (!(tick > 0.0)) return 2;
    for (int d = 0; d <= 8; ++d) {
        const double scaled = tick * std::pow(10.0, d);
        if (std::fabs(scaled - std::round(scaled)) < 1e-7 * std::max(1.0, scaled))
            return std::max(2, d);
    }
    return 8;
}

// The rule id for `exchange` from contract details' marketRuleIds, which runs
// parallel to validExchanges (both comma-separated). The first id when the
// exchange isn't listed; 0 when there is none.
inline int PickMarketRuleId(const std::string& marketRuleIds,
                            const std::string& validExchanges,
                            const std::string& exchange) {
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) out.push_back(item);
        return out;
    };
    const std::vector<std::string> ids = split(marketRuleIds);
    const std::vector<std::string> ex  = split(validExchanges);
    auto toId = [](const std::string& s) { return std::atoi(s.c_str()); };
    for (size_t i = 0; i < ex.size() && i < ids.size(); ++i)
        if (ex[i] == exchange) return toId(ids[i]);
    for (const std::string& id : ids)
        if (toId(id) > 0) return toId(id);
    return 0;
}

} // namespace core::services
