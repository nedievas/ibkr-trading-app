#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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
    CHECK(g[0].label == "SPX Oct16 4800/4900/5100/5200 Iron Condor");
}

TEST_CASE("Iron butterfly (shared body strike)", "[strategy]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20261016", 4900, "P",  1),
        Opt("SPX", "20261016", 5000, "P", -1),
        Opt("SPX", "20261016", 5000, "C", -1),
        Opt("SPX", "20261016", 5100, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::IronButterfly);
    CHECK(g[0].label == "SPX Oct16 4900/5000/5100 Iron Butterfly");
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

// ── Authoritative combo links ─────────────────────────────────────────────────

TEST_CASE("Link groups a spread with certainty (Actual, not Inferred)", "[strategy][link]") {
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 200, "C",  1, 0, 0, 101),
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 102),
    };
    // Heuristic alone -> Vertical but Inferred.
    CHECK(ClassifyStrategies(pos)[0].source == GroupSource::Inferred);
    // With an authoritative link -> same shape, but Actual.
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{101, 102}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].source == GroupSource::Actual);
    CHECK(g[0].label == "AAPL Oct16 200/210 Bull Call");
    CHECK(g[0].comboQty == 1);
}

TEST_CASE("Links override the heuristic pairing", "[strategy][link]") {
    // Four calls. Rank-pairing would form 100/105 and 110/115, but the user
    // actually traded 100/115 and 105/110 as two combos -> links pin those.
    std::vector<Position> pos = {
        Opt("XYZ", "20261016", 100, "C",  1, 0, 0, 1),
        Opt("XYZ", "20261016", 105, "C",  1, 0, 0, 2),
        Opt("XYZ", "20261016", 110, "C", -1, 0, 0, 3),
        Opt("XYZ", "20261016", 115, "C", -1, 0, 0, 4),
    };
    auto g = ClassifyStrategies(pos, {}, {
        ComboLink{{1, 4}, GroupSource::Actual},   // 100/115
        ComboLink{{2, 3}, GroupSource::Actual},   // 105/110
    });
    REQUIRE(g.size() == 2);
    for (auto& s : g) {
        CHECK(s.kind == StrategyKind::Vertical);
        CHECK(s.source == GroupSource::Actual);
    }
    // 100/115 group and 105/110 group, by label.
    bool has_100_115 = false, has_105_110 = false;
    for (auto& s : g) {
        if (s.label.find("100/115") != std::string::npos) has_100_115 = true;
        if (s.label.find("105/110") != std::string::npos) has_105_110 = true;
    }
    CHECK(has_100_115);
    CHECK(has_105_110);
}

TEST_CASE("A link with a missing leg is ignored (falls back to heuristic)", "[strategy][link]") {
    // Only one leg of the link is still held -> link fails; the held leg
    // classifies heuristically (a lone Single).
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 200, "C", 1, 0, 0, 101),
    };
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{101, 102}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Single);
    CHECK(g[0].source == GroupSource::Actual);   // a lone leg is unambiguous
    CHECK(g[0].legIdx.size() == 1);
}

TEST_CASE("Duplicate identical links group once (netted combo)", "[strategy][link]") {
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 200, "C",  2, 0, 0, 101),
        Opt("AAPL", "20261016", 210, "C", -2, 0, 0, 102),
    };
    auto g = ClassifyStrategies(pos, {}, {
        ComboLink{{101, 102}, GroupSource::Actual},
        ComboLink{{101, 102}, GroupSource::Actual},   // same set again
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Vertical);
    CHECK(g[0].comboQty == 2);
}

TEST_CASE("Ungroup overrides a link", "[strategy][link]") {
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 200, "C",  1, 0, 0, 101),
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 102),
    };
    // User pinned leg 101 flat -> the link can't claim it, both legs go flat.
    auto g = ClassifyStrategies(pos, {101}, { ComboLink{{101, 102}, GroupSource::Actual} });
    REQUIRE(g.size() == 2);
    for (auto& s : g) CHECK(s.kind == StrategyKind::Single);
    // 101 is the user-pinned Manual single; 102 is a heuristic single.
    bool sawManual = false;
    for (auto& s : g) if (s.source == GroupSource::Manual) sawManual = true;
    CHECK(sawManual);
}

