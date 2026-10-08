#pragma once

// Post-fill option-strategy classification.
//
// IB delivers only *net positions* — the combo linkage from the original order
// is gone by the time a spread's legs show up in the portfolio. So this is a
// best-effort classifier: OPT legs are bucketed by underlying symbol and the
// bucket is named from its leg signature (leg count + strikes + rights + signs).
// One clean structure per underlying → a named strategy (Vertical / Calendar /
// …). Two unrelated structures on the same underlying → "Custom (N legs)"
// rather than a mis-pairing. Non-option positions each stay a Single group so
// the portfolio table can render options and stock through one code path.
//
// Pure logic — no IB API / ImGui deps — unit-tested under the [strategy] tag.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../models/PortfolioData.h"
#include "OptionChain.h"   // StrategyLeg, ExpiryDte (position analysis)

namespace core::services {

enum class StrategyKind {
    Single,         // one option leg, or any non-option position
    Vertical,       // 2 legs, same expiry+right, opposite sign, different strike
    Calendar,       // 2 legs, same right+strike, different expiry
    Diagonal,       // 2 legs, same right, different expiry AND strike
    Straddle,       // 2 legs, same expiry+strike, different right, SAME sign
    Strangle,       // 2 legs, same expiry, different right AND strike, SAME sign
    Synthetic,      // 2 legs, same expiry+strike, different right, OPPOSITE sign
    RiskReversal,   // 2 legs, same expiry, different right AND strike, OPPOSITE sign
    IronCondor,     // 4 legs: 2 calls + 2 puts, same expiry, short body / long wings
    Condor,         // 4 legs, all same right
    Butterfly,      // 3 legs, 1:-2:1 same right, evenly spaced
    IronButterfly,  // 4 legs: short straddle body + long strangle wings
    Ratio,          // 2 legs, same expiry+right, opposite sign, unequal quantity
    Custom          // anything else (incl. multiple structures on one underlying)
};

inline const char* StrategyKindLabel(StrategyKind k) {
    switch (k) {
        case StrategyKind::Single:        return "Single";
        case StrategyKind::Vertical:      return "Vertical";
        case StrategyKind::Calendar:      return "Calendar";
        case StrategyKind::Diagonal:      return "Diagonal";
        case StrategyKind::Straddle:      return "Straddle";
        case StrategyKind::Strangle:      return "Strangle";
        case StrategyKind::Synthetic:     return "Synthetic";
        case StrategyKind::RiskReversal:  return "Risk Reversal";
        case StrategyKind::IronCondor:    return "Iron Condor";
        case StrategyKind::Condor:        return "Condor";
        case StrategyKind::Butterfly:     return "Butterfly";
        case StrategyKind::IronButterfly: return "Iron Butterfly";
        case StrategyKind::Ratio:         return "Ratio";
        case StrategyKind::Custom:        return "Custom";
    }
    return "?";
}

// Where a group's identity comes from — drives whether the UI can trust it.
//   Actual   — an unambiguous single position (a lone option leg, or a stock).
//   Inferred — a multi-leg strategy GUESSED from net positions; may be wrong
//              (6 naked legs look identical to 3 spreads), so never present it as
//              fact for risk purposes. Marked in the UI; user can ungroup it.
//   Manual   — the user pinned this grouping (here: a leg the user ungrouped).
enum class GroupSource { Actual, Inferred, Manual };

// One grouped position (a strategy, a lone option, or a stock/future/cash line).
// legIdx holds indices back into the positions vector passed to ClassifyStrategies
// so callers can render the underlying legs without copying them.
struct StrategyGroup {
    StrategyKind      kind = StrategyKind::Single;
    GroupSource       source = GroupSource::Actual;
    std::string       underlying;   // symbol
    std::string       label;        // IBKR-style, e.g. "SPX Sep09 7640/7650 Bear Call"
    bool              isOption = false;
    std::vector<int>  legIdx;

    int    comboQty = 0;   // number of spreads (gcd of |leg qty|); 0 = mixed/non-integer

    // Rollups summed across the legs.
    double costBasis       = 0.0;
    double marketValue     = 0.0;
    double unrealizedPnL   = 0.0;
    double dailyPnL        = 0.0;
    double portfolioWeight = 0.0;
};

// Authoritative combo linkage. An in-app combo order records its exact leg
// contract ids at submit, so the resulting positions can be grouped with
// certainty instead of guessed from net positions. A link is only a PARTITION
// (which conIds belong together); the label still comes from the shape of the
// matched legs. `conIds` may include the underlying stock conId for a stock-leg
// combo (covered call / collar). source is Actual for a submit-recorded link.
struct ComboLink {
    std::vector<long> conIds;
    GroupSource       source = GroupSource::Actual;
};

// ── internal helpers ─────────────────────────────────────────────────────────
namespace detail {

// "20260909" -> "Sep09" (month title-case + day-of-month). Falls back to the
// raw expiry when it is not the expected 8-char YYYYMMDD.
inline std::string ExpiryShort(const std::string& expiry) {
    if (expiry.size() < 8) return expiry;
    static const char* kMon[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    const int mo = (expiry[4] - '0') * 10 + (expiry[5] - '0');
    if (mo < 1 || mo > 12) return expiry;
    return std::string(kMon[mo - 1]) + expiry.substr(6, 2);
}

inline std::string StrikeStr(double strike) {
    char b[24];
    if (strike == std::floor(strike)) std::snprintf(b, sizeof(b), "%.0f", strike);
    else                              std::snprintf(b, sizeof(b), "%g", strike);
    return b;
}

inline bool isCall(const core::Position& p) { return !p.right.empty() && (p.right[0] == 'C' || p.right[0] == 'c'); }
inline bool isLong(const core::Position& p) { return p.quantity > 0.0; }

}  // namespace detail

// Group + classify a full positions list. See file header for the grouping rule.
// `ungroupedConIds` holds contract ids the user pinned flat (see GroupSource):
// each such option leg is emitted as its own Manual single and excluded from the
// heuristic pairing, so an inferred spread the user rejected stays split.
// `links` are authoritative combo partitions recorded when the app submitted a
// combo: any link whose legs are ALL still held (and not ungrouped) groups those
// legs with certainty (source Actual/Manual, no "~"), ahead of the heuristic; a
// link that no longer fully matches is ignored, so it self-heals on close/reject.
inline std::vector<StrategyGroup>
ClassifyStrategies(const std::vector<core::Position>& positions,
                   const std::unordered_set<long>& ungroupedConIds = {},
                   const std::vector<ComboLink>& links = {}) {
    using detail::ExpiryShort;
    using detail::StrikeStr;
    using detail::isCall;

    std::vector<StrategyGroup> out;

    // 0) Resolve authoritative links first and reserve their legs. A link matches
    //    only when every conId is a present, non-flat, non-ungrouped position not
    //    already claimed by an earlier link; otherwise it is skipped and its legs
    //    fall through to the heuristic below (an identical duplicate link finds
    //    its legs already claimed and is a no-op — netted combos group once).
    std::unordered_set<int> claimed;
    std::vector<std::pair<std::vector<int>, GroupSource>> matchedLinks;
    if (!links.empty()) {
        std::unordered_map<long, int> byConId;
        for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
            const core::Position& p = positions[i];
            if (std::abs(p.quantity) < 1e-9 || p.conId == 0) continue;
            byConId[(long)p.conId] = i;
        }
        for (const ComboLink& lk : links) {
            if (lk.conIds.size() < 2) continue;
            std::vector<int> idx; idx.reserve(lk.conIds.size());
            bool ok = true;
            for (long c : lk.conIds) {
                auto it = byConId.find(c);
                if (it == byConId.end() || claimed.count(it->second) ||
                    ungroupedConIds.count(c)) { ok = false; break; }
                idx.push_back(it->second);
            }
            if (!ok) continue;
            for (int i : idx) claimed.insert(i);
            matchedLinks.emplace_back(std::move(idx), lk.source);
        }
    }

