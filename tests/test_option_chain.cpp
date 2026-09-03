#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/services/OptionChain.h"

using namespace core;
using namespace core::services;

namespace {
OptionContractKey K(const char* expiry, double strike, char right,
                    const char* sym = "AAPL") {
    OptionContractKey k;
    k.symbol = sym; k.expiry = expiry; k.strike = strike; k.right = right;
    return k;
}
}  // namespace

// ── MergeChainDefinition ─────────────────────────────────────────────────────

TEST_CASE("MergeChainDefinition merges one exchange", "[options][chain]") {
    OptionChainMeta meta;
    meta.symbol = "AAPL";
    MergeChainDefinition(meta, "AAPL", "100", 265598,
                         {"20260918", "20261016"}, {190.0, 195.0, 200.0});

    REQUIRE(meta.tradingClass    == "AAPL");
    REQUIRE(meta.multiplier      == "100");
    REQUIRE(meta.underlyingConId == 265598);
    REQUIRE(meta.expirations.size() == 2);
    REQUIRE(meta.strikes.size()     == 3);
}

TEST_CASE("MergeChainDefinition dedups across exchanges", "[options][chain]") {
    // IB fires once per listing exchange with overlapping sets — dedup is real
    // logic, not defensive coding.
    OptionChainMeta meta;
    MergeChainDefinition(meta, "AAPL", "100", 265598,
                         {"20260918", "20261016"}, {190.0, 195.0});
    MergeChainDefinition(meta, "AAPL", "100", 265598,
                         {"20261016", "20261120"}, {195.0, 200.0});

    REQUIRE(meta.expirations.size() == 3);
    REQUIRE(meta.expirations[0] == "20260918");
    REQUIRE(meta.expirations[1] == "20261016");
    REQUIRE(meta.expirations[2] == "20261120");
    REQUIRE(meta.strikes.size() == 3);
    REQUIRE(meta.strikes[0] == 190.0);
    REQUIRE(meta.strikes[2] == 200.0);
}

TEST_CASE("MergeChainDefinition sorts unsorted input", "[options][chain]") {
    OptionChainMeta meta;
    MergeChainDefinition(meta, "AAPL", "100", 1,
                         {"20261120", "20260918"}, {200.0, 190.0, 195.0});
    REQUIRE(meta.expirations[0] == "20260918");
    REQUIRE(meta.strikes[0] == 190.0);
    REQUIRE(meta.strikes[2] == 200.0);
}

TEST_CASE("MergeChainDefinition does not clobber an established trading class",
          "[options][chain]") {
    // A regional exchange reporting a different class must not overwrite the
    // first (SMART) one.
    OptionChainMeta meta;
    MergeChainDefinition(meta, "AAPL",  "100", 265598, {"20260918"}, {190.0});
    MergeChainDefinition(meta, "AAPL1", "100", 999,    {"20260918"}, {190.0});
    REQUIRE(meta.tradingClass    == "AAPL");
    REQUIRE(meta.underlyingConId == 265598);
}

TEST_CASE("MergeChainDefinition handles empty input", "[options][chain]") {
    OptionChainMeta meta;
    MergeChainDefinition(meta, "", "", 0, {}, {});
    REQUIRE(meta.expirations.empty());
    REQUIRE(meta.strikes.empty());
}

// ── FindAtmIndex ─────────────────────────────────────────────────────────────

TEST_CASE("FindAtmIndex picks the nearest strike", "[options][atm]") {
    std::vector<double> strikes = {190, 195, 200, 205, 210};
    REQUIRE(FindAtmIndex(strikes, 197.4) == 1);   // 195 closer than 200
    REQUIRE(FindAtmIndex(strikes, 202.6) == 3);   // 205 closer than 200
    REQUIRE(FindAtmIndex(strikes, 200.0) == 2);   // exact match
}

TEST_CASE("FindAtmIndex clamps outside the strike range", "[options][atm]") {
    std::vector<double> strikes = {190, 195, 200};
    REQUIRE(FindAtmIndex(strikes, 10.0)   == 0);
    REQUIRE(FindAtmIndex(strikes, 9999.0) == 2);
}

