#pragma once

// PriceAlerts — price alerts set on a chart, checked against last-price
// ticks. Pure logic: no IB API / ImGui. An alert is kept per symbol and
// fires once, when the last price reaches its level from the side it was set
// on (an alert above the market fires at or above it, one below at or below).

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace core::services {

struct PriceAlert {
    int         id    = 0;
    std::string symbol;
    double      price = 0.0;
    bool        above = true;   // true: fires when last >= price; false: last <= price
};

// The side an alert sits on, from the market price when it is set or moved.
// With no market price yet (0) the alert counts as above.
[[nodiscard]] inline bool AlertIsAbove(double alertPrice, double lastPrice) {
    return lastPrice <= 0.0 || alertPrice >= lastPrice;
}

[[nodiscard]] inline bool AlertTriggered(const PriceAlert& a, double lastPrice) {
    if (lastPrice <= 0.0 || a.price <= 0.0) return false;
    return a.above ? lastPrice >= a.price : lastPrice <= a.price;
}

// One alert per line: SYMBOL|PRICE|A (above) or B (below). The id is not
// saved; the loader numbers alerts from 1.
[[nodiscard]] inline std::string FormatPriceAlerts(const std::vector<PriceAlert>& alerts) {
    std::string out;
    char buf[64];
    for (const auto& a : alerts) {
        if (a.symbol.empty() || a.price <= 0.0) continue;
        std::snprintf(buf, sizeof(buf), "|%.10g|%c\n", a.price, a.above ? 'A' : 'B');
        out += a.symbol;
        out += buf;
    }
    return out;
}

[[nodiscard]] inline std::vector<PriceAlert> ParsePriceAlerts(const std::string& text) {
    std::vector<PriceAlert> out;
    std::istringstream in(text);
    std::string line;
    int nextId = 1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t p1 = line.find('|');
        if (p1 == std::string::npos || p1 == 0) continue;
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos || p2 + 1 >= line.size()) continue;
        std::string priceText = line.substr(p1 + 1, p2 - p1 - 1);
        char* end = nullptr;
        double price = std::strtod(priceText.c_str(), &end);
        if (end == priceText.c_str() || !(price > 0.0) || price > 1e9) continue;
        char side = line[p2 + 1];
        if (side != 'A' && side != 'B') continue;
        PriceAlert a;
        a.id     = nextId++;
        a.symbol = line.substr(0, p1);
        a.price  = price;
        a.above  = (side == 'A');
        out.push_back(a);
    }
    return out;
}

}   // namespace core::services