    // 1) Non-option positions each become their own Single group; option legs are
    //    bucketed by underlying symbol (insertion order preserved for the pass).
    //    A leg the user ungrouped is emitted flat here and never bucketed.
    std::vector<std::string>            optOrder;   // underlyings, first-seen order
    std::vector<std::vector<int>>       optBuckets; // parallel to optOrder

    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        const core::Position& p = positions[i];
        if (std::abs(p.quantity) < 1e-9) continue;          // flat — skip
        if (claimed.count(i)) continue;                     // owned by an authoritative link
        if (p.assetClass != "OPT") {
            StrategyGroup g;
            g.kind = StrategyKind::Single;
            g.source = GroupSource::Actual;
            g.underlying = p.symbol;
            g.label = p.symbol;
            g.isOption = false;
            g.legIdx = { i };
            out.push_back(std::move(g));
            continue;
        }
        if (p.conId != 0 && ungroupedConIds.count((long)p.conId)) {
            StrategyGroup g;
            g.kind = StrategyKind::Single;
            g.source = GroupSource::Manual;   // user pinned this leg flat
            g.underlying = p.symbol;
            g.isOption = true;
            g.legIdx = { i };
            g.label = core::OptionDisplayLabel(p.symbol, p.expiry, p.strike, p.right);
            g.comboQty = (int)std::llround(std::abs(p.quantity));
            g.costBasis = p.costBasis; g.marketValue = p.marketValue;
            g.unrealizedPnL = p.unrealizedPnL; g.dailyPnL = p.dailyPnL;
            g.portfolioWeight = p.portfolioWeight;
            out.push_back(std::move(g));
            continue;
        }
        auto it = std::find(optOrder.begin(), optOrder.end(), p.symbol);
        if (it == optOrder.end()) { optOrder.push_back(p.symbol); optBuckets.emplace_back().push_back(i); }
        else                      { optBuckets[std::distance(optOrder.begin(), it)].push_back(i); }
    }

    const auto& L = positions;   // shorthand for the leg lookups below

    // Fill comboQty (gcd of integer |leg qty|) + rollups for a finished group.
    auto finalize = [&](StrategyGroup g) -> StrategyGroup {
        long gg = 0; bool integral = true;
        for (int li : g.legIdx) {
            const double q = std::abs(L[li].quantity);
            const long qi = static_cast<long>(std::llround(q));
            if (std::abs(q - qi) > 1e-6 || qi == 0) { integral = false; break; }
            gg = gg == 0 ? qi : std::gcd(gg, qi);
        }
        g.comboQty = integral ? static_cast<int>(gg) : 0;
        for (int li : g.legIdx) {
            g.costBasis       += L[li].costBasis;
            g.marketValue     += L[li].marketValue;
            g.unrealizedPnL   += L[li].unrealizedPnL;
            g.dailyPnL        += L[li].dailyPnL;
            g.portfolioWeight += L[li].portfolioWeight;
        }
        return g;
    };

    // One option leg on its own.
    auto singleGroup = [&](int i) -> StrategyGroup {
        const core::Position& p = L[i];
        StrategyGroup g; g.underlying = p.symbol; g.isOption = true;
        g.kind = StrategyKind::Single; g.legIdx = { i };
        g.label = core::OptionDisplayLabel(p.symbol, p.expiry, p.strike, p.right);
        return finalize(g);
    };

    // A 2-leg vertical/ratio given its lower- and higher-strike leg indices
    // (same expiry + right, opposite sign). Direction is read off the lower leg:
    //   calls: lower long -> Bull Call, lower short -> Bear Call
    //   puts : lower long -> Bull Put , lower short -> Bear Put
    auto verticalGroup = [&](int lo, int hi) -> StrategyGroup {
        const core::Position& a = L[lo];
        const core::Position& c = L[hi];
        const bool ratio = std::abs(std::abs(a.quantity) - std::abs(c.quantity)) > 1e-9;
        const char* dir = a.quantity > 0 ? "Bull" : "Bear";
        const char* rt  = isCall(a) ? "Call" : "Put";
        StrategyGroup g; g.underlying = a.symbol; g.isOption = true;
        g.kind   = ratio ? StrategyKind::Ratio : StrategyKind::Vertical;
        g.legIdx = { lo, hi };
        g.label  = a.symbol + " " + ExpiryShort(a.expiry) + " " + StrikeStr(a.strike) + "/"
                 + StrikeStr(c.strike) + " " + dir + " " + rt + (ratio ? " Ratio" : "");
        return finalize(g);
    };

