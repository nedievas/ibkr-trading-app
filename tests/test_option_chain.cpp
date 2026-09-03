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