TEST_CASE("Stock-leg link labels a covered call", "[strategy][link]") {
    Position stk = Stock("AAPL", 100); stk.conId = 500;
    std::vector<Position> pos = {
        stk,
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 501),
    };
    // Without a link: a stock Single + an option Single (two rows).
    CHECK(ClassifyStrategies(pos).size() == 2);
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{500, 501}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].isOption);                       // renders through the option-group path
    CHECK(g[0].source == GroupSource::Actual);
    CHECK(g[0].label == "AAPL Covered Call");
    CHECK(g[0].legIdx.size() == 2);
}

TEST_CASE("Stock-leg link labels a collar", "[strategy][link]") {
    Position stk = Stock("AAPL", 100); stk.conId = 500;
    std::vector<Position> pos = {
        stk,
        Opt("AAPL", "20261016", 190, "P",  1, 0, 0, 502),   // long put
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 501),   // short call
    };
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{500, 501, 502}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].label == "AAPL Oct16 190/210 Collar");   // put strike / call strike
    CHECK(g[0].legIdx.size() == 3);

    // Put and call on different expiries: each leg carries its own.
    pos[2] = Opt("AAPL", "20261120", 210, "C", -1, 0, 0, 501);
    g = ClassifyStrategies(pos, {}, { ComboLink{{500, 501, 502}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].label == "AAPL Oct16 190P / Nov20 210C Collar");
}

TEST_CASE("Stock-leg link labels a conversion and a reversal (same strike)", "[strategy][link]") {
    // The live QBTS case: long stock + long put + short call at one strike is a
    // conversion, not a collar.
    Position stk = Stock("QBTS", 100); stk.conId = 600;
    std::vector<Position> conv = {
        stk,
        Opt("QBTS", "20261016", 17, "P",  1, 0, 0, 602),
        Opt("QBTS", "20261016", 17, "C", -1, 0, 0, 601),
    };
    auto g = ClassifyStrategies(conv, {}, { ComboLink{{600, 601, 602}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].label == "QBTS Oct16 17 Conversion");

    Position shrt = Stock("QBTS", -100); shrt.conId = 600;
    std::vector<Position> rev = {
        shrt,
        Opt("QBTS", "20261016", 17, "P", -1, 0, 0, 602),
        Opt("QBTS", "20261016", 17, "C",  1, 0, 0, 601),
    };
    g = ClassifyStrategies(rev, {}, { ComboLink{{600, 601, 602}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].label == "QBTS Oct16 17 Reversal");
}

TEST_CASE("Link partition is never decomposed", "[strategy][link]") {
    // Four calls that would decompose into two verticals; a single 4-leg link
    // keeps them as ONE Custom group (the user traded them as one combo).
    std::vector<Position> pos = {
        Opt("XYZ", "20261016", 100, "C",  1, 0, 0, 1),
        Opt("XYZ", "20261016", 105, "C", -1, 0, 0, 2),
        Opt("XYZ", "20261016", 110, "C", -1, 0, 0, 3),
        Opt("XYZ", "20261016", 115, "C",  1, 0, 0, 4),
    };
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{1, 2, 3, 4}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);                     // one group, not two verticals
    CHECK(g[0].source == GroupSource::Actual);
    CHECK(g[0].legIdx.size() == 4);
    // 2 calls + 2 calls, ascending +,-,-,+ -> Condor via tryNamedMulti.
    CHECK(g[0].kind == StrategyKind::Condor);
    CHECK(g[0].label == "XYZ Oct16 100/105/110/115 Call Condor");
}

// ── Sign-aware labelling (a call+put of opposite sign is directional, not a
//    straddle/strangle; calendars need one long + one short) ──────────────────

TEST_CASE("Long call + short put at one strike is a Synthetic Long, not a Straddle", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 500, "C",  1),
        Opt("SPY", "20261016", 500, "P", -1),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::Synthetic);
    CHECK(g[0].label == "SPY Oct16 500 Synthetic Long");
}

TEST_CASE("Short call + long put at one strike is a Synthetic Short", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 500, "C", -1),
        Opt("SPY", "20261016", 500, "P",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::Synthetic);
    CHECK(g[0].label == "SPY Oct16 500 Synthetic Short");
}

TEST_CASE("Long call + short put at different strikes is a Bullish Risk Reversal", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 490, "P", -1),
        Opt("SPY", "20261016", 510, "C",  1),
    });
    REQUIRE(g[0].kind == StrategyKind::RiskReversal);
    CHECK(g[0].label == "SPY Oct16 490/510 Bullish Risk Reversal");
}