    // The 2-leg classifier (vertical/ratio/calendar/diagonal/straddle/strangle),
    // or Custom "N legs" for a 2-leg combo that matches nothing.
    auto twoLegGroup = [&](int i0, int i1) -> StrategyGroup {
        const core::Position& a = L[i0];   // lower strike (idx pre-sorted)
        const core::Position& c = L[i1];
        const bool sameExp = a.expiry == c.expiry;
        const bool sameRt  = isCall(a) == isCall(c);
        const bool sameStk = a.strike == c.strike;
        const bool opp     = (a.quantity > 0) != (c.quantity > 0);
        const std::string ex = ExpiryShort(a.expiry);
        if (sameRt && sameExp && !sameStk && opp) return verticalGroup(i0, i1);

        StrategyGroup g; g.underlying = a.symbol; g.isOption = true; g.legIdx = { i0, i1 };
        g.kind = StrategyKind::Custom;
        // Signs matter: a calendar/diagonal is one long + one short; a straddle/
        // strangle is two longs or two shorts. A call and a put of opposite sign
        // is a synthetic (same strike) or risk reversal (different strikes) — a
        // directional position, not a volatility one, so never call it a
        // straddle/strangle.
        const core::Position& callLeg = isCall(a) ? a : c;   // valid when !sameRt
        if (sameRt && !sameExp && sameStk && opp) {
            g.kind = StrategyKind::Calendar;
            g.label = a.symbol + " " + StrikeStr(a.strike) + (isCall(a) ? "C" : "P")
                    + " Calendar (" + ExpiryShort(a.expiry) + "/" + ExpiryShort(c.expiry) + ")";
        } else if (sameRt && !sameExp && !sameStk && opp) {
            g.kind = StrategyKind::Diagonal;
            g.label = a.symbol + " " + StrikeStr(a.strike) + "/" + StrikeStr(c.strike)
                    + (isCall(a) ? "C" : "P") + " Diagonal ("
                    + ExpiryShort(a.expiry) + "/" + ExpiryShort(c.expiry) + ")";
        } else if (!sameRt && sameExp && sameStk && !opp) {
            g.kind = StrategyKind::Straddle;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike) + " Straddle";
        } else if (!sameRt && sameExp && !sameStk && !opp) {
            g.kind = StrategyKind::Strangle;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike) + "/" + StrikeStr(c.strike)
                    + " Strangle";
        } else if (!sameRt && sameExp && sameStk && opp) {
            // Long call + short put = synthetic long; the mirror is synthetic short.
            g.kind = StrategyKind::Synthetic;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike)
                    + (callLeg.quantity > 0 ? " Synthetic Long" : " Synthetic Short");
        } else if (!sameRt && sameExp && !sameStk && opp) {
            // Long call + short put = bullish; long put + short call = bearish.
            g.kind = StrategyKind::RiskReversal;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike) + "/" + StrikeStr(c.strike)
                    + (callLeg.quantity > 0 ? " Bullish Risk Reversal" : " Bearish Risk Reversal");
        } else {
            g.label = a.symbol + " 2 legs";
        }
        return finalize(g);
    };

    // Named 3/4-leg patterns we DON'T want decomposed into plain verticals:
    // butterfly, iron condor, iron butterfly, condor. nullopt otherwise.
    auto tryNamedMulti = [&](const std::vector<int>& idx) -> std::optional<StrategyGroup> {
        const int n = static_cast<int>(idx.size());
        auto sameExp = [&]{ for (int k = 1; k < n; ++k) if (L[idx[k]].expiry != L[idx[0]].expiry) return false; return true; };
        auto sameRt  = [&]{ for (int k = 1; k < n; ++k) if (isCall(L[idx[k]]) != isCall(L[idx[0]])) return false; return true; };
        const std::string sym = L[idx[0]].symbol;
        const std::string ex  = ExpiryShort(L[idx[0]].expiry);

        if (n == 3 && sameExp() && sameRt()) {
            const double k0 = L[idx[0]].strike, k1 = L[idx[1]].strike, k2 = L[idx[2]].strike;
            const double q0 = L[idx[0]].quantity, q1 = L[idx[1]].quantity, q2 = L[idx[2]].quantity;
            const bool fly = std::abs((k1 - k0) - (k2 - k1)) < 1e-6
                          && std::abs(q0 - q2) < 1e-9 && std::abs(q1 + 2.0 * q0) < 1e-9;
            if (fly) {
                StrategyGroup g; g.underlying = sym; g.isOption = true; g.legIdx = idx;
                g.kind = StrategyKind::Butterfly;
                g.label = sym + " " + ex + " " + StrikeStr(k0) + "/" + StrikeStr(k1) + "/"
                        + StrikeStr(k2) + (isCall(L[idx[0]]) ? " Call" : " Put") + " Butterfly";
                return g;
            }
        }
        if (n == 4 && sameExp()) {
            int calls = 0, puts = 0;
            for (int k = 0; k < 4; ++k) (isCall(L[idx[k]]) ? calls : puts)++;
            if (calls == 2 && puts == 2) {
                // A real iron condor / butterfly is a put vertical below a call
                // vertical, equal size, with the short legs as the body:
                //   long pLo, short pHi, short cLo, long cHi, and pHi <= cLo.
                // The all-flipped form (long body) is the reverse. Anything else
                // with 2 calls + 2 puts — a box spread, two straddles, two
                // strangles — is NOT an iron structure; return nullopt so the
                // heuristic decomposes it (or a link names it Custom).
                std::vector<int> P, C;
                for (int k = 0; k < 4; ++k) (isCall(L[idx[k]]) ? C : P).push_back(idx[k]);
                auto byStrike = [&](int a, int b) { return L[a].strike < L[b].strike; };
                std::sort(P.begin(), P.end(), byStrike);
                std::sort(C.begin(), C.end(), byStrike);
                const core::Position& pLo = L[P[0]]; const core::Position& pHi = L[P[1]];
                const core::Position& cLo = L[C[0]]; const core::Position& cHi = L[C[1]];
                const double q = std::abs(pLo.quantity);
                const bool equalQty = q > 0.0
                    && std::abs(std::abs(pHi.quantity) - q) < 1e-9
                    && std::abs(std::abs(cLo.quantity) - q) < 1e-9
                    && std::abs(std::abs(cHi.quantity) - q) < 1e-9;
                const bool distinct  = pLo.strike < pHi.strike && cLo.strike < cHi.strike;
                const bool ordered   = pHi.strike <= cLo.strike;
                const bool shortBody = pLo.quantity > 0 && pHi.quantity < 0
                                    && cLo.quantity < 0 && cHi.quantity > 0;
                const bool longBody  = pLo.quantity < 0 && pHi.quantity > 0
                                    && cLo.quantity > 0 && cHi.quantity < 0;
                if (equalQty && distinct && ordered && (shortBody || longBody)) {
                    StrategyGroup g; g.underlying = sym; g.isOption = true; g.legIdx = idx;
                    g.kind  = (pHi.strike == cLo.strike) ? StrategyKind::IronButterfly
                                                         : StrategyKind::IronCondor;
                    // Strikes low to high; an iron butterfly's shared body once.
                    std::string ks = StrikeStr(pLo.strike) + "/" + StrikeStr(pHi.strike);
                    if (cLo.strike != pHi.strike) ks += "/" + StrikeStr(cLo.strike);
                    ks += "/" + StrikeStr(cHi.strike);
                    g.label = sym + " " + ex + " " + ks + " " + (longBody ? "Reverse " : "")
                            + StrategyKindLabel(g.kind);
                    return g;
                }
                return std::nullopt;
            }
            if (sameRt()) {
                // Real condor: strikes ascending with long wings / short body
                // (+,-,-,+) or short wings / long body (-,+,+,-).
                std::vector<int> s = idx;
                std::sort(s.begin(), s.end(), [&](int a, int b){ return L[a].strike < L[b].strike; });
                const double q0 = L[s[0]].quantity, q1 = L[s[1]].quantity,
                             q2 = L[s[2]].quantity, q3 = L[s[3]].quantity;
                if ((q0 > 0 && q1 < 0 && q2 < 0 && q3 > 0) ||
                    (q0 < 0 && q1 > 0 && q2 > 0 && q3 < 0)) {
                    StrategyGroup g; g.underlying = sym; g.isOption = true; g.legIdx = idx;
                    g.kind = StrategyKind::Condor;
                    g.label = sym + " " + ex + " " + StrikeStr(L[s[0]].strike) + "/"
                            + StrikeStr(L[s[1]].strike) + "/" + StrikeStr(L[s[2]].strike) + "/"
                            + StrikeStr(L[s[3]].strike)
                            + (isCall(L[s[0]]) ? " Call" : " Put") + " Condor";
                    return g;
                }
            }
        }
        return std::nullopt;
    };

    // Decompose a >2-leg bucket into verticals + leftover singles by pairing, per
    // (expiry,right), the ranked long and short strikes. Returns {} when a
    // partition can't be cleanly paired (unequal long/short counts or paired
    // quantities) — the caller then keeps the whole bucket as one Custom group.
    // This is what turns "3 short verticals on one underlying" into three Bull
    // Put rows instead of a single "6 legs" blob.
    auto decompose = [&](const std::vector<int>& idx) -> std::vector<StrategyGroup> {
        std::vector<std::pair<std::string,bool>> keys;
        for (int li : idx) {
            std::pair<std::string,bool> k{ L[li].expiry, isCall(L[li]) };
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        }
        std::vector<StrategyGroup> res;
        std::vector<int> loose;   // one-sided legs; paired into calendars below
        for (const auto& k : keys) {
            std::vector<int> longs, shorts;
            for (int li : idx)
                if (L[li].expiry == k.first && isCall(L[li]) == k.second)
                    (L[li].quantity > 0 ? longs : shorts).push_back(li);
            auto byStrike = [&](int a, int b){ return L[a].strike < L[b].strike; };
            std::sort(longs.begin(), longs.end(), byStrike);
            std::sort(shorts.begin(), shorts.end(), byStrike);

            if (longs.empty() || shorts.empty()) {
                // One-sided partition (e.g. several long calls) -> separate legs,
                // unless a leg on another expiry completes a calendar (below).
                for (int li : longs)  loose.push_back(li);
                for (int li : shorts) loose.push_back(li);
                continue;
            }
            if (longs.size() != shorts.size()) return {};   // not cleanly pairable
            for (std::size_t m = 0; m < longs.size(); ++m) {
                const int a = longs[m], c = shorts[m];
                if (L[a].strike == L[c].strike) return {};   // same strike -> not a vertical
                if (std::abs(std::abs(L[a].quantity) - std::abs(L[c].quantity)) > 1e-9) return {};
                const int lo = L[a].strike < L[c].strike ? a : c;
                const int hi = lo == a ? c : a;
                res.push_back(verticalGroup(lo, hi));
            }
        }
        // A calendar's two legs sit in different (expiry,right) partitions, so
        // pair loose legs: same right + strike, one long + one short, equal size,
        // different expiry. Whatever doesn't pair stays a single.
        std::vector<bool> used(loose.size(), false);
        for (std::size_t i = 0; i < loose.size(); ++i) {
            if (used[i]) continue;
            const core::Position& a = L[loose[i]];
            for (std::size_t j = i + 1; j < loose.size(); ++j) {
                if (used[j]) continue;
                const core::Position& c = L[loose[j]];
                if (isCall(a) == isCall(c) && a.strike == c.strike && a.expiry != c.expiry &&
                    (a.quantity > 0) != (c.quantity > 0) &&
                    std::abs(std::abs(a.quantity) - std::abs(c.quantity)) < 1e-9) {
                    const bool aFirst = a.expiry < c.expiry;
                    res.push_back(twoLegGroup(aFirst ? loose[i] : loose[j],
                                              aFirst ? loose[j] : loose[i]));
                    used[i] = used[j] = true;
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < loose.size(); ++i)
            if (!used[i]) res.push_back(singleGroup(loose[i]));
        return res;
    };

    // Generic namer for a link that includes the underlying stock leg (covered
    // call / married put / collar / conversion / reversal). The shape namers
    // above are option-only, so name from the leg counts and fall back to
    // "Combo (N legs)". A put + call at the SAME strike and expiry around the
    // stock is a conversion (long stock) / reversal (short stock), not a collar.
    auto stockComboGroup = [&](std::vector<int> idx, GroupSource src) -> StrategyGroup {
        StrategyGroup g; g.isOption = true; g.legIdx = idx; g.kind = StrategyKind::Custom;
        g.source = src;
        int stkLong = 0, stkShort = 0, cLong = 0, cShort = 0, pLong = 0, pShort = 0;
        std::string sym;
        const core::Position* put  = nullptr;
        const core::Position* call = nullptr;
        for (int i : idx) {
            const core::Position& p = L[i];
            if (sym.empty()) sym = p.symbol;
            if (p.assetClass == "OPT") (isCall(p) ? call : put) = &p;
            if (p.assetClass != "OPT")   (p.quantity > 0 ? stkLong : stkShort)++;
            else if (isCall(p))          (p.quantity > 0 ? cLong   : cShort)++;
            else                         (p.quantity > 0 ? pLong   : pShort)++;
        }
        g.underlying = sym;
        const int nOpt = cLong + cShort + pLong + pShort;
        std::string name;
        if      (stkLong == 1 && stkShort == 0 && nOpt == 1 && cShort == 1) name = "Covered Call";
        else if (stkLong == 1 && stkShort == 0 && nOpt == 1 && pLong  == 1) name = "Married Put";
        else if (stkLong == 1 && stkShort == 0 && nOpt == 2 && cShort == 1 && pLong == 1) {
            // Collar: put strike / call strike, like a vertical's "200/210".
            if (put->strike == call->strike && put->expiry == call->expiry)
                name = ExpiryShort(put->expiry) + " " + StrikeStr(put->strike) + " Conversion";
            else if (put->expiry == call->expiry)
                name = ExpiryShort(put->expiry) + " " + StrikeStr(put->strike) + "/" +
                       StrikeStr(call->strike) + " Collar";
            else
                name = ExpiryShort(put->expiry) + " " + StrikeStr(put->strike) + "P / " +
                       ExpiryShort(call->expiry) + " " + StrikeStr(call->strike) + "C Collar";
        }
        else if (stkShort == 1 && stkLong == 0 && nOpt == 2 && cLong == 1 && pShort == 1 &&
                 put->strike == call->strike && put->expiry == call->expiry)
            name = ExpiryShort(put->expiry) + " " + StrikeStr(put->strike) + " Reversal";
        else name = "Combo (" + std::to_string((int)idx.size()) + " legs)";
        g.label = sym + " " + name;
        return finalize(std::move(g));
    };

    // Build one group for a matched authoritative link — an explicit partition, so
    // it is never decomposed. Option-only links reuse the shape namers; a link
    // that includes stock goes through stockComboGroup. Source is the link's.
    auto linkGroup = [&](std::vector<int> idx, GroupSource src) -> StrategyGroup {
        for (int i : idx)
            if (L[i].assetClass != "OPT") return stockComboGroup(std::move(idx), src);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            const core::Position& pa = L[a]; const core::Position& pb = L[b];
            if (pa.expiry != pb.expiry) return pa.expiry < pb.expiry;
            if (pa.strike != pb.strike) return pa.strike < pb.strike;
            return pa.right < pb.right;
        });
        const int n = static_cast<int>(idx.size());
        StrategyGroup g;
        if (n == 1)      g = singleGroup(idx[0]);
        else if (n == 2) g = twoLegGroup(idx[0], idx[1]);
        else if (auto named = tryNamedMulti(idx)) g = finalize(*named);
        else {
            g.underlying = L[idx[0]].symbol; g.isOption = true; g.legIdx = idx;
            g.kind = StrategyKind::Custom;
            g.label = g.underlying + " " + std::to_string(n) + " legs";
            g = finalize(std::move(g));
        }
        g.source = src;
        return g;
    };
    for (auto& ml : matchedLinks)
        out.push_back(linkGroup(std::move(ml.first), ml.second));

    // 2) Classify each option bucket.
    for (std::size_t b = 0; b < optOrder.size(); ++b) {
        std::vector<int> idx = optBuckets[b];
        // Deterministic order: expiry, then strike, then right (C before P).
        std::sort(idx.begin(), idx.end(), [&](int a, int c) {
            const core::Position& pa = positions[a];
            const core::Position& pc = positions[c];
            if (pa.expiry != pc.expiry) return pa.expiry < pc.expiry;
            if (pa.strike != pc.strike) return pa.strike < pc.strike;
            return pa.right < pc.right;
        });

        const int n = static_cast<int>(idx.size());

        // A lone naked leg is unambiguous (Actual); any grouping the heuristic
        // forms from net positions is a guess (Inferred).
        if (n == 1) { auto g = singleGroup(idx[0]); g.source = GroupSource::Actual; out.push_back(std::move(g)); continue; }
        if (n == 2) { auto g = twoLegGroup(idx[0], idx[1]); g.source = GroupSource::Inferred; out.push_back(std::move(g)); continue; }

        // n >= 3: named multi-leg patterns first, then vertical decomposition,
        // then Custom for anything that decomposition can't cleanly split.
        if (auto named = tryNamedMulti(idx)) {
            auto g = finalize(*named); g.source = GroupSource::Inferred; out.push_back(std::move(g)); continue;
        }
        auto parts = decompose(idx);
        if (!parts.empty()) {
            for (auto& g : parts) {
                g.source = g.legIdx.size() >= 2 ? GroupSource::Inferred : GroupSource::Actual;
                out.push_back(std::move(g));
            }
            continue;
        }

        StrategyGroup g;
        g.underlying = optOrder[b]; g.isOption = true; g.legIdx = idx;
        g.kind = StrategyKind::Custom; g.source = GroupSource::Inferred;
        g.label = g.underlying + " " + std::to_string(n) + " legs";
        out.push_back(finalize(std::move(g)));
    }

    return out;
}

