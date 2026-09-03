#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/models/OptionData.h"

namespace core::services {

// ---- Option chain pure logic -------------------------------------------------
// No IB API, no ImGui, no I/O — same contract as ChartAnalysis.h, so tests-core
// can cover it without a Gateway or a display.

// ── Contract-key comparison ──────────────────────────────────────────────────
// Kept here rather than on the POD in OptionData.h, per "models are POD-first".

inline bool KeyEqual(const OptionContractKey& a, const OptionContractKey& b) {
    return a.right == b.right && a.strike == b.strike &&
           a.expiry == b.expiry && a.symbol == b.symbol;
}

// Strict weak ordering, so keys can go in sorted vectors / set algorithms.
inline bool KeyLess(const OptionContractKey& a, const OptionContractKey& b) {
    if (a.symbol != b.symbol) return a.symbol < b.symbol;
    if (a.expiry != b.expiry) return a.expiry < b.expiry;
    if (a.strike != b.strike) return a.strike < b.strike;
    return a.right < b.right;
}

// ── Chain definition merge ───────────────────────────────────────────────────
// IB fires securityDefinitionOptionalParameter once per exchange that lists the
// underlying, each with its own (overlapping) expirations and strikes. Callers
// merge every callback into one meta, then call the End handler.
//
// tradingClass / multiplier are taken from the first callback that supplies
// them, and a later exchange does not overwrite a value already set — the SMART
// row is the one that matters and arrives first in practice, and a regional
// exchange reporting a different trading class should not clobber it.

inline void MergeChainDefinition(OptionChainMeta& meta,
                                 const std::string& tradingClass,
                                 const std::string& multiplier,
                                 int underlyingConId,
                                 const std::vector<std::string>& expirations,
                                 const std::vector<double>& strikes) {
    if (meta.tradingClass.empty()) meta.tradingClass = tradingClass;
    if (meta.multiplier.empty())   meta.multiplier   = multiplier;
    if (meta.underlyingConId == 0) meta.underlyingConId = underlyingConId;

    meta.expirations.insert(meta.expirations.end(), expirations.begin(), expirations.end());
    std::sort(meta.expirations.begin(), meta.expirations.end());
    meta.expirations.erase(std::unique(meta.expirations.begin(), meta.expirations.end()),
                           meta.expirations.end());

    meta.strikes.insert(meta.strikes.end(), strikes.begin(), strikes.end());
    std::sort(meta.strikes.begin(), meta.strikes.end());
    meta.strikes.erase(std::unique(meta.strikes.begin(), meta.strikes.end()),
                       meta.strikes.end());
}

// ── ATM detection ────────────────────────────────────────────────────────────
// Index of the strike nearest the underlying price. -1 when there are no
// strikes. Ties (price exactly between two strikes) resolve to the lower
// strike, matching std::lower_bound's bias — arbitrary but deterministic.

inline int FindAtmIndex(const std::vector<double>& strikes, double underlyingPrice) {
    if (strikes.empty()) return -1;
    int    best     = 0;
    double bestDist = std::fabs(strikes[0] - underlyingPrice);
    for (std::size_t i = 1; i < strikes.size(); ++i) {
        const double d = std::fabs(strikes[i] - underlyingPrice);
        if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
    }
    return best;
}

// ── Moneyness ────────────────────────────────────────────────────────────────

enum class Moneyness { ITM, ATM, OTM };

// `atmTolerance` is an absolute price band around the underlying within which a
// strike counts as ATM. 0 means "only an exact match is ATM".
inline Moneyness ClassifyMoneyness(double strike, double underlyingPrice,
                                   char right, double atmTolerance = 0.0) {
    if (std::fabs(strike - underlyingPrice) <= atmTolerance) return Moneyness::ATM;
    const bool isCall = (right == 'C' || right == 'c');
    if (isCall) return strike < underlyingPrice ? Moneyness::ITM : Moneyness::OTM;
    return strike > underlyingPrice ? Moneyness::ITM : Moneyness::OTM;
}

// ── Strike-range filter ──────────────────────────────────────────────────────
// Inclusive [lo, hi] index range covering the ATM strike plus `nEachSide` on
// each side, clipped to the ends of the list. Returns {-1,-1} on empty input.
// nEachSide < 0 means "no filter" — the whole list.

struct StrikeRange { int lo = -1, hi = -1; };

inline StrikeRange StrikeRangeAroundAtm(const std::vector<double>& strikes,
                                        double underlyingPrice, int nEachSide) {
    if (strikes.empty()) return {};
    const int last = static_cast<int>(strikes.size()) - 1;
    if (nEachSide < 0) return {0, last};
    const int atm = FindAtmIndex(strikes, underlyingPrice);
    return { std::max(0, atm - nEachSide), std::min(last, atm + nEachSide) };
}

// ── Subscription diffing ─────────────────────────────────────────────────────
// The core of the visible-row streaming strategy. Given what the UI wants
// subscribed and what currently is, work out the minimum set of subscribes and
// cancels — and enforce a hard ceiling on concurrent option subscriptions so a
// big chain cannot exhaust the account's market-data lines.
//
// When `desired` exceeds `maxSubs`, contracts closest to the underlying price
// win: those are the strikes a trader is actually looking at. Ordering within
// the trimmed set is by strike distance, then KeyLess for determinism.

struct SubscriptionDiff {
    std::vector<OptionContractKey> toSubscribe;
    std::vector<OptionContractKey> toCancel;
};

inline SubscriptionDiff DiffSubscriptions(std::vector<OptionContractKey> desired,
                                          std::vector<OptionContractKey> current,
                                          int maxSubs, double underlyingPrice) {
    SubscriptionDiff out;

    // Trim to the cap, keeping the strikes nearest the money.
    if (maxSubs >= 0 && static_cast<int>(desired.size()) > maxSubs) {
        std::stable_sort(desired.begin(), desired.end(),
                         [underlyingPrice](const OptionContractKey& a,
                                           const OptionContractKey& b) {
                             const double da = std::fabs(a.strike - underlyingPrice);
                             const double db = std::fabs(b.strike - underlyingPrice);
                             if (da != db) return da < db;
                             return KeyLess(a, b);
                         });
        desired.resize(static_cast<std::size_t>(maxSubs));
    }

    std::sort(desired.begin(), desired.end(), KeyLess);
    std::sort(current.begin(), current.end(), KeyLess);
    desired.erase(std::unique(desired.begin(), desired.end(), KeyEqual), desired.end());
    current.erase(std::unique(current.begin(), current.end(), KeyEqual), current.end());

    std::set_difference(desired.begin(), desired.end(),
                        current.begin(), current.end(),
                        std::back_inserter(out.toSubscribe), KeyLess);
    std::set_difference(current.begin(), current.end(),
                        desired.begin(), desired.end(),
                        std::back_inserter(out.toCancel), KeyLess);
    return out;
}

// ── Vertical spread pricing ──────────────────────────────────────────────────
// Net price of a two-leg vertical at the given per-leg mids. Positive is a net
// debit (you pay), negative a net credit (you receive). The sign convention
// matters: IB accepts a negative limit price on a BAG order for a credit
// spread, so callers must not clamp this to >= 0.

inline double SpreadNetPrice(double longLegMid, double shortLegMid) {
    return longLegMid - shortLegMid;
}

// Mid of a bid/ask pair, falling back to whichever side exists when the book is
// one-sided (common on illiquid strikes). Returns 0 when neither side has a
// price, which callers treat as "not priceable yet".
inline double QuoteMid(double bid, double ask) {
    if (bid > 0.0 && ask > 0.0) return (bid + ask) * 0.5;
    if (ask > 0.0) return ask;
    if (bid > 0.0) return bid;
    return 0.0;
}

// ── Expected move ────────────────────────────────────────────────────────────
// tastytrade's weighted definition, which is what traders reading a chain
// expect that label to mean:
//
//   EM = 0.60 * ATM straddle + 0.30 * 1st OTM strangle + 0.10 * 2nd OTM strangle
//
// This is deliberately NOT the textbook spot * IV * sqrt(DTE/365) one-sigma
// move. Both are called "expected move" in the wild, but they disagree —
// notably around earnings, where the straddle carries event premium the
// annualised-IV formula smears across the whole year. Since the rest of the
// chain is modelled on tastytrade's presentation, the straddle definition is
// the consistent one.
//
// Each argument is the total premium of that leg pair (call mid + put mid).
// When the OTM strangles have not priced — illiquid strikes, or simply not
// subscribed yet — this falls back to the simple 0.85 * straddle method
// tastytrade documents for manual use. Returns 0 when even the straddle is
// unpriced, which callers render as "no value" rather than zero.

inline double ExpectedMoveFromStraddle(double straddle, double strangle1,
                                       double strangle2) {
    if (straddle <= 0.0) return 0.0;
    if (strangle1 > 0.0 && strangle2 > 0.0)
        return 0.60 * straddle + 0.30 * strangle1 + 0.10 * strangle2;
    return 0.85 * straddle;
}

}  // namespace core::services