TEST_CASE("Long put + short call at different strikes is a Bearish Risk Reversal", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPY", "20261016", 490, "P",  1),
        Opt("SPY", "20261016", 510, "C", -1),
    });
    REQUIRE(g[0].kind == StrategyKind::RiskReversal);
    CHECK(g[0].label == "SPY Oct16 490/510 Bearish Risk Reversal");
}

TEST_CASE("Long straddle and long strangle keep their names", "[strategy][signs]") {
    auto st = ClassifyStrategies({
        Opt("SPY", "20261016", 500, "C", 1),
        Opt("SPY", "20261016", 500, "P", 1),
    });
    CHECK(st[0].kind == StrategyKind::Straddle);
    auto sg = ClassifyStrategies({
        Opt("SPY", "20261016", 490, "P", 1),
        Opt("SPY", "20261016", 510, "C", 1),
    });
    CHECK(sg[0].kind == StrategyKind::Strangle);
}

TEST_CASE("Same-sign legs across expiries are not a Calendar or Diagonal", "[strategy][signs]") {
    auto cal = ClassifyStrategies({
        Opt("GOOGL", "20260918", 400, "C", 1),
        Opt("GOOGL", "20261120", 400, "C", 1),
    });
    CHECK(cal[0].kind == StrategyKind::Custom);
    auto diag = ClassifyStrategies({
        Opt("GOOGL", "20260918", 400, "C", 1),
        Opt("GOOGL", "20261120", 420, "C", 1),
    });
    CHECK(diag[0].kind == StrategyKind::Custom);
}

// ── Iron condor / butterfly validation ───────────────────────────────────────

TEST_CASE("A box spread is not an Iron Condor (decomposes into two verticals)", "[strategy][signs]") {
    // long 100C / short 110C + long 110P / short 100P = a box, not an iron condor.
    auto g = ClassifyStrategies({
        Opt("XYZ", "20261016", 100, "C",  1),
        Opt("XYZ", "20261016", 110, "C", -1),
        Opt("XYZ", "20261016", 110, "P",  1),
        Opt("XYZ", "20261016", 100, "P", -1),
    });
    REQUIRE(g.size() == 2);
    for (auto& s : g) {
        CHECK(s.kind == StrategyKind::Vertical);
        CHECK(s.kind != StrategyKind::IronCondor);
    }
}

TEST_CASE("Two long straddles are not an Iron Condor", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("XYZ", "20261016", 100, "C", 1),
        Opt("XYZ", "20261016", 100, "P", 1),
        Opt("XYZ", "20261016", 110, "C", 1),
        Opt("XYZ", "20261016", 110, "P", 1),
    });
    for (auto& s : g) {
        CHECK(s.kind != StrategyKind::IronCondor);
        CHECK(s.kind != StrategyKind::IronButterfly);
    }
}

TEST_CASE("A long-body iron condor is labelled Reverse", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20261016", 4800, "P", -1),
        Opt("SPX", "20261016", 4900, "P",  1),
        Opt("SPX", "20261016", 5100, "C",  1),
        Opt("SPX", "20261016", 5200, "C", -1),
    });
    REQUIRE(g.size() == 1);
    CHECK(g[0].kind == StrategyKind::IronCondor);
    CHECK(g[0].label == "SPX Oct16 4800/4900/5100/5200 Reverse Iron Condor");
}

TEST_CASE("An unequal-size 2C+2P is not an Iron Condor", "[strategy][signs]") {
    auto g = ClassifyStrategies({
        Opt("SPX", "20261016", 4800, "P",  2),
        Opt("SPX", "20261016", 4900, "P", -2),
        Opt("SPX", "20261016", 5100, "C", -1),
        Opt("SPX", "20261016", 5200, "C",  1),
    });
    for (auto& s : g) CHECK(s.kind != StrategyKind::IronCondor);
}