// A stable, unique identity for a group, independent of its label — two groups
// can share a label (e.g. two same-expiry "Iron Condor"s, or two "N legs"), and
// a UI keyed on the label would conflate them. Built from the legs' contract
// ids, sorted so leg order doesn't matter: "101_102". A leg with an unknown
// conId (0) falls back to its position index ("i3") so the key is never empty.
inline std::string StrategyGroupKey(const StrategyGroup& g,
                                    const std::vector<core::Position>& positions) {
    std::vector<std::string> parts;
    parts.reserve(g.legIdx.size());
    for (int li : g.legIdx) {
        const bool known = li >= 0 && li < static_cast<int>(positions.size())
                        && positions[li].conId != 0;
        parts.push_back(known ? std::to_string(positions[li].conId)
                              : "i" + std::to_string(li));
    }
    std::sort(parts.begin(), parts.end());
    std::string key;
    for (const auto& p : parts) { if (!key.empty()) key += '_'; key += p; }
    return key;
}

// ── conId-set persistence (PORT_UNGROUP / PORT_LINK) ─────────────────────────
// Format is "c-c|c-c-c": '|' separates sets, '-' separates conIds within a set.
// Sets with fewer than 2 conIds are dropped on parse.
inline std::vector<std::vector<long>> ParseConIdSets(const std::string& s) {
    std::vector<std::vector<long>> out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t bar = s.find('|', i);
        std::string setStr = s.substr(i, bar == std::string::npos ? std::string::npos : bar - i);
        std::vector<long> set;
        std::size_t j = 0;
        while (j < setStr.size()) {
            std::size_t dash = setStr.find('-', j);
            std::string tok = setStr.substr(j, dash == std::string::npos ? std::string::npos : dash - j);
            if (!tok.empty()) { try { set.push_back(std::stol(tok)); } catch (...) {} }
            if (dash == std::string::npos) break;
            j = dash + 1;
        }
        if (set.size() >= 2) out.push_back(std::move(set));
        if (bar == std::string::npos) break;
        i = bar + 1;
    }
    return out;
}

