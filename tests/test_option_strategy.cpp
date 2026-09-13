#include <catch2/catch_test_macros.hpp>

#include "core/services/OptionStrategy.h"

using core::Position;
using namespace core::services;

namespace {

// Minimal option-leg factory. quantity sign encodes long(+)/short(-).
Position Opt(const char* sym, const char* expiry, double strike, const char* right,
             double qty, double costBasis = 0.0, double uPnl = 0.0, long conId = 0) {
    Position p;
    p.symbol     = sym;
    p.assetClass = "OPT";
    p.expiry     = expiry;
    p.strike     = strike;
    p.right      = right;
    p.quantity   = qty;
    p.conId      = conId;
    p.costBasis  = costBasis;
    p.marketValue   = costBasis + uPnl;
    p.unrealizedPnL = uPnl;
    return p;
}

Position Stock(const char* sym, double qty) {
    Position p;
    p.symbol = sym;
    p.assetClass = "STK";
    p.quantity = qty;
    return p;
}

}  // namespace

TEST_CASE("Single option leg", "[strategy]") {
    auto g = ClassifyStrategies({ Opt("TSLA", "20261016", 310, "P", -2) });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Single);
    CHECK(g[0].isOption);
    CHECK(g[0].label == "TSLA Oct16'26 310 Put");
    CHECK(g[0].comboQty == 2);
}

TEST_CASE("Non-option positions pass through as Single groups", "[strategy]") {
    auto g = ClassifyStrategies({ Stock("AAPL", 100), Stock("MSFT", -50) });
    REQUIRE(g.size() == 2);
    CHECK(g[0].kind == StrategyKind::Single);
    CHECK_FALSE(g[0].isOption);
    CHECK(g[0].label == "AAPL");
    CHECK(g[1].label == "MSFT");
}

TEST_CASE("Bear call vertical (short lower / long higher)", "[strategy]") {
    // SPX Sep09 7640/7650 Bear Call: short 7640C, long 7650C, 5 spreads.
    auto g = ClassifyStrategies({
        Opt("SPX", "20260909", 7640, "C", -5),
        Opt("SPX", "20260909", 7650, "C",  5),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].label == "SPX Sep09 7640/7650 Bear Call");
    CHECK(g[0].comboQty == 5);
}

TEST_CASE("Bull call vertical (long lower / short higher)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 200, "C",  1),
        Opt("AAPL", "20261016", 210, "C", -1),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].label == "AAPL Oct16 200/210 Bull Call");
}

TEST_CASE("Bull put vertical (short higher / long lower)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 190, "P",  1),
        Opt("AAPL", "20261016", 200, "P", -1),
    });
    REQUIRE(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].label == "AAPL Oct16 190/200 Bull Put");
}

TEST_CASE("Bear put vertical (long higher / short lower)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 190, "P", -1),
        Opt("AAPL", "20261016", 200, "P",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].label == "AAPL Oct16 190/200 Bear Put");
}

TEST_CASE("Ratio spread (unequal quantities)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 200, "C",  1),
        Opt("AAPL", "20261016", 210, "C", -2),
    });
    REQUIRE(g[0].kind == StrategyKind::Ratio);
    CHECK(g[0].label == "AAPL Oct16 200/210 Bull Call Ratio");
    CHECK(g[0].comboQty == 1);   // gcd(1,2)
}

TEST_CASE("Calendar (same right+strike, different expiry)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("GOOGL", "20260918", 400, "C", -1),
        Opt("GOOGL", "20261120", 400, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::Calendar);
    CHECK(g[0].label == "GOOGL 400C Calendar (Sep18/Nov20)");
}

TEST_CASE("Diagonal (same right, different expiry AND strike)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("GOOGL", "20260918", 400, "C", -1),
        Opt("GOOGL", "20261120", 420, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::Diagonal);
    CHECK(g[0].label == "GOOGL 400/420C Diagonal (Sep18/Nov20)");
}