TEST_CASE("A box placed as one combo is Custom, not a certain Iron Condor", "[strategy][signs][link]") {
    std::vector<Position> pos = {
        Opt("XYZ", "20261016", 100, "C",  1, 0, 0, 1),
        Opt("XYZ", "20261016", 110, "C", -1, 0, 0, 2),
        Opt("XYZ", "20261016", 110, "P",  1, 0, 0, 3),
        Opt("XYZ", "20261016", 100, "P", -1, 0, 0, 4),
    };
    auto g = ClassifyStrategies(pos, {}, { ComboLink{{1, 2, 3, 4}, GroupSource::Actual} });
    REQUIRE(g.size() == 1);
    CHECK(g[0].source == GroupSource::Actual);
    CHECK(g[0].kind == StrategyKind::Custom);
    CHECK(g[0].label == "XYZ 4 legs");
}

// ── Group identity (UI row key) ──────────────────────────────────────────────

TEST_CASE("Two same-label Iron Condors get distinct group keys", "[strategy][key]") {
    // Two SPX 0DTE iron condors on the same expiry, placed as two combos. Labels
    // carry strikes now, but "N legs" / "Combo (N legs)" can still repeat, so
    // identity must come from the legs, never the label.
    std::vector<Position> pos = {
        Opt("SPX", "20261016", 4800, "P",  1, 0, 0, 11),
        Opt("SPX", "20261016", 4900, "P", -1, 0, 0, 12),
        Opt("SPX", "20261016", 5100, "C", -1, 0, 0, 13),
        Opt("SPX", "20261016", 5200, "C",  1, 0, 0, 14),
        Opt("SPX", "20261016", 4700, "P",  1, 0, 0, 21),
        Opt("SPX", "20261016", 4750, "P", -1, 0, 0, 22),
        Opt("SPX", "20261016", 5250, "C", -1, 0, 0, 23),
        Opt("SPX", "20261016", 5300, "C",  1, 0, 0, 24),
    };
    auto g = ClassifyStrategies(pos, {}, {
        ComboLink{{11, 12, 13, 14}, GroupSource::Actual},
        ComboLink{{21, 22, 23, 24}, GroupSource::Actual},
    });
    REQUIRE(g.size() == 2);
    CHECK(g[0].label != g[1].label);   // strikes tell them apart
    CHECK(StrategyGroupKey(g[0], pos) != StrategyGroupKey(g[1], pos));
    CHECK(StrategyGroupKey(g[0], pos) == "11_12_13_14");
}

TEST_CASE("Group key is order-independent and falls back to the index", "[strategy][key]") {
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 102),
        Opt("AAPL", "20261016", 200, "C",  1, 0, 0, 101),
        Opt("AAPL", "20261016", 220, "C",  1),              // conId 0
    };
    StrategyGroup a; a.legIdx = { 0, 1 };
    StrategyGroup b; b.legIdx = { 1, 0 };
    CHECK(StrategyGroupKey(a, pos) == StrategyGroupKey(b, pos));
    StrategyGroup c; c.legIdx = { 2 };
    CHECK(StrategyGroupKey(c, pos) == "i2");
}

// ── conId-set persistence (PORT_UNGROUP / PORT_LINK) ─────────────────────────

TEST_CASE("Unpruned save keeps every set even with no positions loaded", "[strategy][persist]") {
    // Regression: the first settings flush after connect runs before IB has
    // delivered positions. Pruning against that empty list wiped every set.
    const std::vector<std::vector<long>> sets = { {101, 102}, {11, 12, 13, 14} };
    CHECK(FormatConIdSets(sets, {}, /*prune=*/false) == "101-102|11-12-13-14");
    CHECK(FormatConIdSets(sets, {}, /*prune=*/true).empty());   // the old hazard
}

TEST_CASE("Pruned save drops closed legs and sets left with <2 legs", "[strategy][persist]") {
    std::vector<Position> pos = {
        Opt("AAPL", "20261016", 200, "C",  1, 0, 0, 101),
        Opt("AAPL", "20261016", 210, "C", -1, 0, 0, 102),
        Opt("SPX",  "20261016", 4800, "P", 1, 0, 0, 11),
        Opt("SPX",  "20261016", 4900, "P", 0, 0, 0, 12),   // flat
    };
    const std::vector<std::vector<long>> sets = { {101, 102, 103}, {11, 12} };
    // 103 is gone and dropped; {11,12} keeps only 11 -> omitted.
    CHECK(FormatConIdSets(sets, pos, /*prune=*/true) == "101-102");
}