// Format the sets. With `prune`, conIds that are no longer a live (non-flat)
// position are dropped so expired / closed legs can't accumulate, and a set left
// with <2 legs is omitted. Only prune once the positions snapshot is COMPLETE:
// pruning against an empty or partial list (before IB has delivered positions,
// or right after an account switch) would silently wipe every set from disk.
//
// `keep` holds conIds that must survive a prune even though they aren't held —
// the legs of a combo order still WORKING. Its link is recorded at submit, long
// before the fill; pruning it then lost the link for a combo that filled later
// (e.g. while the app was closed), so the position wasn't named.
inline std::string FormatConIdSets(const std::vector<std::vector<long>>& sets,
                                   const std::vector<core::Position>& positions,
                                   bool prune,
                                   const std::unordered_set<long>& keep = {}) {
    auto live = [&](long c) {
        if (keep.count(c)) return true;
        for (const auto& p : positions)
            if ((long)p.conId == c && std::abs(p.quantity) > 1e-9) return true;
        return false;
    };
    std::string all;
    for (const auto& set : sets) {
        std::string one; int n = 0;
        for (long c : set) {
            if (prune && !live(c)) continue;
            if (!one.empty()) one += "-";
            one += std::to_string(c); ++n;
        }
        if (n < 2) continue;
        if (!all.empty()) all += "|";
        all += one;
    }
    return all;
}