TEST_CASE("FindAtmIndex ties resolve to the lower strike", "[options][atm]") {
    std::vector<double> strikes = {190, 200};
    REQUIRE(FindAtmIndex(strikes, 195.0) == 0);
}

TEST_CASE("FindAtmIndex returns -1 on empty strikes", "[options][atm]") {
    REQUIRE(FindAtmIndex({}, 100.0) == -1);
}

// ── ClassifyMoneyness ────────────────────────────────────────────────────────

TEST_CASE("ClassifyMoneyness for calls", "[options][moneyness]") {
    REQUIRE(ClassifyMoneyness(190, 200, 'C') == Moneyness::ITM);
    REQUIRE(ClassifyMoneyness(210, 200, 'C') == Moneyness::OTM);
    REQUIRE(ClassifyMoneyness(200, 200, 'C') == Moneyness::ATM);
}

TEST_CASE("ClassifyMoneyness for puts is the mirror", "[options][moneyness]") {
    REQUIRE(ClassifyMoneyness(210, 200, 'P') == Moneyness::ITM);
    REQUIRE(ClassifyMoneyness(190, 200, 'P') == Moneyness::OTM);
    REQUIRE(ClassifyMoneyness(200, 200, 'P') == Moneyness::ATM);
}

TEST_CASE("ClassifyMoneyness honours the ATM tolerance band", "[options][moneyness]") {
    REQUIRE(ClassifyMoneyness(199, 200, 'C', 2.0) == Moneyness::ATM);
    REQUIRE(ClassifyMoneyness(199, 200, 'C', 0.5) == Moneyness::ITM);
    REQUIRE(ClassifyMoneyness(201, 200, 'P', 2.0) == Moneyness::ATM);
}

TEST_CASE("ClassifyMoneyness accepts lowercase right", "[options][moneyness]") {
    REQUIRE(ClassifyMoneyness(190, 200, 'c') == Moneyness::ITM);
}

// ── StrikeRangeAroundAtm ─────────────────────────────────────────────────────

TEST_CASE("StrikeRangeAroundAtm centres on ATM", "[options][range]") {
    std::vector<double> strikes = {180, 185, 190, 195, 200, 205, 210};
    auto r = StrikeRangeAroundAtm(strikes, 195.0, 2);
    REQUIRE(r.lo == 1);
    REQUIRE(r.hi == 5);
}

TEST_CASE("StrikeRangeAroundAtm clips at both ends", "[options][range]") {
    std::vector<double> strikes = {180, 185, 190};
    auto lowEnd = StrikeRangeAroundAtm(strikes, 180.0, 5);
    REQUIRE(lowEnd.lo == 0);
    REQUIRE(lowEnd.hi == 2);

    auto highEnd = StrikeRangeAroundAtm(strikes, 190.0, 5);
    REQUIRE(highEnd.lo == 0);
    REQUIRE(highEnd.hi == 2);
}

TEST_CASE("StrikeRangeAroundAtm with negative n means no filter", "[options][range]") {
    std::vector<double> strikes = {180, 185, 190, 195};
    auto r = StrikeRangeAroundAtm(strikes, 185.0, -1);
    REQUIRE(r.lo == 0);
    REQUIRE(r.hi == 3);
}

TEST_CASE("StrikeRangeAroundAtm on empty strikes is invalid", "[options][range]") {
    auto r = StrikeRangeAroundAtm({}, 100.0, 3);
    REQUIRE(r.lo == -1);
    REQUIRE(r.hi == -1);
}

TEST_CASE("StrikeRangeAroundAtm with n=0 is the ATM strike alone", "[options][range]") {
    std::vector<double> strikes = {180, 185, 190};
    auto r = StrikeRangeAroundAtm(strikes, 186.0, 0);
    REQUIRE(r.lo == 1);
    REQUIRE(r.hi == 1);
}

// ── DiffSubscriptions ────────────────────────────────────────────────────────