TEST_CASE("Straddle (same expiry+strike, different right)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 500, "C", -1),
        Opt("SPY", "20261016", 500, "P", -1),
    });
    REQUIRE(g[0].kind == StrategyKind::Straddle);
    CHECK(g[0].label == "SPY Oct16 500 Straddle");
}

TEST_CASE("Strangle (same expiry, different right AND strike)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 490, "P", -1),
        Opt("SPY", "20261016", 510, "C", -1),
    });
    REQUIRE(g[0].kind == StrategyKind::Strangle);
    CHECK(g[0].label == "SPY Oct16 490/510 Strangle");
}

TEST_CASE("Iron condor (2C+2P, distinct body strikes)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20261016", 4800, "P",  1),
        Opt("SPX", "20261016", 4900, "P", -1),
        Opt("SPX", "20261016", 5100, "C", -1),
        Opt("SPX", "20261016", 5200, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::IronCondor);
    CHECK(g[0].label == "SPX Oct16 Iron Condor");
}

TEST_CASE("Iron butterfly (shared body strike)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20261016", 4900, "P",  1),
        Opt("SPX", "20261016", 5000, "P", -1),
        Opt("SPX", "20261016", 5000, "C", -1),
        Opt("SPX", "20261016", 5100, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::IronButterfly);
}

TEST_CASE("Call butterfly (1:-2:1 evenly spaced)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 190, "C",  1),
        Opt("AAPL", "20261016", 200, "C", -2),
        Opt("AAPL", "20261016", 210, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::Butterfly);
    CHECK(g[0].label == "AAPL Oct16 190/200/210 Call Butterfly");
}

TEST_CASE("Three short verticals decompose into three Bull Puts", "[strategy]") {
    // The reported bug: three separate credit put spreads on one underlying were
    // shown as a single "6 legs" blob. They must split into three Bull Put rows.
    auto g = ClassifyStrategies({
        Opt("SPX", "20260909", 7000, "P",  5), Opt("SPX", "20260909", 7050, "P", -5),
        Opt("SPX", "20260909", 7100, "P",  5), Opt("SPX", "20260909", 7150, "P", -5),
        Opt("SPX", "20260909", 7200, "P",  5), Opt("SPX", "20260909", 7250, "P", -5),
    });
    REQUIRE(g.size() == 3);
    for (const auto& s : g) { CHECK(s.kind == StrategyKind::Vertical); CHECK(s.comboQty == 5); }
    CHECK(g[0].label == "SPX Sep09 7000/7050 Bull Put");
    CHECK(g[1].label == "SPX Sep09 7100/7150 Bull Put");
    CHECK(g[2].label == "SPX Sep09 7200/7250 Bull Put");
}

TEST_CASE("Mixed bucket decomposes into a vertical + a single", "[strategy]") {
    // A call vertical (same expiry) plus an unrelated put on a later expiry:
    // one Bear Call row + one lone-put row, not a "3 legs" Custom blob.
    auto g = ClassifyStrategies({
        Opt("SPX", "20260909", 7640, "C", -5),
        Opt("SPX", "20260909", 7650, "C",  5),
        Opt("SPX", "20261016", 7000, "P", -3),
    });
    REQUIRE(g.size() == 2);
    CHECK(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].label == "SPX Sep09 7640/7650 Bear Call");
    CHECK(g[1].kind == StrategyKind::Single);
    CHECK(g[1].label == "SPX Oct16'26 7000 Put");
}

TEST_CASE("Unpairable bucket stays Custom", "[strategy]") {
    // 2 longs + 1 short in one (expiry,right) partition -> not cleanly pairable.
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 200, "C",  1),
        Opt("AAPL", "20261016", 210, "C",  1),
        Opt("AAPL", "20261016", 220, "C", -1),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Custom);
    CHECK(g[0].label == "AAPL 3 legs");
}