// Record a manual merge: the user asserts `set` (leg conIds) is one strategy.
// Earlier merges sharing any leg are dropped (a leg belongs to one merge), and
// the legs are removed from the ungrouped (pinned-flat) sets, since the new
// intent wins and a pinned leg would otherwise block the merge from matching.
// Ungrouped sets left with fewer than 2 legs are dropped (they can't persist).
// Returns false (nothing changed) for fewer than 2 distinct legs.
inline bool ApplyManualMerge(std::vector<std::vector<long>>& merges,
                             std::vector<std::vector<long>>& ungrouped,
                             std::vector<long> set) {
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
    set.erase(std::remove(set.begin(), set.end(), 0L), set.end());
    if (set.size() < 2) return false;
    auto shares = [&](const std::vector<long>& s) {
        for (long c : s) if (std::binary_search(set.begin(), set.end(), c)) return true;
        return false;
    };
    merges.erase(std::remove_if(merges.begin(), merges.end(), shares), merges.end());
    for (auto& u : ungrouped)
        u.erase(std::remove_if(u.begin(), u.end(), [&](long c) {
                    return std::binary_search(set.begin(), set.end(), c); }),
                u.end());
    ungrouped.erase(std::remove_if(ungrouped.begin(), ungrouped.end(),
                        [](const std::vector<long>& u) { return u.size() < 2; }),
                    ungrouped.end());
    merges.push_back(std::move(set));
    return true;
}

// Index of the merge whose legs are exactly `conIds` (any order), else -1.
inline int FindManualMerge(const std::vector<std::vector<long>>& merges,
                           std::vector<long> conIds) {
    std::sort(conIds.begin(), conIds.end());
    for (int i = 0; i < (int)merges.size(); ++i) {
        std::vector<long> m = merges[(std::size_t)i];
        std::sort(m.begin(), m.end());
        if (m == conIds) return i;
    }
    return -1;
}

// One leg of a combo ORDER, resolved to its contract identity, for labelling the
// order in a blotter. `buy` is the leg's effective direction (the order side
// already applied: a SELL of a BAG flips every leg's action).
struct ComboLegInfo {
    long        conId  = 0;
    bool        buy    = true;
    int         ratio  = 1;
    bool        stock  = false;   // equity leg (ratio = shares)
    std::string expiry;           // YYYYMMDD (options)
    double      strike = 0.0;
    std::string right;            // "C" / "P"
};

// Strategy label for a combo order ("SPY 600C Calendar (Oct16/Nov20)", "SPY
// Oct16 600/605 Bull Call", ...), named by the same shape logic the portfolio
// uses for held positions: the legs become synthetic positions (qty = ±ratio)
// linked as one partition. Empty when there are no legs or a leg is unresolved,
// so the caller can fall back to a generic label.
inline std::string ComboStrategyLabel(const std::string& symbol,
                                      const std::vector<ComboLegInfo>& legs) {
    if (legs.empty()) return {};
    std::vector<core::Position> pos;
    ComboLink link;
    for (const auto& L : legs) {
        if (L.conId == 0 || (!L.stock && (L.expiry.empty() || L.right.empty())))
            return {};
        core::Position p;
        p.symbol     = symbol;
        p.conId      = L.conId;
        p.assetClass = L.stock ? "STK" : "OPT";
        p.quantity   = (L.buy ? 1.0 : -1.0) * std::max(1, L.ratio);
        if (!L.stock) {
            p.expiry = L.expiry; p.strike = L.strike; p.right = L.right;
            p.multiplier = "100";
        }
        pos.push_back(p);
        link.conIds.push_back(L.conId);
    }
    const auto groups = ClassifyStrategies(pos, {}, {link});
    for (const auto& g : groups)
        if (g.legIdx.size() == legs.size()) return g.label;
    return {};
}

// Analysis inputs for HELD legs (a Portfolio strategy group or single leg), in
// the same conventions the order ticket feeds StrategyAnalysisWindow:
//   legs[i].ratio  signed contracts (options) / shares (stock)
//   netPrice       signed per-share net actually paid: debit + / credit -,
//                  from the positions' cost basis, so the curves measure P/L
//                  against the real entry rather than today's mid
//   qty            combo count (gcd of |option qty|) for the per-contract view
// IB delivers no IV or greeks for positions, so each option leg's IV is backed
// out of its mark (ImpliedVolFromPrice) and delta/theta come from Black-Scholes.
// Without a spot both stay 0 and only the expiry payoff is meaningful.
struct PositionAnalysis {
    bool                      valid       = false;
    std::vector<StrategyLeg>  legs;
    std::vector<double>       strikes;      // sorted, unique
    double                    netPrice    = 0.0;
    double                    multiplier  = 100.0;
    int                       qty         = 1;
    bool                      multiExpiry = false;
};

