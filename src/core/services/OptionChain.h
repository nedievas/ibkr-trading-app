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

// ── IVx: VIX-style implied volatility per expiration ─────────────────────────
// Cboe's model-free (variance-swap) construction, applied to a single
// expiration cycle rather than interpolated to 30 days:
//
//   sigma^2 = (2/T) * SUM_i (dK_i / K_i^2) * e^(rT) * Q(K_i)
//             - (1/T) * (F/K0 - 1)^2
//   IVx     = sqrt(sigma^2)
//
// where F is the forward implied by put-call parity at the strike whose
// call/put mids are closest together, K0 is the first strike at or below F,
// Q(K_i) is the OTM option's mid (both sides averaged at K0), and dK_i is the
// half-distance between neighbouring strikes.
//
// This is NOT the ATM implied vol. ATM IV samples one point on the smile; the
// VIX construction integrates the whole OTM wing, so the two diverge whenever
// there is skew — which is essentially always on equity index options.
//
// Two honest caveats for callers:
//  * `r` is the risk-free rate. Cboe uses the bond-equivalent yield of the
//    T-bills bracketing the expiry; we have no rate feed, so callers pass an
//    approximation. e^(rT) is within ~0.1% of 1 for a month at 1%, so the
//    error is small for near expiries and grows with T.
//  * Accuracy depends on having the wings. Cboe walks strikes outward until it
//    hits two consecutive zero-bid strikes. A caller that only supplies the
//    strikes near the money will truncate the integral early and understate
//    IVx.
//
// Returns 0 when the inputs cannot support the calculation, which callers
// render as "no value".

struct ChainStrikeQuote {
    double strike  = 0.0;
    double callBid = 0.0, callAsk = 0.0;
    double putBid  = 0.0, putAsk  = 0.0;
};

inline double ImpliedVolatilityVixStyle(std::vector<ChainStrikeQuote> rows,
                                        double T, double r = 0.0) {
    if (T <= 0.0 || rows.size() < 3) return 0.0;
    std::sort(rows.begin(), rows.end(),
              [](const ChainStrikeQuote& a, const ChainStrikeQuote& b) {
                  return a.strike < b.strike;
              });

    // 1. Forward level, from the strike whose call and put mids are closest.
    int    atmIdx  = -1;
    double bestGap = 0.0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const double c = QuoteMid(rows[i].callBid, rows[i].callAsk);
        const double p = QuoteMid(rows[i].putBid,  rows[i].putAsk);
        if (c <= 0.0 || p <= 0.0) continue;
        const double gap = std::fabs(c - p);
        if (atmIdx < 0 || gap < bestGap) { bestGap = gap; atmIdx = (int)i; }
    }
    if (atmIdx < 0) return 0.0;

    const double disc = std::exp(r * T);
    const double cAtm = QuoteMid(rows[(std::size_t)atmIdx].callBid,
                                 rows[(std::size_t)atmIdx].callAsk);
    const double pAtm = QuoteMid(rows[(std::size_t)atmIdx].putBid,
                                 rows[(std::size_t)atmIdx].putAsk);
    const double F = rows[(std::size_t)atmIdx].strike + disc * (cAtm - pAtm);

    // 2. K0 = first strike at or below F.
    int k0 = -1;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].strike <= F) k0 = (int)i;
    if (k0 < 0) return 0.0;

    // 3. Contributing strikes: OTM puts below K0, OTM calls above, both at K0.
    //    Walk outward from K0 and stop after two consecutive zero-bid strikes.
    std::vector<char> use(rows.size(), 0);
    use[(std::size_t)k0] = 1;

    int zeros = 0;
    for (int i = k0 - 1; i >= 0; --i) {          // puts, downward
        if (rows[(std::size_t)i].putBid <= 0.0) { if (++zeros >= 2) break; continue; }
        zeros = 0;
        use[(std::size_t)i] = 1;
    }
    zeros = 0;
    for (std::size_t i = (std::size_t)k0 + 1; i < rows.size(); ++i) {  // calls, upward
        if (rows[i].callBid <= 0.0) { if (++zeros >= 2) break; continue; }
        zeros = 0;
        use[i] = 1;
    }

    // 4. Sum the (dK / K^2) * e^(rT) * Q(K) contributions.
    double sum = 0.0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!use[i]) continue;
        const double K = rows[i].strike;
        if (K <= 0.0) continue;

        double q;
        if ((int)i < k0)      q = QuoteMid(rows[i].putBid,  rows[i].putAsk);
        else if ((int)i > k0) q = QuoteMid(rows[i].callBid, rows[i].callAsk);
        else                  q = 0.5 * (QuoteMid(rows[i].callBid, rows[i].callAsk) +
                                         QuoteMid(rows[i].putBid,  rows[i].putAsk));
        if (q <= 0.0) continue;

        // dK is the half-distance to the neighbours; endpoints use the single
        // neighbour they have.
        double dK;
        if (i == 0)                    dK = rows[1].strike - rows[0].strike;
        else if (i + 1 == rows.size()) dK = rows[i].strike - rows[i - 1].strike;
        else                           dK = (rows[i + 1].strike - rows[i - 1].strike) * 0.5;
        if (dK <= 0.0) continue;

        sum += (dK / (K * K)) * disc * q;
    }
    if (sum <= 0.0) return 0.0;

    const double K0    = rows[(std::size_t)k0].strike;
    const double ratio = F / K0 - 1.0;
    const double var   = (2.0 / T) * sum - (1.0 / T) * ratio * ratio;
    if (var <= 0.0) return 0.0;
    return std::sqrt(var);
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