TEST_CASE("DiffSubscriptions subscribes everything when nothing is live",
          "[options][subs]") {
    std::vector<OptionContractKey> desired = {
        K("20260918", 195, 'C'), K("20260918", 195, 'P')};
    auto d = DiffSubscriptions(desired, {}, 60, 195.0);
    REQUIRE(d.toSubscribe.size() == 2);
    REQUIRE(d.toCancel.empty());
}

TEST_CASE("DiffSubscriptions is a no-op when the visible set is unchanged",
          "[options][subs]") {
    std::vector<OptionContractKey> keys = {
        K("20260918", 195, 'C'), K("20260918", 200, 'C')};
    auto d = DiffSubscriptions(keys, keys, 60, 197.0);
    REQUIRE(d.toSubscribe.empty());
    REQUIRE(d.toCancel.empty());
}

TEST_CASE("DiffSubscriptions cancels rows scrolled out of view", "[options][subs]") {
    std::vector<OptionContractKey> current = {
        K("20260918", 190, 'C'), K("20260918", 195, 'C')};
    std::vector<OptionContractKey> desired = {
        K("20260918", 195, 'C'), K("20260918", 200, 'C')};

    auto d = DiffSubscriptions(desired, current, 60, 197.0);
    REQUIRE(d.toSubscribe.size() == 1);
    REQUIRE(d.toSubscribe[0].strike == 200.0);
    REQUIRE(d.toCancel.size() == 1);
    REQUIRE(d.toCancel[0].strike == 190.0);
}

TEST_CASE("DiffSubscriptions swaps the whole set on expiry change",
          "[options][subs]") {
    std::vector<OptionContractKey> current = {
        K("20260918", 195, 'C'), K("20260918", 200, 'C')};
    std::vector<OptionContractKey> desired = {
        K("20261016", 195, 'C'), K("20261016", 200, 'C')};

    auto d = DiffSubscriptions(desired, current, 60, 197.0);
    REQUIRE(d.toSubscribe.size() == 2);
    REQUIRE(d.toCancel.size()    == 2);
    for (const auto& k : d.toSubscribe) REQUIRE(k.expiry == "20261016");
    for (const auto& k : d.toCancel)    REQUIRE(k.expiry == "20260918");
}

TEST_CASE("DiffSubscriptions enforces the cap, keeping strikes nearest the money",
          "[options][subs]") {
    std::vector<OptionContractKey> desired = {
        K("20260918", 100, 'C'),   // far OTM
        K("20260918", 195, 'C'),   // nearest
        K("20260918", 200, 'C'),   // second nearest
        K("20260918", 900, 'C')};  // far ITM
    auto d = DiffSubscriptions(desired, {}, 2, 197.0);

    REQUIRE(d.toSubscribe.size() == 2);
    std::vector<double> got;
    for (const auto& k : d.toSubscribe) got.push_back(k.strike);
    REQUIRE(std::find(got.begin(), got.end(), 195.0) != got.end());
    REQUIRE(std::find(got.begin(), got.end(), 200.0) != got.end());
}

TEST_CASE("DiffSubscriptions cancels live rows trimmed away by the cap",
          "[options][subs]") {
    // The far strike is live but the cap no longer admits it — it must be
    // cancelled, not silently leaked.
    std::vector<OptionContractKey> current = {K("20260918", 900, 'C')};
    std::vector<OptionContractKey> desired = {
        K("20260918", 195, 'C'), K("20260918", 900, 'C')};

    auto d = DiffSubscriptions(desired, current, 1, 197.0);
    REQUIRE(d.toSubscribe.size() == 1);
    REQUIRE(d.toSubscribe[0].strike == 195.0);
    REQUIRE(d.toCancel.size() == 1);
    REQUIRE(d.toCancel[0].strike == 900.0);
}

TEST_CASE("DiffSubscriptions cancels everything when desired is empty",
          "[options][subs]") {
    std::vector<OptionContractKey> current = {
        K("20260918", 195, 'C'), K("20260918", 200, 'P')};
    auto d = DiffSubscriptions({}, current, 60, 197.0);
    REQUIRE(d.toSubscribe.empty());
    REQUIRE(d.toCancel.size() == 2);
}