TEST_CASE("conId sets round-trip through Format/Parse", "[strategy][persist]") {
    const std::vector<std::vector<long>> sets = { {101, 102}, {11, 12, 13} };
    CHECK(ParseConIdSets(FormatConIdSets(sets, {}, false)) == sets);
    CHECK(ParseConIdSets("5|7-8").size() == 1);   // a 1-conId set is dropped
    CHECK(ParseConIdSets("").empty());
}

TEST_CASE("ComboStrategyLabel names a calendar order, not a vertical", "[strategy][combo-label]") {
    std::vector<ComboLegInfo> legs = {
        {201, false, 1, false, "20261016", 600, "C"},
        {202, true,  1, false, "20261120", 600, "C"},
    };
    const std::string lbl = ComboStrategyLabel("SPY", legs);
    CHECK(lbl.find("Calendar") != std::string::npos);
    CHECK(lbl.find("SPY") == 0);
}

TEST_CASE("ComboStrategyLabel names a vertical by direction", "[strategy][combo-label]") {
    std::vector<ComboLegInfo> legs = {
        {301, true,  1, false, "20261016", 600, "C"},
        {302, false, 1, false, "20261016", 605, "C"},
    };
    CHECK(ComboStrategyLabel("SPY", legs).find("Bull Call") != std::string::npos);
    for (auto& L : legs) L.buy = !L.buy;   // SELL the same BAG
    CHECK(ComboStrategyLabel("SPY", legs).find("Bear Call") != std::string::npos);
}

TEST_CASE("ComboStrategyLabel is empty when a leg is unresolved", "[strategy][combo-label]") {
    CHECK(ComboStrategyLabel("SPY", {}).empty());
    std::vector<ComboLegInfo> legs = {
        {301, true,  1, false, "20261016", 600, "C"},
        {302, false, 1, false, "",         0,   ""},   // not resolved yet
    };
    CHECK(ComboStrategyLabel("SPY", legs).empty());
}

TEST_CASE("A calendar mixed with a vertical on one underlying is still named", "[strategy][calendar]") {
    std::vector<Position> pos = {
        Opt("SPY", "20261016", 760, "P",  1, 0, 0, 1),   // bull put vertical
        Opt("SPY", "20261016", 750, "P", -1, 0, 0, 2),
        Opt("SPY", "20261016", 767, "C", -1, 0, 0, 3),   // call calendar
        Opt("SPY", "20261120", 767, "C",  1, 0, 0, 4),
    };
    auto groups = ClassifyStrategies(pos);
    bool calendar = false, vertical = false;
    for (const auto& g : groups) {
        if (g.kind == StrategyKind::Calendar && g.legIdx.size() == 2) calendar = true;
        if (g.legIdx.size() == 2 && g.kind != StrategyKind::Calendar) vertical = true;
    }
    CHECK(calendar);
    CHECK(vertical);
}

TEST_CASE("Loose legs that don't form a calendar stay singles", "[strategy][calendar]") {
    std::vector<Position> pos = {
        Opt("SPY", "20261016", 760, "P",  1, 0, 0, 1),
        Opt("SPY", "20261016", 750, "P", -1, 0, 0, 2),
        Opt("SPY", "20261016", 767, "C", -1, 0, 0, 3),
        Opt("SPY", "20261120", 770, "C",  2, 0, 0, 4),   // other strike + size
    };
    int singles = 0;
    for (const auto& g : ClassifyStrategies(pos))
        if (g.legIdx.size() == 1) ++singles;
    CHECK(singles == 2);
}

TEST_CASE("Pruned save keeps a link whose combo order is still working", "[strategy][persist]") {
    const std::vector<std::vector<long>> sets = { {201, 202}, {301, 302} };
    // Nothing held yet; 201/202 belong to a resting combo order.
    const std::unordered_set<long> working = {201, 202};
    CHECK(FormatConIdSets(sets, {}, /*prune=*/true, working) == "201-202");
}

namespace {
Position Held(const char* expiry, double strike, const char* right, double qty,
              double avgCostPerContract, double mark, long conId) {
    Position p = Opt("QQQ", expiry, strike, right, qty, qty * avgCostPerContract, 0, conId);
    p.avgCost     = avgCostPerContract;
    p.marketPrice = mark;
    p.multiplier  = "100";
    return p;
}
}  // namespace

