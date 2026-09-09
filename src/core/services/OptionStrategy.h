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
#include <cmath>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../models/PortfolioData.h"

namespace core::services {

enum class StrategyKind {
    Single,         // one option leg, or any non-option position
    Vertical,       // 2 legs, same expiry+right, opposite sign, different strike
    Calendar,       // 2 legs, same right+strike, different expiry
    Diagonal,       // 2 legs, same right, different expiry AND strike
    Straddle,       // 2 legs, same expiry+strike, different right
    Strangle,       // 2 legs, same expiry, different right AND strike
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
inline std::vector<StrategyGroup>
ClassifyStrategies(const std::vector<core::Position>& positions,
                   const std::unordered_set<long>& ungroupedConIds = {}) {
    using detail::ExpiryShort;
    using detail::StrikeStr;
    using detail::isCall;

    std::vector<StrategyGroup> out;

    // 1) Non-option positions each become their own Single group; option legs are
    //    bucketed by underlying symbol (insertion order preserved for the pass).
    //    A leg the user ungrouped is emitted flat here and never bucketed.
    std::vector<std::string>            optOrder;   // underlyings, first-seen order
    std::vector<std::vector<int>>       optBuckets; // parallel to optOrder

    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        const core::Position& p = positions[i];
        if (std::abs(p.quantity) < 1e-9) continue;          // flat — skip
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
        if (sameRt && !sameExp && sameStk) {
            g.kind = StrategyKind::Calendar;
            g.label = a.symbol + " " + StrikeStr(a.strike) + (isCall(a) ? "C" : "P")
                    + " Calendar (" + ExpiryShort(a.expiry) + "/" + ExpiryShort(c.expiry) + ")";
        } else if (sameRt && !sameExp && !sameStk) {
            g.kind = StrategyKind::Diagonal;
            g.label = a.symbol + " " + StrikeStr(a.strike) + "/" + StrikeStr(c.strike)
                    + (isCall(a) ? "C" : "P") + " Diagonal ("
                    + ExpiryShort(a.expiry) + "/" + ExpiryShort(c.expiry) + ")";
        } else if (!sameRt && sameExp && sameStk) {
            g.kind = StrategyKind::Straddle;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike) + " Straddle";
        } else if (!sameRt && sameExp && !sameStk) {
            g.kind = StrategyKind::Strangle;
            g.label = a.symbol + " " + ex + " " + StrikeStr(a.strike) + "/" + StrikeStr(c.strike)
                    + " Strangle";
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
                std::vector<double> ks; for (int k = 0; k < 4; ++k) ks.push_back(L[idx[k]].strike);
                std::sort(ks.begin(), ks.end());
                StrategyGroup g; g.underlying = sym; g.isOption = true; g.legIdx = idx;
                g.kind  = (ks[1] == ks[2]) ? StrategyKind::IronButterfly : StrategyKind::IronCondor;
                g.label = sym + " " + ex + " " + StrategyKindLabel(g.kind);
                return g;
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
                    g.kind = StrategyKind::Condor; g.label = sym + " " + ex + " Condor";
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
        for (const auto& k : keys) {
            std::vector<int> longs, shorts;
            for (int li : idx)
                if (L[li].expiry == k.first && isCall(L[li]) == k.second)
                    (L[li].quantity > 0 ? longs : shorts).push_back(li);
            auto byStrike = [&](int a, int b){ return L[a].strike < L[b].strike; };
            std::sort(longs.begin(), longs.end(), byStrike);
            std::sort(shorts.begin(), shorts.end(), byStrike);

            if (longs.empty() || shorts.empty()) {
                // One-sided partition (e.g. several long calls) -> separate legs.
                for (int li : longs)  res.push_back(singleGroup(li));
                for (int li : shorts) res.push_back(singleGroup(li));
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
        return res;
    };

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

}  // namespace core::services