TEST_CASE("DiffSubscriptions tolerates duplicates in its inputs",
          "[options][subs]") {
    std::vector<OptionContractKey> desired = {
        K("20260918", 195, 'C'), K("20260918", 195, 'C')};
    auto d = DiffSubscriptions(desired, {}, 60, 195.0);
    REQUIRE(d.toSubscribe.size() == 1);
}

TEST_CASE("DiffSubscriptions distinguishes calls from puts at one strike",
          "[options][subs]") {
    std::vector<OptionContractKey> current = {K("20260918", 195, 'C')};
    std::vector<OptionContractKey> desired = {K("20260918", 195, 'P')};
    auto d = DiffSubscriptions(desired, current, 60, 195.0);
    REQUIRE(d.toSubscribe.size() == 1);
    REQUIRE(d.toSubscribe[0].right == 'P');
    REQUIRE(d.toCancel.size() == 1);
    REQUIRE(d.toCancel[0].right == 'C');
}

TEST_CASE("DiffSubscriptions with cap 0 cancels everything", "[options][subs]") {
    std::vector<OptionContractKey> current = {K("20260918", 195, 'C')};
    std::vector<OptionContractKey> desired = {K("20260918", 195, 'C')};
    auto d = DiffSubscriptions(desired, current, 0, 195.0);
    REQUIRE(d.toSubscribe.empty());
    REQUIRE(d.toCancel.size() == 1);
}

// ── Spread pricing ───────────────────────────────────────────────────────────

TEST_CASE("SpreadNetPrice yields a debit when the long leg costs more",
          "[options][spread]") {
    REQUIRE(SpreadNetPrice(7.50, 3.20) == Catch::Approx(4.30));
}

TEST_CASE("SpreadNetPrice yields a negative net for a credit spread",
          "[options][spread]") {
    // Credit spreads are legitimate and IB accepts a negative BAG limit price —
    // callers must not clamp this to >= 0.
    REQUIRE(SpreadNetPrice(3.20, 7.50) == Catch::Approx(-4.30));
}

TEST_CASE("QuoteMid averages a two-sided book", "[options][spread]") {
    REQUIRE(QuoteMid(7.00, 7.50) == Catch::Approx(7.25));
}

TEST_CASE("QuoteMid falls back on a one-sided book", "[options][spread]") {
    REQUIRE(QuoteMid(0.0, 7.50) == Catch::Approx(7.50));
    REQUIRE(QuoteMid(7.00, 0.0) == Catch::Approx(7.00));
}

TEST_CASE("QuoteMid returns 0 when the contract has no price yet",
          "[options][spread]") {
    REQUIRE(QuoteMid(0.0, 0.0) == 0.0);
}

// ── Expected move ────────────────────────────────────────────────────────────

TEST_CASE("ExpectedMoveFromStraddle uses the tastytrade weighting",
          "[options][expectedmove]") {
    // 0.60*10 + 0.30*6 + 0.10*3 = 6.0 + 1.8 + 0.3 = 8.1
    REQUIRE(ExpectedMoveFromStraddle(10.0, 6.0, 3.0) == Catch::Approx(8.1));
}

TEST_CASE("ExpectedMoveFromStraddle falls back to 0.85x straddle",
          "[options][expectedmove]") {
    // Illiquid wings: neither strangle priced -> documented manual method.
    REQUIRE(ExpectedMoveFromStraddle(10.0, 0.0, 0.0) == Catch::Approx(8.5));
    // A single missing wing is still not enough for the weighted form.
    REQUIRE(ExpectedMoveFromStraddle(10.0, 6.0, 0.0) == Catch::Approx(8.5));
    REQUIRE(ExpectedMoveFromStraddle(10.0, 0.0, 3.0) == Catch::Approx(8.5));
}