TEST_CASE("Position analysis: credit put spread nets the real entry credit", "[strategy][analysis]") {
    // Bull put: short 743P for 4.50, long 738P for 2.41 -> 2.09 credit.
    std::vector<Position> held = {
        Held("20261130", 743, "P", -1, 450.0, 3.80, 1),
        Held("20261130", 738, "P",  1, 241.0, 2.10, 2),
    };
    const auto a = BuildPositionAnalysis(held, 760.0, 2026, 10, 1);
    REQUIRE(a.valid);
    CHECK(a.netPrice == Catch::Approx(-2.09));
    CHECK(a.qty == 1);
    CHECK_FALSE(a.multiExpiry);
    REQUIRE(a.legs.size() == 2);
    CHECK(a.legs[0].ratio == -1);
    CHECK(a.legs[1].ratio == 1);
    CHECK(a.legs[0].dte == 61);
    // Expiry payoff: keep the credit above 743, lose the width less credit below 738.
    CHECK(PayoffAtExpiry(a.legs, a.netPrice, a.multiplier, 800.0) == Catch::Approx(209.0));
    CHECK(PayoffAtExpiry(a.legs, a.netPrice, a.multiplier, 700.0) == Catch::Approx(-291.0));
    // Spot known: IV backed out of each mark, so greeks are populated.
    CHECK(a.legs[0].iv > 0.0);
    CHECK(a.legs[1].iv > 0.0);
    CHECK(a.legs[0].delta < 0.0);   // put
}

TEST_CASE("Position analysis: calendar is multi-expiry, combo qty is the gcd", "[strategy][analysis]") {
    std::vector<Position> held = {
        Held("20261016", 767, "C", -2, 600.0, 5.0, 1),
        Held("20261120", 767, "C",  2, 1100.0, 11.0, 2),
    };
    const auto a = BuildPositionAnalysis(held, 0.0, 2026, 10, 1);   // no spot yet
    REQUIRE(a.valid);
    CHECK(a.multiExpiry);
    CHECK(a.qty == 2);
    CHECK(a.netPrice == Catch::Approx((-1200.0 + 2200.0) / 100.0));
    CHECK(a.legs[0].iv == 0.0);     // nothing to back IV out of without a spot
}

TEST_CASE("Position analysis needs an option leg and a non-flat position", "[strategy][analysis]") {
    Position stk; stk.symbol = "QQQ"; stk.assetClass = "STK"; stk.quantity = 100; stk.avgCost = 700;
    CHECK_FALSE(BuildPositionAnalysis({ stk }, 700.0, 2026, 10, 1).valid);
    CHECK_FALSE(BuildPositionAnalysis({ Held("20261130", 743, "P", 0, 450, 3.8, 1) },
                                      760.0, 2026, 10, 1).valid);
    // Covered call: stock leg rides along with the option.
    Position cc = stk; cc.costBasis = 70000; cc.marketPrice = 701;
    const auto a = BuildPositionAnalysis({ cc, Held("20261130", 720, "C", -1, 900, 8.0, 2) },
                                         701.0, 2026, 10, 1);
    REQUIRE(a.valid);
    CHECK(a.legs[0].stock);
    CHECK(a.netPrice == Catch::Approx(700.0 - 9.0));
}

TEST_CASE("ApplyManualMerge replaces overlapping merges and un-pins legs", "[strategy][merge]") {
    std::vector<std::vector<long>> merges   = { {1, 2}, {7, 8} };
    std::vector<std::vector<long>> ungroup  = { {3, 4}, {5, 6, 9} };
    REQUIRE(ApplyManualMerge(merges, ungroup, { 4, 2, 5, 4 }));
    // {1,2} shared leg 2 -> dropped; {7,8} untouched; new set sorted + deduped.
    REQUIRE(merges.size() == 2);
    CHECK(merges[0] == std::vector<long>{ 7, 8 });
    CHECK(merges[1] == std::vector<long>{ 2, 4, 5 });
    // 4 leaves {3,4} -> {3} (dropped, <2); 5 leaves {5,6,9} -> {6,9}.
    REQUIRE(ungroup.size() == 1);
    CHECK(ungroup[0] == std::vector<long>{ 6, 9 });
}

TEST_CASE("ApplyManualMerge needs two distinct legs", "[strategy][merge]") {
    std::vector<std::vector<long>> merges, ungroup;
    CHECK_FALSE(ApplyManualMerge(merges, ungroup, { 5, 5 }));
    CHECK_FALSE(ApplyManualMerge(merges, ungroup, { 0, 5 }));
    CHECK(merges.empty());
}