inline PositionAnalysis BuildPositionAnalysis(const std::vector<core::Position>& held,
                                              double spot, int y, int m, int d,
                                              double r = kAssumedRiskFreeRate) {
    PositionAnalysis out;
    double mult = 0.0;
    std::string firstExpiry;
    for (const auto& p : held) {
        if (p.quantity == 0.0) return out;
        if (p.assetClass == "OPT") {
            if (mult <= 0.0) mult = p.multiplier.empty() ? 100.0 : std::atof(p.multiplier.c_str());
            if (firstExpiry.empty()) firstExpiry = p.expiry;
            else if (p.expiry != firstExpiry) out.multiExpiry = true;
        } else if (p.assetClass != "STK") {
            return out;                      // futures / cash: not modelled
        }
    }
    if (mult <= 0.0) return out;             // needs at least one option leg
    out.multiplier = mult;

    long g = 0;
    for (const auto& p : held) {
        // costBasis = qty x avgCost (signed; IB's option avgCost already
        // includes the multiplier), so / mult is the per-share net.
        const double cost = p.costBasis != 0.0 ? p.costBasis : p.quantity * p.avgCost;
        out.netPrice += cost / mult;

        StrategyLeg L;
        L.ratio = (int)std::llround(p.quantity);
        if (p.assetClass == "STK") {
            L.stock = true;
            L.price = p.marketPrice;
            out.legs.push_back(L);
            continue;
        }
        const long aq = std::labs((long)L.ratio);
        g = (g == 0) ? aq : std::gcd(g, aq);
        L.strike = p.strike;
        L.right  = p.right.empty() ? 'C' : (char)std::toupper((unsigned char)p.right[0]);
        // Per-share mark; derive it from market value when the mark is missing.
        L.price = p.marketPrice > 0.0 ? p.marketPrice
                : (p.quantity != 0.0 ? std::fabs(p.marketValue / (p.quantity * mult)) : 0.0);
        L.dte = (double)std::max(0, ExpiryDte(p.expiry, y, m, d));
        if (spot > 0.0 && L.dte > 0.0) {
            const double t = L.dte / 365.0;
            L.iv = ImpliedVolFromPrice(L.right, L.price, spot, L.strike, t, r);
            const BsGreeks gr = BlackScholesGreeks(L.right, spot, L.strike, t, r, L.iv);
            L.delta = gr.delta;
            L.theta = gr.theta;
        }
        out.legs.push_back(L);
        out.strikes.push_back(L.strike);
    }
    std::sort(out.strikes.begin(), out.strikes.end());
    out.strikes.erase(std::unique(out.strikes.begin(), out.strikes.end()), out.strikes.end());
    out.qty   = g > 0 ? (int)g : 1;
    out.valid = true;
    return out;
}

// ── Roll (Portfolio -> Options Chain cart) ───────────────────────────────────
// A roll closes the held legs and reopens the same legs one expiry further out,
// as one combo. Each leg rolls to the first listed expiry after its own, so a
// calendar keeps its shape. Strikes are kept; the user adjusts them in the cart.
struct RollLeg {
    OptionContractKey key;
    bool buy     = true;
    int  ratio   = 1;
    bool closing = false;   // true = closes a held leg, false = the new leg
};
struct RollPlan {
    bool        ok = false;
    std::string error;          // why not, when !ok
    int         qty = 0;        // combos to send (gcd of |leg qty|)
    std::string toExpiry;       // earliest new expiry (the tab to show)
    std::vector<RollLeg> legs;  // closing legs first, then the new legs
};

// `held` = the legs to roll; `expirations` = the chain's expiries (YYYYMMDD,
// any order). A roll doubles the leg count, so at most maxLegs/2 legs.
inline RollPlan BuildRollPlan(const std::vector<core::Position>& held,
                              std::vector<std::string> expirations,
                              int maxLegs = 6) {
    RollPlan r;
    if (held.empty()) { r.error = "Nothing to roll."; return r; }
    if ((int)held.size() * 2 > maxLegs) {
        r.error = "Roll supports up to " + std::to_string(maxLegs / 2) +
                  " legs (" + std::to_string(maxLegs) + " legs per combo).";
        return r;
    }
    std::sort(expirations.begin(), expirations.end());
    long g = 0;
    for (const auto& p : held) {
        if (p.assetClass != "OPT" || p.right.empty() || p.expiry.empty()) {
            r.error = "Only option legs can be rolled."; return r;
        }
        if (p.symbol != held.front().symbol) {
            r.error = "Legs are on different underlyings."; return r;
        }
        const double aq = std::fabs(p.quantity);
        const long   q  = std::lround(aq);
        if (q <= 0 || std::fabs(aq - (double)q) > 1e-6) {
            r.error = "Leg quantity is not a whole number of contracts."; return r;
        }
        g = (g == 0) ? q : std::gcd(g, q);
    }
    std::vector<RollLeg> opens;
    for (const auto& p : held) {
        const auto nx = std::upper_bound(expirations.begin(), expirations.end(), p.expiry);
        if (nx == expirations.end()) {
            r.error = "No later expiry listed for " + p.expiry + "."; return r;
        }
        RollLeg c;
        c.key = OptionContractKey{ p.symbol, p.expiry, p.strike,
                                   static_cast<char>(std::toupper((unsigned char)p.right[0])) };
        c.ratio   = (int)(std::lround(std::fabs(p.quantity)) / g);
        c.buy     = p.quantity < 0.0;     // buy back a short, sell out a long
        c.closing = true;
        r.legs.push_back(c);
        RollLeg o = c;
        o.key.expiry = *nx;
        o.buy        = !c.buy;            // reopen the same side
        o.closing    = false;
        opens.push_back(o);
        if (r.toExpiry.empty() || *nx < r.toExpiry) r.toExpiry = *nx;
    }
    r.legs.insert(r.legs.end(), opens.begin(), opens.end());
    r.qty = (int)g;
    r.ok  = true;
    return r;
}