TEST_CASE("ExpectedMoveFromStraddle returns 0 when the straddle is unpriced",
          "[options][expectedmove]") {
    REQUIRE(ExpectedMoveFromStraddle(0.0,  6.0, 3.0) == 0.0);
    REQUIRE(ExpectedMoveFromStraddle(-1.0, 6.0, 3.0) == 0.0);
}

TEST_CASE("ExpectedMoveFromStraddle is tighter than the raw straddle",
          "[options][expectedmove]") {
    // The weighting exists to pull the range in; a wider result would mean the
    // weights were applied wrongly.
    const double em = ExpectedMoveFromStraddle(10.0, 6.0, 3.0);
    REQUIRE(em < 10.0);
    REQUIRE(em > 0.0);
}

// ── IVx (VIX-style) ──────────────────────────────────────────────────────────

namespace {
ChainStrikeQuote SQ(double strike, double cb, double ca, double pb, double pa) {
    ChainStrikeQuote q;
    q.strike = strike;
    q.callBid = cb; q.callAsk = ca;
    q.putBid  = pb; q.putAsk  = pa;
    return q;
}
}  // namespace

TEST_CASE("ImpliedVolatilityVixStyle matches a hand-computed chain",
          "[options][ivx]") {
    // Strikes 90/100/110, T=1, r=0. Call and put mids are both 5.00 at 100, so
    // F = 100 and K0 = 100. Contributions:
    //   (10/90^2)*2 + (10/100^2)*5 + (10/110^2)*2 = 0.00912203
    //   var = 2 * 0.00912203 - 0 = 0.01824406  ->  sigma = 0.135070
    std::vector<ChainStrikeQuote> rows = {
        SQ( 90, 12.0, 12.4, 1.9, 2.1),
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
    };
    REQUIRE(ImpliedVolatilityVixStyle(rows, 1.0, 0.0) == Catch::Approx(0.135070).epsilon(1e-4));
}

TEST_CASE("ImpliedVolatilityVixStyle rises with richer option premium",
          "[options][ivx]") {
    std::vector<ChainStrikeQuote> cheap = {
        SQ( 90, 12.0, 12.4, 1.9, 2.1),
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
    };
    std::vector<ChainStrikeQuote> rich = {
        SQ( 90, 12.0, 12.4, 3.9, 4.1),
        SQ(100,  9.9, 10.1, 9.9, 10.1),
        SQ(110,  3.9,  4.1, 12.0, 12.4),
    };
    REQUIRE(ImpliedVolatilityVixStyle(rich,  1.0) >
            ImpliedVolatilityVixStyle(cheap, 1.0));
}

TEST_CASE("ImpliedVolatilityVixStyle stops after two zero-bid strikes",
          "[options][ivx]") {
    // Cboe's truncation rule: the far wing past two consecutive zero bids must
    // not contribute, however tempting its ask looks.
    std::vector<ChainStrikeQuote> withWing = {
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
        SQ(120,  0.0,  0.5, 20.0, 20.4),   // zero bid #1
        SQ(130,  0.0,  0.5, 30.0, 30.4),   // zero bid #2 -> stop
        SQ(140,  9.0,  9.5, 40.0, 40.4),   // must be ignored
    };
    std::vector<ChainStrikeQuote> truncated = {
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
        SQ(120,  0.0,  0.5, 20.0, 20.4),
        SQ(130,  0.0,  0.5, 30.0, 30.4),
    };
    // The 140 row still shifts dK on its neighbour, so compare that the fat
    // far-wing premium did not blow the result up.
    const double a = ImpliedVolatilityVixStyle(withWing,  1.0);
    const double b = ImpliedVolatilityVixStyle(truncated, 1.0);
    REQUIRE(a > 0.0);
    REQUIRE(b > 0.0);
    REQUIRE(a == Catch::Approx(b).epsilon(0.05));
}