TEST_CASE("Rollups sum across legs", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20260909", 7640, "C", -5, /*cost*/ -1000, /*uPnl*/ 137),
        Opt("SPX", "20260909", 7650, "C",  5, /*cost*/   500, /*uPnl*/ -50),
    });
    REQUIRE(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].costBasis == -500.0);
    CHECK(g[0].unrealizedPnL == 87.0);
    CHECK(g[0].marketValue == (-1000 + 137) + (500 - 50));
}

TEST_CASE("Stock and its options are separate groups", "[strategy]") {
    auto g = ClassifyStrategies({
        Stock("AAPL", 100),
        Opt("AAPL", "20261016", 200, "C", 1),
        Opt("AAPL", "20261016", 210, "C", -1),
    });
    REQUIRE(g.size() == 2);
    CHECK_FALSE(g[0].isOption);              // stock line first
    CHECK(g[0].label == "AAPL");
    CHECK(g[1].kind == StrategyKind::Vertical);
}

TEST_CASE("Flat (zero-qty) legs are ignored", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 200, "C", 0),
        Opt("AAPL", "20261016", 210, "C", -1),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Single);   // only the live leg remains
}

TEST_CASE("Source tag: inferred strategies vs actual singles", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("AAPL", "20261016", 200, "C",  1),   // -> Inferred vertical
        Opt("AAPL", "20261016", 210, "C", -1),
        Opt("TSLA", "20261016", 300, "P", -1),   // -> Actual single
        Stock("MSFT", 100),                       // -> Actual single
    });
    const StrategyGroup* aapl = nullptr; const StrategyGroup* tsla = nullptr; const StrategyGroup* msft = nullptr;
    for (auto& s : g) { if (s.underlying=="AAPL") aapl=&s; if (s.underlying=="TSLA") tsla=&s; if (s.underlying=="MSFT") msft=&s; }
    REQUIRE(aapl); REQUIRE(tsla); REQUIRE(msft);
    CHECK(aapl->source == GroupSource::Inferred);
    CHECK(tsla->source == GroupSource::Actual);
    CHECK(msft->source == GroupSource::Actual);
}

TEST_CASE("Ungroup override splits an inferred vertical into flat legs", "[strategy]") {
    std::vector<Position> pos = {
        Opt("SPX", "20260909", 7640, "C", -5, 0, 0, /*conId*/ 111),
        Opt("SPX", "20260909", 7650, "C",  5, 0, 0, /*conId*/ 222),
    };
    // Without override -> one Inferred vertical.
    CHECK(ClassifyStrategies(pos).size() == 1);
    // With both conIds pinned flat -> two Manual singles, no vertical.
    auto g = ClassifyStrategies(pos, {111, 222});
    REQUIRE(g.size() == 2);
    for (auto& s : g) {
        CHECK(s.kind == StrategyKind::Single);
        CHECK(s.source == GroupSource::Manual);
    }
}

TEST_CASE("Ungroup one leg leaves the rest to re-decompose", "[strategy]") {
    // Two verticals (4 puts). Ungroup one leg of the first -> that leg + its
    // partner both fall out of pairing; the second vertical still forms.
    std::vector<Position> pos = {
        Opt("SPX", "20260909", 7000, "P",  5, 0, 0, 11),
        Opt("SPX", "20260909", 7050, "P", -5, 0, 0, 12),
        Opt("SPX", "20260909", 7100, "P",  5, 0, 0, 13),
        Opt("SPX", "20260909", 7150, "P", -5, 0, 0, 14),
    };
    CHECK(ClassifyStrategies(pos).size() == 2);            // two Bull Puts
    auto g = ClassifyStrategies(pos, {11, 12});            // ungroup the 7000/7050 pair
    int verticals = 0, singles = 0;
    for (auto& s : g) { if (s.kind==StrategyKind::Vertical) ++verticals; if (s.kind==StrategyKind::Single) ++singles; }
    CHECK(verticals == 1);   // 7100/7150 still pairs
    CHECK(singles == 2);     // 7000 and 7050 now flat
}

TEST_CASE("Empty input", "[strategy]") {
    CHECK(ClassifyStrategies({}).empty());
}