TEST_CASE("A manual merge groups legs the heuristic would not", "[strategy][merge]") {
    // Two unrelated-looking puts on different expiries and strikes.
    std::vector<Position> pos = {
        Opt("SPY", "20261016", 600, "P", -1, 0, 0, 11),
        Opt("SPY", "20261120", 590, "P",  1, 0, 0, 12),
        Opt("SPY", "20261016", 650, "C", -1, 0, 0, 13),
    };
    std::vector<std::vector<long>> merges, ungroup = { { 11, 13 } };
    REQUIRE(ApplyManualMerge(merges, ungroup, { 11, 12 }));
    CHECK(ungroup.empty());   // 11 un-pinned; {13} alone is dropped
    std::vector<ComboLink> links;
    for (const auto& m : merges) links.push_back({ m, GroupSource::Manual });
    const auto groups = ClassifyStrategies(pos, {}, links);
    bool found = false;
    for (const auto& g : groups)
        if (g.legIdx.size() == 2 && g.source == GroupSource::Manual) found = true;
    CHECK(found);
    CHECK(FindManualMerge(merges, { 12, 11 }) == 0);
    CHECK(FindManualMerge(merges, { 11, 13 }) == -1);
}

TEST_CASE("Roll plan: vertical closes and reopens one expiry out", "[strategy][roll]") {
    const std::vector<Position> held = {
        Held("20261016", 600, "P", -2, 450.0, 3.80, 1),
        Held("20261016", 595, "P",  2, 241.0, 2.10, 2),
    };
    const RollPlan r = BuildRollPlan(held, {"20261120", "20261016", "20261023"});
    REQUIRE(r.ok);
    CHECK(r.qty == 2);
    CHECK(r.toExpiry == "20261023");
    REQUIRE(r.legs.size() == 4);
    // Closing legs: buy back the short, sell out the long, held expiry.
    CHECK(r.legs[0].closing);  CHECK(r.legs[0].buy);   CHECK(r.legs[0].key.expiry == "20261016");
    CHECK(r.legs[1].closing);  CHECK(!r.legs[1].buy);
    // New legs: same strikes and sides, next expiry, ratio 1 each.
    CHECK(!r.legs[2].closing); CHECK(!r.legs[2].buy);  CHECK(r.legs[2].key.expiry == "20261023");
    CHECK(r.legs[2].key.strike == 600); CHECK(r.legs[2].key.right == 'P');
    CHECK(!r.legs[3].closing); CHECK(r.legs[3].buy);   CHECK(r.legs[3].ratio == 1);
}

TEST_CASE("Roll plan: calendar legs each roll to their own next expiry", "[strategy][roll]") {
    const std::vector<Position> held = {
        Held("20261016", 600, "C", -1, 300.0, 3.0, 1),
        Held("20261120", 600, "C",  1, 600.0, 6.0, 2),
    };
    const RollPlan r = BuildRollPlan(held, {"20261016", "20261023", "20261120", "20261218"});
    REQUIRE(r.ok);
    CHECK(r.legs[2].key.expiry == "20261023");
    CHECK(r.legs[3].key.expiry == "20261218");
    CHECK(r.toExpiry == "20261023");
}

TEST_CASE("Roll plan: refuses what it cannot build", "[strategy][roll]") {
    std::vector<Position> four = {
        Held("20261016", 590, "P",  1, 1, 1, 1), Held("20261016", 595, "P", -1, 1, 1, 2),
        Held("20261016", 605, "C", -1, 1, 1, 3), Held("20261016", 610, "C",  1, 1, 1, 4),
    };
    CHECK(!BuildRollPlan(four, {"20261016", "20261023"}).ok);          // 8 legs > 6
    CHECK(!BuildRollPlan({ four[0] }, {"20261016"}).ok);                 // no later expiry
    Position stk = four[0]; stk.assetClass = "STK";
    CHECK(!BuildRollPlan({ stk }, {"20261016", "20261023"}).ok);       // not an option
    CHECK(!BuildRollPlan({}, {"20261023"}).ok);
}