TEST_CASE("ImpliedVolatilityVixStyle differs from plain ATM implied vol",
          "[options][ivx]") {
    // A skewed chain: expensive downside puts. A VIX-style integral picks that
    // up; sampling only the ATM strike would not. This is the whole reason the
    // metric exists, so assert the wing actually moves the number.
    std::vector<ChainStrikeQuote> flat = {
        SQ( 80,  0.0,  0.0, 0.9, 1.1),
        SQ( 90, 12.0, 12.4, 1.9, 2.1),
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
    };
    std::vector<ChainStrikeQuote> skewed = flat;
    skewed[0].putBid = 4.9; skewed[0].putAsk = 5.1;   // fat left tail
    REQUIRE(ImpliedVolatilityVixStyle(skewed, 1.0) >
            ImpliedVolatilityVixStyle(flat,   1.0));
}

TEST_CASE("ImpliedVolatilityVixStyle rejects degenerate input", "[options][ivx]") {
    std::vector<ChainStrikeQuote> rows = {
        SQ( 90, 12.0, 12.4, 1.9, 2.1),
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
    };
    REQUIRE(ImpliedVolatilityVixStyle(rows, 0.0)  == 0.0);   // no time value
    REQUIRE(ImpliedVolatilityVixStyle(rows, -1.0) == 0.0);
    REQUIRE(ImpliedVolatilityVixStyle({}, 1.0)    == 0.0);   // empty
    REQUIRE(ImpliedVolatilityVixStyle({SQ(100, 4.9, 5.1, 4.9, 5.1)}, 1.0) == 0.0);
    // No strike has both sides priced -> no forward, no result.
    std::vector<ChainStrikeQuote> oneSided = {
        SQ( 90, 0.0, 0.0, 1.9, 2.1),
        SQ(100, 0.0, 0.0, 4.9, 5.1),
        SQ(110, 0.0, 0.0, 12.0, 12.4),
    };
    REQUIRE(ImpliedVolatilityVixStyle(oneSided, 1.0) == 0.0);
}

TEST_CASE("ImpliedVolatilityVixStyle sorts unsorted strikes", "[options][ivx]") {
    std::vector<ChainStrikeQuote> ordered = {
        SQ( 90, 12.0, 12.4, 1.9, 2.1),
        SQ(100,  4.9,  5.1, 4.9, 5.1),
        SQ(110,  1.9,  2.1, 12.0, 12.4),
    };
    std::vector<ChainStrikeQuote> shuffled = {ordered[2], ordered[0], ordered[1]};
    REQUIRE(ImpliedVolatilityVixStyle(shuffled, 1.0) ==
            Catch::Approx(ImpliedVolatilityVixStyle(ordered, 1.0)));
}

// ── Strategy metrics ─────────────────────────────────────────────────────────

namespace {
StrategyLeg LEG(double strike, char right, int ratio, double price,
                double delta = 0.0, double theta = 0.0) {
    StrategyLeg l;
    l.strike = strike; l.right = right; l.ratio = ratio;
    l.price = price; l.delta = delta; l.theta = theta;
    return l;
}
}  // namespace

TEST_CASE("ComputeStrategyMetrics matches the tastytrade SPX reference ticket",
          "[options][metrics]") {
    // Reference from the platform's order-entry strip:
    //   +1 SPX Sep20 2790 P @ 33.90 / -1 SPX Sep20 2770 P @ 29.30
    //   net 4.35 debit, multiplier 100
    //   -> Max Prof 1,565   Max Loss -435
    std::vector<StrategyLeg> legs = {
        LEG(2790, 'P',  1, 33.90),
        LEG(2770, 'P', -1, 29.30),
    };
    const auto m = ComputeStrategyMetrics(legs, /*netPrice=*/4.35, /*multiplier=*/100.0);

    REQUIRE(m.valid);
    REQUIRE(m.maxProfit == Catch::Approx(1565.0));
    REQUIRE(m.maxLoss   == Catch::Approx(-435.0));
    // A vertical is defined-risk on both sides.
    REQUIRE_FALSE(m.profitUnbounded);
    REQUIRE_FALSE(m.lossUnbounded);
}