// Leg conIds of a combo that open or add to a position — legs that reduce a
// held position are left out. Recording a roll's closing legs in the combo
// link would stop the link from ever matching (those legs go flat on fill).
// `legs` = (conId, effective buy); `heldQty` = signed held qty by conId.
inline std::vector<long> OpeningComboLegs(const std::vector<std::pair<long, bool>>& legs,
                                          const std::unordered_map<long, double>& heldQty) {
    std::vector<long> out;
    for (const auto& [id, buy] : legs) {
        if (id == 0) continue;
        auto it = heldQty.find(id);
        const double h = it == heldQty.end() ? 0.0 : it->second;
        if ((h > 1e-9 && !buy) || (h < -1e-9 && buy)) continue;   // closes
        out.push_back(id);
    }
    return out;
}

// A position's average cost in the units its price is quoted in. IB reports an
// option's avgCost per contract (premium x multiplier, 621.00 for a 6.21
// premium), while its price is per share; stocks are already per share.
inline double AvgCostPerUnit(const core::Position& p) {
    if (p.assetClass != "OPT") return p.avgCost;
    const double mult = p.multiplier.empty() ? 100.0 : std::atof(p.multiplier.c_str());
    return mult > 0.0 ? p.avgCost / mult : p.avgCost;
}

// ── Grouped-view sorting ─────────────────────────────────────────────────────
// The value a Portfolio row sorts by in `col`, for a strategy group or a single.
// A single uses its position's own field (as PortfolioWindow::SortPositions
// does); a multi-leg group uses its totals — net per-combo cost / mark for Avg
// Cost / Price, combo count for Qty, summed P&L / value — and falls back to its
// first leg where it has no total (Realized P&L, Day Chg %).
struct GroupSortValue {
    bool        isString = false;
    double      num = 0.0;
    std::string str;
};

inline GroupSortValue StrategySortValue(const StrategyGroup& g,
                                        const std::vector<core::Position>& positions,
                                        core::PositionColumn col) {
    GroupSortValue v;
    if (g.legIdx.empty()) return v;
    const core::Position& f = positions[(std::size_t)g.legIdx.front()];
    using C = core::PositionColumn;
    if (g.legIdx.size() == 1) {
        switch (col) {
            case C::Symbol:        v.isString = true; v.str = f.symbol; break;
            case C::Description:   v.isString = true; v.str = f.description; break;
            case C::Quantity:      v.num = f.quantity; break;
            case C::AvgCost:       v.num = AvgCostPerUnit(f); break;
            case C::Price:         v.num = f.marketPrice; break;
            case C::MarketValue:   v.num = std::abs(f.marketValue); break;
            case C::CostBasis:     v.num = std::abs(f.costBasis); break;
            case C::UnrealizedPnL: v.num = f.unrealizedPnL; break;
            case C::UnrealizedPct: v.num = f.unrealizedPct; break;
            case C::RealizedPnL:   v.num = f.realizedPnL; break;
            case C::DayChange:     v.num = f.dailyPnL; break;   // the Day P&L column
            case C::DayChangePct:  v.num = f.dayChangePct; break;
            case C::Weight:        v.num = f.portfolioWeight; break;
        }
        return v;
    }
    // Net per combo = signed dollars / (multiplier x comboQty), as displayed.
    double mult = 0.0;
    for (int li : g.legIdx) {
        const core::Position& lp = positions[(std::size_t)li];
        if (lp.assetClass == "OPT") {
            mult = lp.multiplier.empty() ? 100.0 : std::atof(lp.multiplier.c_str());
            break;
        }
    }
    const double denom = mult * g.comboQty;
    const bool   net   = g.comboQty > 0 && denom > 0.0;
    switch (col) {
        case C::Symbol:        v.isString = true; v.str = g.label; break;
        case C::Description:   v.isString = true; v.str = StrategyKindLabel(g.kind); break;
        case C::Quantity:      v.num = g.comboQty; break;
        case C::AvgCost:       v.num = net ? g.costBasis / denom   : f.avgCost; break;
        case C::Price:         v.num = net ? g.marketValue / denom : f.marketPrice; break;
        case C::MarketValue:   v.num = std::abs(g.marketValue); break;
        case C::CostBasis:     v.num = std::abs(g.costBasis); break;
        case C::UnrealizedPnL: v.num = g.unrealizedPnL; break;
        case C::UnrealizedPct:
            v.num = std::abs(g.costBasis) > 1e-9 ? g.unrealizedPnL / std::abs(g.costBasis) * 100.0 : 0.0;
            break;
        case C::RealizedPnL:   v.num = f.realizedPnL; break;
        case C::DayChange:     v.num = g.dailyPnL; break;
        case C::DayChangePct:  v.num = f.dayChangePct; break;
        case C::Weight:        v.num = g.portfolioWeight; break;
    }
    return v;
}

// Order grouped rows (strategies and singles together) by `col`. Stable, so
// ties keep the classifier's order.
inline void SortStrategyGroups(std::vector<StrategyGroup>& groups,
                               const std::vector<core::Position>& positions,
                               core::PositionColumn col, bool ascending) {
    std::stable_sort(groups.begin(), groups.end(),
        [&](const StrategyGroup& a, const StrategyGroup& b) {
            const GroupSortValue va = StrategySortValue(a, positions, col);
            const GroupSortValue vb = StrategySortValue(b, positions, col);
            if (va.isString) return ascending ? va.str < vb.str : va.str > vb.str;
            return ascending ? va.num < vb.num : va.num > vb.num;
        });
}

// ── Leg open / close marking (order ticket) ──────────────────────────────────
// What a leg does to the position already held in that contract. IB nets every
// fill, so a leg on the opposite side of a held position closes it rather than
// opening a new one — a roll's new leg can silently close an older position.
enum class LegEffect { Open, Add, Close, Flip };

inline const char* LegEffectLabel(LegEffect e) {
    switch (e) {
        case LegEffect::Open:  return "open";
        case LegEffect::Add:   return "add";
        case LegEffect::Close: return "close";
        case LegEffect::Flip:  return "flip";
    }
    return "";
}

// heldQty: signed held contracts (+long / -short, 0 = none). legQty: contracts
// this leg trades (ratio x combo qty), > 0. Close covers a partial close too;
// Flip = closes the whole holding and opens the rest on the other side.
inline LegEffect ClassifyLegEffect(double heldQty, bool buy, double legQty) {
    constexpr double eps = 1e-9;
    if (std::abs(heldQty) < eps) return LegEffect::Open;
    const bool heldLong = heldQty > 0.0;
    if (heldLong == buy) return LegEffect::Add;
    return legQty <= std::abs(heldQty) + eps ? LegEffect::Close : LegEffect::Flip;
}

}  // namespace core::services