TEST_CASE("Opening combo legs leave out the legs that close a position", "[strategy][roll]") {
    const std::unordered_map<long, double> held = { {1, -2.0}, {2, 2.0} };
    // Roll: buy 1 (closes short), sell 2 (closes long), sell 3, buy 4 (new).
    const auto ids = OpeningComboLegs({ {1, true}, {2, false}, {3, false}, {4, true} }, held);
    CHECK(ids == std::vector<long>{3, 4});
    // Adding to a held leg keeps it.
    CHECK(OpeningComboLegs({ {1, false}, {2, true} }, held) == std::vector<long>{1, 2});
}

TEST_CASE("Grouped rows sort by strategy totals, mixed with singles", "[strategy][sort]") {
    // A bull put (unrealized -300 total) and a stock line (+100). Sorting by
    // Unrealized P&L ascending puts the spread first, by its total.
    std::vector<Position> pos = {
        Opt("SPY", "20261016", 600, "P", -1, 0, 0, 1),
        Opt("SPY", "20261016", 595, "P",  1, 0, 0, 2),
    };
    pos[0].unrealizedPnL = -400; pos[0].marketValue = -900; pos[0].costBasis = -500;
    pos[1].unrealizedPnL =  100; pos[1].marketValue =  300; pos[1].costBasis =  200;
    Position stk; stk.symbol = "AAPL"; stk.assetClass = "STK"; stk.conId = 9;
    stk.quantity = 10; stk.unrealizedPnL = 100; stk.dailyPnL = 5;
    pos.push_back(stk);

    auto g = ClassifyStrategies(pos, {}, { ComboLink{{1, 2}, GroupSource::Actual} });
    REQUIRE(g.size() == 2);
    SortStrategyGroups(g, pos, core::PositionColumn::UnrealizedPnL, /*ascending=*/true);
    CHECK(g[0].legIdx.size() == 2);   // the spread, at -300
    CHECK(g[1].legIdx.size() == 1);
    SortStrategyGroups(g, pos, core::PositionColumn::UnrealizedPnL, /*ascending=*/false);
    CHECK(g[0].legIdx.size() == 1);   // the stock, at +100

    // Avg Cost uses the net per combo: -300 / (100 x 1) = -3.00.
    const auto spread = g[1];
    CHECK(StrategySortValue(spread, pos, core::PositionColumn::AvgCost).num == Catch::Approx(-3.0));
    CHECK(StrategySortValue(spread, pos, core::PositionColumn::Quantity).num == 1.0);
    // Day P&L sorts by the daily P&L shown in that column, not price change.
    CHECK(StrategySortValue(g[0], pos, core::PositionColumn::DayChange).num == 5.0);
}

TEST_CASE("An option's average cost is shown per share, like its price", "[strategy][sort]") {
    // IB reports 621.00 per contract for a 6.21 premium.
    std::vector<Position> pos = { Opt("SPY", "20261016", 600, "C", 1, 0, 0, 1) };
    pos[0].avgCost = 621.0; pos[0].multiplier = "100";
    CHECK(AvgCostPerUnit(pos[0]) == Catch::Approx(6.21));
    pos[0].multiplier.clear();                        // missing: US default of 100
    CHECK(AvgCostPerUnit(pos[0]) == Catch::Approx(6.21));

    Position stk = Stock("AAPL", 10);
    stk.avgCost = 187.42;
    CHECK(AvgCostPerUnit(stk) == Catch::Approx(187.42));

    // The Avg Cost column sorts a single leg by the same per-share figure.
    const auto g = ClassifyStrategies(pos);
    REQUIRE(g.size() == 1);
    CHECK(StrategySortValue(g[0], pos, core::PositionColumn::AvgCost).num == Catch::Approx(6.21));
}

TEST_CASE("Leg effect: open, add, close, flip against the held position", "[strategy][leg-effect]") {
    CHECK(ClassifyLegEffect( 0.0, true,  1) == LegEffect::Open);
    CHECK(ClassifyLegEffect( 2.0, true,  1) == LegEffect::Add);
    CHECK(ClassifyLegEffect(-2.0, false, 1) == LegEffect::Add);
    // The live roll case: long 1 Oct09 767C, the roll sells 1 -> closes it.
    CHECK(ClassifyLegEffect( 1.0, false, 1) == LegEffect::Close);
    CHECK(ClassifyLegEffect(-3.0, true,  2) == LegEffect::Close);   // partial close
    CHECK(ClassifyLegEffect( 1.0, false, 2) == LegEffect::Flip);
    CHECK(std::string(LegEffectLabel(LegEffect::Close)) == "close");
}
