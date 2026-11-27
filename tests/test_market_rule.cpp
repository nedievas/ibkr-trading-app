#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/services/MarketRule.h"
#include "core/services/NumberFormat.h"

using namespace core::services;

TEST_CASE("TickForPrice: the step at or below the price", "[market-rule]") {
    const MarketRule es = {{0.0, 0.25}};
    CHECK(TickForPrice(es, 6012.30) == 0.25);
    CHECK(TickForPrice(es, -3.0) == 0.25);   // a spread price
    // An option-style rule: 0.05 below 5, 0.25 from 5.
    const MarketRule fop = {{0.0, 0.05}, {5.0, 0.25}};
    CHECK(TickForPrice(fop, 4.95) == 0.05);
    CHECK(TickForPrice(fop, 5.0) == 0.25);
    CHECK(TickForPrice(fop, 12.0) == 0.25);
    // Steps given out of order.
    CHECK(TickForPrice({{5.0, 0.25}, {0.0, 0.05}}, 3.0) == 0.05);
    // A price below every edge takes the lowest step.
    CHECK(TickForPrice({{1.0, 0.01}}, 0.5) == 0.01);
    CHECK(TickForPrice({}, 10.0) == 0.0);
}

TEST_CASE("SnapToRule: nearest, up and down on the grid", "[market-rule]") {
    const MarketRule es = {{0.0, 0.25}};
    CHECK(SnapToRule(es, 6012.30) == Catch::Approx(6012.25));
    CHECK(SnapToRule(es, 6012.40) == Catch::Approx(6012.50));
    CHECK(SnapToRule(es, 6012.30, +1) == Catch::Approx(6012.50));
    CHECK(SnapToRule(es, 6012.30, -1) == Catch::Approx(6012.25));
    CHECK(SnapToRule(es, 6012.25, +1) == Catch::Approx(6012.25));   // already on it
    CHECK(SnapToRule(es, -2.6) == Catch::Approx(-2.5));
    // ZN in 1/64: exact, no ULP drift.
    const MarketRule zn = {{0.0, 1.0 / 64.0}};
    CHECK(SnapToRule(zn, 110.02) == 110.015625);
    // SIL in 0.005.
    CHECK(SnapToRule({{0.0, 0.005}}, 43.9862) == Catch::Approx(43.985));
    // Crossing into a coarser step snaps again on it.
    const MarketRule fop = {{0.0, 0.05}, {5.0, 0.25}};
    CHECK(SnapToRule(fop, 4.99) == Catch::Approx(5.0));
    CHECK(SnapToRule(fop, 5.10) == Catch::Approx(5.0));
    CHECK(SnapToRule(fop, 4.93) == Catch::Approx(4.95));
    // An empty rule leaves the price.
    CHECK(SnapToRule({}, 1.2345) == 1.2345);
}

TEST_CASE("TickDecimals and PriceDecimals with a tick", "[market-rule]") {
    CHECK(TickDecimals(0.25) == 2);
    CHECK(TickDecimals(0.005) == 3);
    CHECK(TickDecimals(1.0 / 64.0) == 6);
    CHECK(TickDecimals(1.0) == 2);
    CHECK(TickDecimals(0.0) == 2);
    CHECK(PriceDecimals(43.985, 0.005) == 3);
    CHECK(PriceDecimals(6012.25, 0.25) == 2);
    CHECK(PriceDecimals(110.015625, 1.0 / 64.0) == 6);
    CHECK(PriceDecimals(136.355, 0.0) == 2);    // a stock keeps its rule
    CHECK(PriceDecimals(0.2893, 0.0) == 4);
}

TEST_CASE("PickMarketRuleId: the id for the exchange", "[market-rule]") {
    CHECK(PickMarketRuleId("67,32", "CME,QBALGO", "CME") == 67);
    CHECK(PickMarketRuleId("67,32", "CME,QBALGO", "QBALGO") == 32);
    CHECK(PickMarketRuleId("67,32", "CME,QBALGO", "NYMEX") == 67);   // not listed: the first
    CHECK(PickMarketRuleId("88", "", "COMEX") == 88);
    CHECK(PickMarketRuleId("", "", "CME") == 0);
}