TEST_CASE("ComputeStrategyMetrics: credit vertical mirrors the debit case",
          "[options][metrics]") {
    // Short the same 20-wide put spread for a 4.35 credit: risk and reward swap.
    std::vector<StrategyLeg> legs = {
        LEG(2790, 'P', -1, 33.90),
        LEG(2770, 'P',  1, 29.30),
    };
    const auto m = ComputeStrategyMetrics(legs, -4.35, 100.0);
    REQUIRE(m.maxProfit == Catch::Approx(435.0));
    REQUIRE(m.maxLoss   == Catch::Approx(-1565.0));
}

TEST_CASE("ComputeStrategyMetrics flags unbounded loss on a naked short call",
          "[options][metrics]") {
    // No finite max loss exists here; displaying one would be a lie.
    std::vector<StrategyLeg> legs = { LEG(100, 'C', -1, 2.0) };
    const auto m = ComputeStrategyMetrics(legs, -2.0, 100.0);
    REQUIRE(m.lossUnbounded);
    REQUIRE_FALSE(m.profitUnbounded);
    REQUIRE(m.maxProfit == Catch::Approx(200.0));   // keeps the credit
}

TEST_CASE("ComputeStrategyMetrics flags unbounded profit on a long call",
          "[options][metrics]") {
    std::vector<StrategyLeg> legs = { LEG(100, 'C', 1, 2.0) };
    const auto m = ComputeStrategyMetrics(legs, 2.0, 100.0);
    REQUIRE(m.profitUnbounded);
    REQUIRE_FALSE(m.lossUnbounded);
    REQUIRE(m.maxLoss == Catch::Approx(-200.0));    // premium paid
}

TEST_CASE("ComputeStrategyMetrics handles a four-leg iron condor",
          "[options][metrics]") {
    // Both wings defined: 10-wide each side, 2.00 credit.
    std::vector<StrategyLeg> legs = {
        LEG( 90, 'P',  1, 0.50),
        LEG( 95, 'P', -1, 1.50),
        LEG(105, 'C', -1, 1.50),
        LEG(110, 'C',  1, 0.50),
    };
    const auto m = ComputeStrategyMetrics(legs, -2.0, 100.0);
    REQUIRE_FALSE(m.profitUnbounded);
    REQUIRE_FALSE(m.lossUnbounded);
    REQUIRE(m.maxProfit == Catch::Approx(200.0));    // credit kept
    REQUIRE(m.maxLoss   == Catch::Approx(-300.0));   // 5 wide - 2 credit
}

TEST_CASE("ComputeStrategyMetrics scales greeks by ratio and multiplier",
          "[options][metrics]") {
    std::vector<StrategyLeg> legs = {
        LEG(2790, 'P',  1, 33.90, -0.45, -0.031),
        LEG(2770, 'P', -1, 29.30, -0.42, -0.004),
    };
    const auto m = ComputeStrategyMetrics(legs, 4.35, 100.0);
    // (1*-0.45 + -1*-0.42) * 100 = -3.00
    REQUIRE(m.netDelta == Catch::Approx(-3.0));
    // (1*-0.031 + -1*-0.004) * 100 = -2.70
    REQUIRE(m.netTheta == Catch::Approx(-2.7));
}

TEST_CASE("ComputeStrategyMetrics reports extrinsic for an all-OTM spread",
          "[options][metrics]") {
    // SPX well above both put strikes: every leg is pure time value, so the
    // net extrinsic is the net premium, shown negative for a debit paid.
    std::vector<StrategyLeg> legs = {
        LEG(2790, 'P',  1, 33.90),
        LEG(2770, 'P', -1, 29.30),
    };
    const auto m = ComputeStrategyMetrics(legs, 4.60, 100.0, /*spot=*/2900.0);
    REQUIRE(m.extrinsic == Catch::Approx(-460.0));
}

TEST_CASE("ComputeStrategyMetrics rejects degenerate input", "[options][metrics]") {
    REQUIRE_FALSE(ComputeStrategyMetrics({}, 1.0, 100.0).valid);
    REQUIRE_FALSE(ComputeStrategyMetrics({LEG(100, 'C', 1, 2.0)}, 1.0, 0.0).valid);
}