// ── Strategy payoff metrics ──────────────────────────────────────────────────
// The exactly-computable half of the order-entry stats strip: Max Profit,
// Max Loss, extrinsic, net delta and net theta. These are deterministic from
// the legs — no model, no broker rules. POP and P50 need a vol model, and BP
// Effect must come from IB's whatIf preview; none of those belong here.
//
// Payoff at expiry is piecewise linear with breakpoints at the strikes, so the
// extremes are found by evaluating every breakpoint plus the two outer regions.
// The outer slopes also tell us whether the position is unbounded: a naked
// short call has no maximum loss, and no finite number should be displayed for
// one.

struct StrategyLeg {
    double strike = 0.0;
    char   right  = 'C';   // 'C' / 'P'
    int    ratio  = 0;     // >0 long, <0 short
    double price  = 0.0;   // per-share premium for this leg (mid or fill)
    double delta  = 0.0;   // per-share greeks
    double theta  = 0.0;
};

struct StrategyMetrics {
    bool   valid            = false;
    double maxProfit        = 0.0;   // dollars
    double maxLoss          = 0.0;   // dollars, negative
    bool   profitUnbounded  = false;
    bool   lossUnbounded    = false;
    double extrinsic        = 0.0;   // dollars, signed like the cash flow
    double netDelta         = 0.0;   // multiplier-scaled
    double netTheta         = 0.0;
};

// `netPrice` is the order's net premium per share: positive = debit paid,
// negative = credit received. It is passed separately rather than summed from
// the legs because the order fills at its own limit, not at the sum of leg
// mids — the two differ by exactly the edge the trader captured.
inline StrategyMetrics ComputeStrategyMetrics(const std::vector<StrategyLeg>& legs,
                                              double netPrice, double multiplier,
                                              double spot = 0.0) {
    StrategyMetrics m;
    if (legs.empty() || multiplier <= 0.0) return m;

    auto intrinsic = [](const StrategyLeg& l, double S) {
        return (l.right == 'C' || l.right == 'c') ? std::max(S - l.strike, 0.0)
                                                  : std::max(l.strike - S, 0.0);
    };
    auto payoffAt = [&](double S) {
        double v = 0.0;
        for (const auto& l : legs) v += l.ratio * intrinsic(l, S);
        return (v - netPrice) * multiplier;
    };

    // Breakpoints: every strike, plus a probe either side of the outermost.
    std::vector<double> ks;
    ks.reserve(legs.size() + 2);
    for (const auto& l : legs) ks.push_back(l.strike);
    std::sort(ks.begin(), ks.end());
    const double lo = ks.front(), hi = ks.back();
    ks.push_back(std::max(0.0, lo - 1.0));
    ks.push_back(hi + 1.0);

    double best = payoffAt(ks[0]), worst = best;
    for (double k : ks) {
        const double v = payoffAt(k);
        best  = std::max(best,  v);
        worst = std::min(worst, v);
    }

    // Outer slopes, in payoff dollars per point of underlying.
    double slopeUp = 0.0, slopeDn = 0.0;
    for (const auto& l : legs) {
        if (l.right == 'C' || l.right == 'c') slopeUp += l.ratio;   // calls live above
        else                                  slopeDn -= l.ratio;   // puts live below
    }
    // slopeDn is dPayoff/dS below the lowest strike; profit grows downward when
    // it is negative.
    m.profitUnbounded = (slopeUp > 0.0) || (slopeDn < 0.0);
    m.lossUnbounded   = (slopeUp < 0.0) || (slopeDn > 0.0);

    m.maxProfit = best;
    m.maxLoss   = worst;

    for (const auto& l : legs) {
        m.netDelta += l.ratio * l.delta * multiplier;
        m.netTheta += l.ratio * l.theta * multiplier;
        if (spot > 0.0)
            m.extrinsic += l.ratio * (l.price - intrinsic(l, spot)) * multiplier;
    }
    // Extrinsic is reported from the position holder's perspective: a net debit
    // paid for time value shows negative, matching the platform's sign.
    m.extrinsic = -m.extrinsic;

    m.valid = true;
    return m;
}

}  // namespace core::services
