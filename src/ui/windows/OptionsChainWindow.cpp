#include "ui/windows/OptionsChainWindow.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/models/WindowGroup.h"
#include "core/services/ChartAnalysis.h"   // RoundToTick
#include "core/services/state-io.h"
#include "ui/UiScale.h"

namespace ui {

using core::services::StrikeRange;

OptionsChainWindow::OptionsChainWindow() = default;

// ── Symbol ───────────────────────────────────────────────────────────────────

void OptionsChainWindow::SetSymbol(const std::string& sym) {
    if (sym == m_symbol) return;
    m_symbol = sym;
    std::snprintf(m_symbolBuf, sizeof(m_symbolBuf), "%s", sym.c_str());

    // A new underlying invalidates everything downstream, including every live
    // option subscription — leaving them running would leak market-data lines.
    CancelAll();
    m_ticketActive = false;
    m_meta = core::OptionChainMeta{};
    m_meta.symbol      = sym;
    m_underlyingConId  = 0;
    m_underlyingPrice  = 0.0;
    m_expiryIdx        = 0;
    m_chainLoaded      = false;
    m_loading          = false;
    m_status.clear();
}

// ── Data in ──────────────────────────────────────────────────────────────────

void OptionsChainWindow::OnUnderlyingConId(int conId) {
    if (conId <= 0) return;
    m_underlyingConId = conId;
    // conId is the prerequisite for asking IB for the chain definition.
    if (m_loading && OnReqSecDefOptParams)
        OnReqSecDefOptParams(kSecDefReqId, m_symbol, m_underlyingConId);
}

void OptionsChainWindow::OnSecDefOptParams(int reqId, const std::string& tradingClass,
                                           const std::string& multiplier,
                                           int underlyingConId,
                                           const std::vector<std::string>& expirations,
                                           const std::vector<double>& strikes) {
    if (reqId != kSecDefReqId) return;
    // IB fires this once per listing exchange; fold them all into one meta.
    core::services::MergeChainDefinition(m_meta, tradingClass, multiplier,
                                         underlyingConId, expirations, strikes);
}

void OptionsChainWindow::OnSecDefOptParamsEnd(int reqId) {
    if (reqId != kSecDefReqId) return;
    m_loading     = false;
    m_chainLoaded = true;
    if (m_meta.expirations.empty() || m_meta.strikes.empty())
        m_status = "IB returned no option chain for this underlying.";
    else
        m_status.clear();
    if (m_expiryIdx >= (int)m_meta.expirations.size()) m_expiryIdx = 0;
    RebuildActiveStrikes();
    MaybeEnumerateStrikes();
}

std::string OptionsChainWindow::DeadKey(const core::OptionContractKey& k) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s|%.4f|%c", k.expiry.c_str(), k.strike, k.right);
    return buf;
}

void OptionsChainWindow::OnChainError(int code, const std::string& msg) {
    m_loading = false;
    if (!m_chainLoaded) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Chain error %d: %s", code, msg.c_str());
        m_status = buf;
    }
}

void OptionsChainWindow::OnOptionError(int reqId, int code, const std::string& msg) {
    auto it = m_reqIdToQuote.find(reqId);
    if (it == m_reqIdToQuote.end()) return;
    core::OptionQuote& q = m_quotes[it->second];

    // 200 = "no security definition": this strike/expiry/right combo does not
    // trade. Mark it dead so SyncSubscriptions stops re-requesting it, and free
    // the line. Other codes (e.g. 10197 no market data during competing
    // session) are transient, so only drop the subscription without blacklisting.
    if (code == 200) m_deadContracts.insert(DeadKey(q.key));

    m_reqIdToQuote.erase(it);
    q.subscribed = false;
    q.reqId      = 0;

    // If every attempted contract for this expiry has died, say so instead of
    // leaving a table of dashes with no explanation.
    if (code == 200 && m_status.empty()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "Some strikes are not listed for this expiration (IB 200).");
        m_status = buf;
    }
}

void OptionsChainWindow::RebuildActiveStrikes() {
    const std::string exp =
        (m_expiryIdx >= 0 && m_expiryIdx < (int)m_meta.expirations.size())
            ? m_meta.expirations[(std::size_t)m_expiryIdx] : std::string();
    auto it = m_expiryStrikes.find(exp);
    if (it != m_expiryStrikes.end() && !it->second.empty())
        m_activeStrikes = it->second;      // IB's exact set for this expiry
    else
        m_activeStrikes = m_meta.strikes;  // union fallback until enumeration lands
}

void OptionsChainWindow::MaybeEnumerateStrikes() {
    if (!m_chainLoaded) return;
    if (m_expiryIdx < 0 || m_expiryIdx >= (int)m_meta.expirations.size()) return;
    const std::string& exp = m_meta.expirations[(std::size_t)m_expiryIdx];
    if (m_expiryStrikes.count(exp)) return;   // already have it
    if (m_enumRequested == exp) return;       // already in flight
    if (!OnReqOptionStrikes) return;
    m_enumRequested = exp;
    OnReqOptionStrikes(kStrikeEnumReqId, m_symbol, exp);
}

void OptionsChainWindow::OnStrikeEnum(const std::string& expiry, double strike,
                                      const std::string& tradingClass) {
    if (expiry.empty() || strike <= 0.0) return;
    // Keep only the underlying's standard class. reqContractDetails with a
    // wildcard strike returns every listed class (TSLA, TSLA1, …); the adjusted
    // ones carry odd strikes that have model greeks but no live market, so IB
    // and tastytrade both hide them. Guard on a known class so a blank never
    // filters the whole chain away.
    if (!m_meta.tradingClass.empty() && !tradingClass.empty() &&
        tradingClass != m_meta.tradingClass)
        return;
    auto& v = m_expiryStrikes[expiry];
    // Keep sorted + deduped; enumeration arrives one contract at a time.
    auto pos = std::lower_bound(v.begin(), v.end(), strike);
    if (pos == v.end() || *pos != strike) v.insert(pos, strike);
    if (m_expiryIdx >= 0 && m_expiryIdx < (int)m_meta.expirations.size() &&
        m_meta.expirations[(std::size_t)m_expiryIdx] == expiry)
        RebuildActiveStrikes();
}

void OptionsChainWindow::OnUnderlyingPrice(double last) {
    if (last > 0.0) m_underlyingPrice = last;
}

// ── Strike range ─────────────────────────────────────────────────────────────

StrikeRange OptionsChainWindow::VisibleStrikeRange() const {
    return core::services::StrikeRangeAroundAtm(m_activeStrikes, m_underlyingPrice,
                                                m_strikeRange);
}

// ── Rendering ────────────────────────────────────────────────────────────────

// ── Quote storage ────────────────────────────────────────────────────────────

core::OptionQuote* OptionsChainWindow::FindQuote(const core::OptionContractKey& k) {
    for (auto& q : m_quotes)
        if (core::services::KeyEqual(q.key, k)) return &q;
    return nullptr;
}

const core::OptionQuote* OptionsChainWindow::FindQuote(const core::OptionContractKey& k) const {
    for (const auto& q : m_quotes)
        if (core::services::KeyEqual(q.key, k)) return &q;
    return nullptr;
}

core::OptionQuote* OptionsChainWindow::QuoteForReqId(int reqId) {
    auto it = m_reqIdToQuote.find(reqId);
    if (it == m_reqIdToQuote.end()) return nullptr;
    if (it->second >= m_quotes.size()) return nullptr;
    core::OptionQuote* q = &m_quotes[it->second];
    // A rotated reqId that no longer belongs to this quote means the tick is
    // stale (IB keeps streaming briefly after a cancel) — drop it.
    return q->reqId == reqId ? q : nullptr;
}

// ── Tick handlers ────────────────────────────────────────────────────────────

void OptionsChainWindow::OnUnderlyingChange(double chg, double chgPct) {
    m_underlyingChange    = chg;
    m_underlyingChangePct = chgPct;
}

void OptionsChainWindow::OnOptionPrice(int reqId, int field, double price) {
    core::OptionQuote* q = QuoteForReqId(reqId);
    if (!q || price < 0.0) return;
    switch (field) {
        case 1: q->bid  = price; break;   // BID
        case 2: q->ask  = price; break;   // ASK
        case 4: q->last = price; break;   // LAST
        default: return;
    }
    q->lastTick = std::time(nullptr);
    // Expected move is priced off bid/ask mids, so it moves with quotes, not
    // only with greeks ticks.
    if (field == 1 || field == 2) RecomputeExpectedMove();
}

void OptionsChainWindow::OnOptionSize(int reqId, int field, double size) {
    core::OptionQuote* q = QuoteForReqId(reqId);
    if (!q || size < 0.0) return;
    if (field == 8) {                     // VOLUME
        q->volume   = size;
        q->lastTick = std::time(nullptr);
    }
}

void OptionsChainWindow::OnOptionGeneric(int reqId, int tickType, double value) {
    core::OptionQuote* q = QuoteForReqId(reqId);
    if (!q || value < 0.0) return;
    // 100 = call open interest, 101 = put open interest. IB sends whichever
    // matches the contract's right, so either lands on this quote.
    if (tickType == 100 || tickType == 101) {
        q->openInterest = value;
        q->lastTick     = std::time(nullptr);
    }
}

void OptionsChainWindow::OnOptionGreeks(int reqId, int tickType, double impliedVol,
                                        double delta, double gamma, double vega,
                                        double theta, double undPrice) {
    core::OptionQuote* q = QuoteForReqId(reqId);
    if (!q) return;
    // 13 = model computation. Bid/ask/last computations (10/11/12) jitter with
    // every quote flicker, so the table tracks the model only.
    if (tickType != 13) return;
    q->impliedVol = impliedVol;
    q->delta      = delta;
    q->gamma      = gamma;
    q->vega       = vega;
    q->theta      = theta;
    if (undPrice > 0.0) {
        q->undPrice = undPrice;
        // IB's undPrice is authoritative for the option's own underlying and
        // arrives even when no separate underlying subscription is running.
        if (m_underlyingPrice <= 0.0) m_underlyingPrice = undPrice;
    }
    q->lastTick = std::time(nullptr);
    RecomputeExpectedMove();
}

// ── IVX + expected move ──────────────────────────────────────────────────────
// IVX is the ATM implied vol, averaged across the call and put (they differ
// through skew). Expected move is tastytrade's straddle/strangle weighting —
// see ExpectedMoveFromStraddle in OptionChain.h for why it is not the
// annualised-IV form.

void OptionsChainWindow::RecomputeExpectedMove() {
    m_ivx          = 0.0;
    m_expectedMove = 0.0;
    if (m_underlyingPrice <= 0.0 || m_activeStrikes.empty()) return;
    if (m_expiryIdx < 0 || m_expiryIdx >= (int)m_meta.expirations.size()) return;

    const int atm = core::services::FindAtmIndex(m_activeStrikes, m_underlyingPrice);
    if (atm < 0) return;

    const std::string expiry = m_meta.expirations[(std::size_t)m_expiryIdx];
    const int last = (int)m_activeStrikes.size() - 1;

    auto midAt = [&](int strikeIdx, char right) -> double {
        if (strikeIdx < 0 || strikeIdx > last) return 0.0;
        core::OptionContractKey k;
        k.symbol = m_symbol;
        k.expiry = expiry;
        k.strike = m_activeStrikes[(std::size_t)strikeIdx];
        k.right  = right;
        const core::OptionQuote* q = FindQuote(k);
        return q ? core::services::QuoteMid(q->bid, q->ask) : 0.0;
    };

    // IVx: VIX-style, integrated across the OTM wings for this expiry — not
    // the ATM implied vol, which samples a single point on the smile.
    {
        std::vector<core::services::ChainStrikeQuote> rows;
        rows.reserve(m_activeStrikes.size());
        for (double strike : m_activeStrikes) {
            core::services::ChainStrikeQuote row;
            row.strike = strike;
            core::OptionContractKey k;
            k.symbol = m_symbol;
            k.expiry = expiry;
            k.strike = strike;
            k.right  = 'C';
            if (const core::OptionQuote* c = FindQuote(k)) {
                row.callBid = c->bid; row.callAsk = c->ask;
            }
            k.right = 'P';
            if (const core::OptionQuote* p = FindQuote(k)) {
                row.putBid = p->bid; row.putAsk = p->ask;
            }
            if (row.callBid > 0.0 || row.callAsk > 0.0 ||
                row.putBid  > 0.0 || row.putAsk  > 0.0)
                rows.push_back(row);
        }
        const int dte = DaysToExpiry(m_expiryIdx);
        if (dte > 0) {
            // Rate is left at 0: we have no T-bill feed, and e^(rT) is within
            // ~0.1% of 1 for a near expiry at current short rates.
            m_ivx = core::services::ImpliedVolatilityVixStyle(
                std::move(rows), (double)dte / 365.0, 0.0);
        }
    }

    // Expected move, tastytrade weighting: a strangle pairs the call one step
    // above ATM with the put one step below.
    const double straddle  = midAt(atm,     'C') + midAt(atm,     'P');
    const double strangle1 = midAt(atm + 1, 'C') + midAt(atm - 1, 'P');
    const double strangle2 = midAt(atm + 2, 'C') + midAt(atm - 2, 'P');
    m_expectedMove   = core::services::ExpectedMoveFromStraddle(straddle, strangle1,
                                                                strangle2);
    // Which construction actually ran, so the UI can say so rather than
    // presenting a degraded number as if it were the full one.
    m_emWeighted = (straddle > 0.0 && strangle1 > 0.0 && strangle2 > 0.0);
}

// ── Subscription manager ─────────────────────────────────────────────────────
// The visible strikes decide what streams. DiffSubscriptions (pure, tested)
// works out the minimal subscribe/cancel sets and enforces the line cap.

void OptionsChainWindow::SyncSubscriptions() {
    // Switching expiry invalidates the whole set — every key carries its expiry,
    // so a stale subscription would keep streaming a contract no longer shown.
    if (m_expiryIdx != m_subscribedExpiryIdx) {
        CancelAll();
        m_subscribedExpiryIdx = m_expiryIdx;
        m_status.clear();   // any "not listed" note belonged to the old expiry
    }
    // Auto off means nothing should be streaming.
    if (!m_autoRefresh) {
        if (!m_quotes.empty()) CancelAll();
        return;
    }
    if (!m_chainLoaded ||
        m_expiryIdx < 0 || m_expiryIdx >= (int)m_meta.expirations.size()) {
        return;
    }
    if (!OnSubscribeOption || !OnCancelOption || !OnAllocOptionReqId) return;

    const core::services::StrikeRange r = VisibleStrikeRange();
    if (r.lo < 0) return;

    // Stream what is actually on screen. In ALL mode the strike filter spans the
    // whole ladder, so without this the nearest-ATM cap-slice would be the only
    // thing streaming no matter where the user scrolled. The render pass records
    // the visible span; pad a little so a row just past the edge pre-loads.
    int lo = r.lo, hi = r.hi;
    if (m_renderVisLo >= 0) {
        constexpr int kPrefetch = 2;
        lo = std::max(r.lo, m_renderVisLo - kPrefetch);
        hi = std::min(r.hi, m_renderVisHi + kPrefetch);
    }

    // Debounce: only act once the visible window has stopped moving.
    const double now = ImGui::GetTime();
    if (lo != m_lastVisLo || hi != m_lastVisHi) {
        m_lastVisLo  = lo;
        m_lastVisHi  = hi;
        m_nextSyncAt = now + 0.25;
        return;
    }
    if (now < m_nextSyncAt) return;
    m_nextSyncAt = now + 1e9;   // handled; re-armed by the next range change

    const std::string& expiry = m_meta.expirations[(std::size_t)m_expiryIdx];

    std::vector<core::OptionContractKey> desired;
    desired.reserve((std::size_t)(r.hi - r.lo + 1) * 2 + 10);

    auto want = [&](int strikeIdx) {
        if (strikeIdx < 0 || strikeIdx >= (int)m_activeStrikes.size()) return;
        for (char right : {'C', 'P'}) {
            core::OptionContractKey k;
            k.symbol = m_symbol;
            k.expiry = expiry;
            k.strike = m_activeStrikes[(std::size_t)strikeIdx];
            k.right  = right;
            if (m_deadContracts.count(DeadKey(k))) continue;  // IB already rejected it
            desired.push_back(std::move(k));
        }
    };

    for (int i = lo; i <= hi; ++i) want(i);

    // Pin the expected-move core (ATM and the two strikes either side) even
    // when it is scrolled out of view or outside the strike filter. Without
    // this, expected move silently degrades to the 0.85 fallback whenever the
    // wings are unsubscribed — and it would flicker between the two methods as
    // the user scrolls, which reads as the number being unstable.
    // DiffSubscriptions ranks by distance to the money, so these also survive
    // the subscription cap ahead of anything further out.
    const int atmIdx = core::services::FindAtmIndex(m_activeStrikes, m_underlyingPrice);
    if (atmIdx >= 0)
        for (int d = -2; d <= 2; ++d) want(atmIdx + d);

    std::vector<core::OptionContractKey> current;
    current.reserve(m_quotes.size());
    for (const auto& q : m_quotes)
        if (q.subscribed) current.push_back(q.key);

    const auto diff = core::services::DiffSubscriptions(desired, current,
                                                        kMaxOptionSubs,
                                                        m_underlyingPrice);

    for (const auto& k : diff.toCancel) {
        core::OptionQuote* q = FindQuote(k);
        if (!q || !q->subscribed) continue;
        OnCancelOption(q->reqId);
        m_reqIdToQuote.erase(q->reqId);
        q->subscribed = false;
        q->reqId      = 0;
    }

    for (const auto& k : diff.toSubscribe) {
        core::OptionQuote* q = FindQuote(k);
        if (!q) {
            core::OptionQuote nq;
            nq.key = k;
            m_quotes.push_back(std::move(nq));
            q = &m_quotes.back();
        }
        if (q->subscribed) continue;
        // A fresh reqId on every (re)subscribe: IB streams for a few ms after a
        // cancel, and reusing the id would let those stale ticks land on the new
        // contract — the Phase 15 contamination bug, in a new place.
        q->reqId      = OnAllocOptionReqId();
        q->subscribed = true;
        m_reqIdToQuote[q->reqId] = (std::size_t)(q - m_quotes.data());
        // tradingClass is deliberately omitted here: the chain flattens all
        // listing exchanges' strikes/expiries into one union, so the merged
        // class can mismatch a given contract. For standard equity/ETF options
        // IB resolves the class from symbol+expiry+strike+right, so leaving it
        // empty is both safer and correct. The order path keeps it — there it
        // is one contract the user picked, not a union.
        OnSubscribeOption(q->reqId, k, /*tradingClass=*/"", m_meta.multiplier);
    }
}

void OptionsChainWindow::CancelAll() {
    if (OnCancelOption)
        for (auto& q : m_quotes)
            if (q.subscribed) OnCancelOption(q.reqId);
    m_quotes.clear();
    m_reqIdToQuote.clear();
    m_lastVisLo = m_lastVisHi = -1;
}

// Palette lifted from the sketch: dark terminal chrome, green calls half,
// red puts half, amber sigma bands.
namespace {
constexpr ImU32 kCallsHdrBg = IM_COL32( 24,  54,  38, 255);
constexpr ImU32 kPutsHdrBg  = IM_COL32( 60,  28,  30, 255);
constexpr ImU32 kStrikeHdrBg= IM_COL32( 32,  34,  40, 255);
constexpr ImU32 kCallItmBg  = IM_COL32( 22,  46,  33, 110);
constexpr ImU32 kPutItmBg   = IM_COL32( 54,  25,  27, 110);
constexpr ImU32 kAtmRowBg   = IM_COL32( 58,  52,  16, 150);
constexpr ImU32 kSigma1Col  = IM_COL32(200, 170,  60, 190);
constexpr ImU32 kSigma2Col  = IM_COL32(190, 150,  55, 140);
constexpr ImU32 kSpotCol    = IM_COL32(225, 228, 235, 210);

const ImVec4 kDim   = ImVec4(0.55f, 0.56f, 0.62f, 1.0f);
const ImVec4 kUp    = ImVec4(0.35f, 0.80f, 0.48f, 1.0f);
const ImVec4 kDown  = ImVec4(0.90f, 0.38f, 0.38f, 1.0f);

// Dashed horizontal rule, used for the +/-2 sigma bands.
void DashedHLine(ImDrawList* dl, float x0, float x1, float y, ImU32 col,
                 float dash = 6.0f, float gap = 4.0f, float thick = 1.0f) {
    for (float x = x0; x < x1; x += dash + gap)
        dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + dash, x1), y), col, thick);
}
}  // namespace

int OptionsChainWindow::DaysToExpiry(int idx) const {
    if (idx < 0 || idx >= (int)m_meta.expirations.size()) return -1;
    const std::string& e = m_meta.expirations[(std::size_t)idx];
    if (e.size() != 8) return -1;
    std::tm t{};
    t.tm_year = std::atoi(e.substr(0, 4).c_str()) - 1900;
    t.tm_mon  = std::atoi(e.substr(4, 2).c_str()) - 1;
    t.tm_mday = std::atoi(e.substr(6, 2).c_str());
    t.tm_hour = 12;                       // noon avoids DST edge flapping
    const std::time_t exp = std::mktime(&t);
    if (exp == (std::time_t)-1) return -1;
    const double secs = std::difftime(exp, std::time(nullptr));
    return (int)std::floor(secs / 86400.0) + 1;
}

void OptionsChainWindow::RequestChain() {
    if (m_symbol.empty()) return;
    m_loading     = true;
    m_chainLoaded = false;
    m_status      = "Loading chain...";

    // Keep the symbol, drop everything derived from the previous chain.
    const std::string sym = m_symbol;
    m_meta = core::OptionChainMeta{};
    m_meta.symbol = sym;
    m_expiryIdx = 0;
    m_deadContracts.clear();
    m_expiryStrikes.clear();
    m_activeStrikes.clear();
    m_enumRequested.clear();
    CancelAll();

    if (m_underlyingConId > 0) {
        if (OnReqSecDefOptParams)
            OnReqSecDefOptParams(kSecDefReqId, sym, m_underlyingConId);
    } else if (OnRequestUnderlying) {
        // conId arrives via OnUnderlyingConId, which re-issues the request.
        OnRequestUnderlying(sym);
    } else {
        m_loading = false;
        m_status  = "Not connected.";
    }
}

void OptionsChainWindow::DrawToolbar() {
    FlexRow row;

    row.item(em(28));
    core::DrawGroupPicker(m_groupId, "##optchain_grp");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Symbol sync group");

    row.item(em(130));
    ImGui::SetNextItemWidth(em(130));
    DrawSymbolInput("##optchain_sym", m_symbolBuf, sizeof(m_symbolBuf), em(130),
                    [this](const std::string& s) {
                        SetSymbol(s);
                        if (OnBroadcastSymbol) OnBroadcastSymbol(s);
                        // Explicit in-window action, so load straight away. A
                        // group broadcast deliberately does not do this.
                        RequestChain();
                    },
                    m_symSearch);

    row.item(FlexRow::buttonW("Load Chain"));
    ImGui::BeginDisabled(m_symbol.empty() || m_loading);
    if (ImGui::Button("Load Chain")) RequestChain();
    ImGui::EndDisabled();

    // Strike count — the sketch's "Strikes: 20" dropdown. -1 == ALL, which
    // StrikeRangeAroundAtm already treats as "no filter".
    char strikesLbl[32];
    if (m_strikeRange < 0) std::snprintf(strikesLbl, sizeof(strikesLbl), "Strikes: ALL");
    else                   std::snprintf(strikesLbl, sizeof(strikesLbl), "Strikes: %d", m_strikeRange);
    row.item(FlexRow::buttonW(strikesLbl));
    if (ImGui::Button(strikesLbl)) ImGui::OpenPopup("##optchain_strikes");
    if (ImGui::BeginPopup("##optchain_strikes")) {
        static const int kOpts[] = {6, 8, 10, 12, 16, 20};
        for (int n : kOpts) {
            char l[16];
            std::snprintf(l, sizeof(l), "%d", n);
            if (ImGui::Selectable(l, m_strikeRange == n)) m_strikeRange = n;
        }
        ImGui::Separator();
        if (ImGui::Selectable("ALL", m_strikeRange < 0)) m_strikeRange = -1;
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Strikes shown each side of ATM");

    row.item(FlexRow::buttonW("Cols [+]"));
    if (ImGui::Button("Cols [+]")) ImGui::OpenPopup("##optchain_cols");
    if (ImGui::BeginPopup("##optchain_cols")) {
        ImGui::TextColored(kDim, "Per side");
        ImGui::Checkbox("Last",          &m_showLast);
        ImGui::Checkbox("Volume",        &m_showVolume);
        ImGui::Checkbox("Open Interest", &m_showOi);
        ImGui::Checkbox("IV",            &m_showIv);
        ImGui::Separator();
        ImGui::TextColored(kDim, "Greeks");
        ImGui::Checkbox("Delta", &m_showDelta);
        ImGui::Checkbox("Gamma", &m_showGamma);
        ImGui::Checkbox("Theta", &m_showTheta);
        ImGui::Checkbox("Vega",  &m_showVega);
        ImGui::EndPopup();
    }

    // Auto: keep the visible strikes subscribed as the user scrolls. There is
    // no polling interval — quotes stream, so rows are subscribed/cancelled on
    // scroll rather than re-requested on a timer.
    row.item(FlexRow::buttonW("Auto OFF"));
    if (ImGui::Button(m_autoRefresh ? "Auto ON" : "Auto OFF"))
        m_autoRefresh = !m_autoRefresh;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ON: stream quotes for the strikes currently in view,\n"
                          "subscribing and cancelling as you scroll.\n"
                          "OFF: the chain stays static until you press Load Chain.");
}

void OptionsChainWindow::DrawUnderlyingStrip() {
    FlexRow row;

    row.item(FlexRow::textW(m_symbol.c_str()) + em(60));
    ImGui::TextUnformatted(m_symbol.c_str());
    ImGui::SameLine(0.0f, em(6));
    if (m_underlyingPrice > 0.0) ImGui::Text("%.2f", m_underlyingPrice);
    else                         ImGui::TextColored(kDim, "-");

    if (m_underlyingChange != 0.0) {
        char chg[48];
        std::snprintf(chg, sizeof(chg), "%+.2f (%+.2f%%)",
                      m_underlyingChange, m_underlyingChangePct);
        row.item(FlexRow::textW(chg));
        ImGui::TextColored(m_underlyingChange >= 0 ? kUp : kDown, "%s", chg);
    }

    // IVX + expected move stay blank until option greeks arrive (Task D2) —
    // showing a fabricated number here would be worse than showing none.
    row.item(em(90));
    ImGui::TextColored(kDim, "IVX");
    ImGui::SameLine(0.0f, em(5));
    if (m_ivx > 0.0) ImGui::Text("%.1f%%", m_ivx * 100.0);
    else             ImGui::TextColored(kDim, "-");

    row.item(em(190));
    ImGui::TextColored(kDim, "Expected Move");
    ImGui::SameLine(0.0f, em(5));
    if (m_expectedMove > 0.0 && m_underlyingPrice > 0.0) {
        ImGui::Text("+/-%.2f (%.2f%%)", m_expectedMove,
                    m_expectedMove / m_underlyingPrice * 100.0);
        if (!m_emWeighted) {
            // Approximate form: say so rather than let it pass as the full one.
            ImGui::SameLine(0.0f, em(4));
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f), "~");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(m_emWeighted
                ? "0.60 x ATM straddle + 0.30 x 1st OTM strangle"
                  " + 0.10 x 2nd OTM strangle."
                : "Approximate: 0.85 x ATM straddle. "
                  "The OTM wings have no quotes yet.");
    } else {
        ImGui::TextColored(kDim, "-");
    }

    const int dte = DaysToExpiry(m_expiryIdx);
    if (dte >= 0) {
        char d[48];
        std::snprintf(d, sizeof(d), "%s - %dD",
                      m_meta.expirations[(std::size_t)m_expiryIdx].c_str(), dte);
        row.item(FlexRow::textW(d));
        ImGui::TextColored(kDim, "%s", d);
    }
}

void OptionsChainWindow::DrawExpiryTabs() {
    if (m_meta.expirations.empty()) return;
    FlexRow row;
    for (int i = 0; i < (int)m_meta.expirations.size(); ++i) {
        const std::string& e = m_meta.expirations[(std::size_t)i];
        const int dte = DaysToExpiry(i);
        char lbl[48];
        if (dte >= 0) std::snprintf(lbl, sizeof(lbl), "%s - %dD##exp%d", e.c_str(), dte, i);
        else          std::snprintf(lbl, sizeof(lbl), "%s##exp%d", e.c_str(), i);

        const bool active = (i == m_expiryIdx);
        row.item(FlexRow::buttonW(lbl));
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.78f, 1.0f));
        if (ImGui::SmallButton(lbl) && m_expiryIdx != i) {
            m_expiryIdx = i;
            RebuildActiveStrikes();     // swap to this expiry's strike set (or union)
            MaybeEnumerateStrikes();    // fetch its exact strikes if not cached
        }
        if (active) ImGui::PopStyleColor();
    }
}

void OptionsChainWindow::DrawLegend() {
    ImGui::TextColored(kDim,
        "calls ITM left  -  puts ITM right      1 sigma solid  -  2 sigma dashed");
}

void OptionsChainWindow::DrawEmptyState(const char* msg) {
    ImGui::Dummy(ImVec2(0, em(20)));
    const float w  = ImGui::GetContentRegionAvail().x;
    const float tw = ImGui::CalcTextSize(msg).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - tw) * 0.5f);
    ImGui::TextColored(kDim, "%s", msg);
}

void OptionsChainWindow::DrawChainTable() {
    const StrikeRange r = VisibleStrikeRange();
    if (r.lo < 0) { DrawEmptyState("No strikes."); return; }

    // Sketch's default per-side columns are delta / bid / ask; the rest are
    // opt-in through "Cols [+]".
    int sideCols = 2;                       // bid + ask
    if (m_showDelta)  ++sideCols;
    if (m_showLast)   ++sideCols;
    if (m_showVolume) ++sideCols;
    if (m_showOi)     ++sideCols;
    if (m_showIv)     ++sideCols;
    if (m_showGamma)  ++sideCols;
    if (m_showTheta)  ++sideCols;
    if (m_showVega)   ++sideCols;

    const int totalCols = sideCols * 2 + 1;

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV;

    // Leave room below for the one-line legend and, when a leg is staged, the
    // pinned order-ticket band — otherwise a full "Strikes: ALL" table pushes
    // the ticket off the bottom of the window and the user has to scroll to
    // reach their own order.
    const float legendH = ImGui::GetTextLineHeightWithSpacing();
    const float ticketH = m_ticketActive ? kTicketBandHeight() : 0.0f;
    float tableH = ImGui::GetContentRegionAvail().y - legendH - ticketH;
    if (tableH < em(120)) tableH = em(120);   // never collapse the table entirely

    if (!ImGui::BeginTable("##optchain", totalCols, flags, ImVec2(0.0f, tableH)))
        return;

    // Calls half is mirrored: greeks outermost, bid/ask nearest the strike.
    auto setupCalls = [&]() {
        if (m_showVega)   ImGui::TableSetupColumn("vega##c",  0, em(50));
        if (m_showTheta)  ImGui::TableSetupColumn("theta##c", 0, em(50));
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##c", 0, em(50));
        if (m_showIv)     ImGui::TableSetupColumn("iv##c",    0, em(48));
        if (m_showOi)     ImGui::TableSetupColumn("oi##c",    0, em(52));
        if (m_showVolume) ImGui::TableSetupColumn("vol##c",   0, em(52));
        if (m_showLast)   ImGui::TableSetupColumn("last##c",  0, em(56));
        if (m_showDelta)  ImGui::TableSetupColumn("delta##c", 0, em(52));
        ImGui::TableSetupColumn("bid##c", 0, em(60));
        ImGui::TableSetupColumn("ask##c", 0, em(60));
    };
    auto setupPuts = [&]() {
        ImGui::TableSetupColumn("bid##p", 0, em(60));
        ImGui::TableSetupColumn("ask##p", 0, em(60));
        if (m_showDelta)  ImGui::TableSetupColumn("delta##p", 0, em(52));
        if (m_showLast)   ImGui::TableSetupColumn("last##p",  0, em(56));
        if (m_showVolume) ImGui::TableSetupColumn("vol##p",   0, em(52));
        if (m_showOi)     ImGui::TableSetupColumn("oi##p",    0, em(52));
        if (m_showIv)     ImGui::TableSetupColumn("iv##p",    0, em(48));
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##p", 0, em(50));
        if (m_showTheta)  ImGui::TableSetupColumn("theta##p", 0, em(50));
        if (m_showVega)   ImGui::TableSetupColumn("vega##p",  0, em(50));
    };
    setupCalls();
    ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_NoHide, em(66));
    setupPuts();
    ImGui::TableSetupScrollFreeze(0, 2);

    // ── Group band: CALLS | STRIKE | PUTS ───────────────────────────────────
    // ImGui tables have no spanning cells, so the band is a normal row whose
    // cells are individually tinted, with the label in each group's middle.
    ImGui::TableNextRow();
    const int callsMid  = sideCols / 2;
    const int putsMid   = sideCols + 1 + sideCols / 2;
    for (int c = 0; c < totalCols; ++c) {
        ImGui::TableSetColumnIndex(c);
        const bool isStrike = (c == sideCols);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               isStrike ? kStrikeHdrBg
                                        : (c < sideCols ? kCallsHdrBg : kPutsHdrBg));
        if (c == callsMid)      ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.58f, 1.0f), "CALLS");
        else if (isStrike)      ImGui::TextColored(ImVec4(0.80f, 0.82f, 0.88f, 1.0f), "STRIKE");
        else if (c == putsMid)  ImGui::TextColored(ImVec4(0.92f, 0.48f, 0.48f, 1.0f), "PUTS");
    }

    // ── Sub-header row ──────────────────────────────────────────────────────
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    int col = 0;
    auto hdr = [&](const char* label) {
        ImGui::TableSetColumnIndex(col);
        // Both halves reuse labels (bid/ask/delta/...); the column index makes
        // each header's ID unique so ImGui does not warn about a conflict.
        char id[24];
        std::snprintf(id, sizeof(id), "%s##h%d", label, col);
        ImGui::TableHeader(id);
        ++col;
    };
    if (m_showVega)   hdr("vega");
    if (m_showTheta)  hdr("theta");
    if (m_showGamma)  hdr("gamma");
    if (m_showIv)     hdr("iv");
    if (m_showOi)     hdr("oi");
    if (m_showVolume) hdr("vol");
    if (m_showLast)   hdr("last");
    if (m_showDelta)  hdr("delta");
    hdr("bid"); hdr("ask");
    hdr("price");
    hdr("bid"); hdr("ask");
    if (m_showDelta)  hdr("delta");
    if (m_showLast)   hdr("last");
    if (m_showVolume) hdr("vol");
    if (m_showOi)     hdr("oi");
    if (m_showIv)     hdr("iv");
    if (m_showGamma)  hdr("gamma");
    if (m_showTheta)  hdr("theta");
    if (m_showVega)   hdr("vega");

    // Y positions of the rules we overlay after EndTable (drawing inside the
    // table would be clipped by the cell the cursor happens to be in).
    struct Rule { float y; ImU32 col; bool dashed; const char* label; };
    std::vector<Rule> rules;
    const double spot  = m_underlyingPrice;
    const double sigma = m_expectedMove;      // 0 until greeks arrive

    const std::string& curExpiry =
        (m_expiryIdx >= 0 && m_expiryIdx < (int)m_meta.expirations.size())
            ? m_meta.expirations[(std::size_t)m_expiryIdx] : std::string();

    // Row that gets the ATM highlight: the strike nearest spot AMONG the rows
    // that actually render. Dead strikes are hidden below, so choosing the raw
    // nearest strike (FindAtmIndex over the whole list) can land the highlight
    // on a skipped row and the yellow ATM band vanishes — which happened on
    // monthlies/LEAPs whose nearest listed strike has no live market.
    auto isHidden = [&](double strike) {
        if (curExpiry.empty()) return false;
        core::OptionContractKey ck{ m_symbol, curExpiry, strike, 'C' };
        core::OptionContractKey pk{ m_symbol, curExpiry, strike, 'P' };
        return m_deadContracts.count(DeadKey(ck)) &&
               m_deadContracts.count(DeadKey(pk));
    };
    int    atm     = -1;
    double atmDist = 0.0;
    if (spot > 0.0) {
        for (int i = r.lo; i <= r.hi; ++i) {
            const double s = m_activeStrikes[(std::size_t)i];
            if (isHidden(s)) continue;
            const double d = std::fabs(s - spot);
            if (atm < 0 || d < atmDist) { atm = i; atmDist = d; }
        }
    }

    // Track which strike rows are actually on screen this pass; SyncSubscriptions
    // streams that span so scrolling a long "ALL" ladder loads the visible rows.
    const float rowH = ImGui::GetFrameHeight();
    int visLo = INT_MAX, visHi = -1;

    for (int i = r.lo; i <= r.hi; ++i) {
        const double strike = m_activeStrikes[(std::size_t)i];

        // Hide strikes IB has confirmed do not trade for this expiry (both the
        // call and the put came back 200). A strike with either leg still live
        // (or not yet checked) is kept. Same predicate the ATM pick uses above,
        // so the highlight can never land on a hidden row.
        if (isHidden(strike)) continue;

        ImGui::TableNextRow();
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const float rowTop = rowPos.y;

        // Is this rendered row inside the table's scroll clip? IsRectVisible
        // tests against the current (scrolling) window's clip rect.
        if (ImGui::IsRectVisible(ImVec2(rowPos.x, rowTop),
                                 ImVec2(rowPos.x + 1.0f, rowTop + rowH))) {
            if (i < visLo) visLo = i;
            if (i > visHi) visHi = i;
        }

        // Boundary rules sit between the previous strike and this one.
        if (i > r.lo && spot > 0.0) {
            const double prev = m_activeStrikes[(std::size_t)(i - 1)];
            auto crosses = [&](double level) {
                return (prev < level && strike >= level) || (prev > level && strike <= level);
            };
            if (crosses(spot)) rules.push_back({rowTop, kSpotCol, false, "spot"});
            if (sigma > 0.0) {
                if (crosses(spot - sigma))       rules.push_back({rowTop, kSigma1Col, false, "-1 sigma"});
                if (crosses(spot + sigma))       rules.push_back({rowTop, kSigma1Col, false, "+1 sigma"});
                if (crosses(spot - 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "-2 sigma"});
                if (crosses(spot + 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "+2 sigma"});
            }
        }

        if (i == atm)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kAtmRowBg);

        // In-the-money shading: calls ITM below spot, puts ITM above.
        const bool callItm = spot > 0.0 && strike < spot;
        const bool putItm  = spot > 0.0 && strike > spot;

        int c = 0;
        core::OptionContractKey key;
        key.symbol = m_symbol;
        key.expiry = m_meta.expirations.empty()
                         ? std::string()
                         : m_meta.expirations[(std::size_t)m_expiryIdx];
        key.strike = strike;

        // One cell: value when we have it, dim dash when we do not. A blank is
        // honest here — an unsubscribed or not-yet-ticked strike has no price,
        // and printing 0.00 would read as a real quote.
        auto cell = [&](bool itm, ImU32 tint, bool have, const char* fmt, double v) {
            ImGui::TableSetColumnIndex(c++);
            if (itm && i != atm)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
            if (have) ImGui::Text(fmt, v);
            else      ImGui::TextColored(kDim, "-");
        };

        // Is this cell a staged ticket leg? Returns 1 for a buy leg (green
        // border), 2 for a sell leg (red border), 0 otherwise. A staged buy
        // corresponds to the ask cell (you buy by hitting the ask); a sell to
        // the bid. All rendered rows share the current expiry, so matching on
        // strike + right + side is sufficient.
        auto stagedLeg = [&](double strk, char right, bool isAsk) -> int {
            if (!m_ticketActive) return 0;
            auto match = [&](const core::OptionContractKey& kk, bool buy) {
                return kk.strike == strk && kk.right == right && isAsk == buy;
            };
            if (match(m_ticketKey, m_ticketBuy)) return m_ticketBuy ? 1 : 2;
            if (m_ticketIsSpread && match(m_leg2Key, m_leg2Buy))
                return m_leg2Buy ? 1 : 2;
            return 0;
        };

        // Clickable bid/ask. Convention follows the platform: clicking the ask
        // buys, clicking the bid sells — you act on the side you can hit.
        auto priceCell = [&](bool itm, ImU32 tint, const core::OptionQuote* q,
                             bool isAsk, char right) {
            ImGui::TableSetColumnIndex(c++);
            if (itm && i != atm)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
            const double v = q ? (isAsk ? q->ask : q->bid) : 0.0;
            ImGui::PushID(i * 4 + (isAsk ? 1 : 0) + (right == 'P' ? 2 : 0));
            if (v > 0.0) {
                char lbl[24];
                std::snprintf(lbl, sizeof(lbl), "%.2f", v);
                if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    core::OptionContractKey k = key;
                    k.right = right;
                    StageTicket(k, /*buy=*/isAsk);
                }
            } else {
                ImGui::TextColored(kDim, "-");
            }
            // Selection outline on the staged leg(s): green = buy, red = sell.
            if (int st = stagedLeg(strike, right, isAsk)) {
                const ImU32 bcol = st == 1 ? IM_COL32(64, 200, 96, 255)
                                           : IM_COL32(224, 72, 72, 255);
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    bcol, 2.0f, 0, 2.0f);
            }
            ImGui::PopID();
        };

        auto side = [&](char right, bool itm, ImU32 tint, bool calls) {
            key.right = right;
            const core::OptionQuote* q = FindQuote(key);
            const bool live = (q != nullptr);
            auto has = [&](double v) { return live && v != 0.0; };

            if (calls) {
                if (m_showVega)   cell(itm, tint, has(q ? q->vega  : 0), "%.3f", q ? q->vega  : 0);
                if (m_showTheta)  cell(itm, tint, has(q ? q->theta : 0), "%.3f", q ? q->theta : 0);
                if (m_showGamma)  cell(itm, tint, has(q ? q->gamma : 0), "%.4f", q ? q->gamma : 0);
                if (m_showIv)     cell(itm, tint, has(q ? q->impliedVol : 0), "%.1f%%",
                                       (q ? q->impliedVol : 0) * 100.0);
                if (m_showOi)     cell(itm, tint, has(q ? q->openInterest : 0), "%.0f", q ? q->openInterest : 0);
                if (m_showVolume) cell(itm, tint, has(q ? q->volume : 0), "%.0f", q ? q->volume : 0);
                if (m_showLast)   cell(itm, tint, has(q ? q->last   : 0), "%.2f", q ? q->last   : 0);
                if (m_showDelta)  cell(itm, tint, has(q ? q->delta  : 0), "%.2f", q ? q->delta  : 0);
                priceCell(itm, tint, q, /*isAsk=*/false, right);
                priceCell(itm, tint, q, /*isAsk=*/true,  right);
            } else {
                priceCell(itm, tint, q, /*isAsk=*/false, right);
                priceCell(itm, tint, q, /*isAsk=*/true,  right);
                if (m_showDelta)  cell(itm, tint, has(q ? q->delta  : 0), "%.2f", q ? q->delta  : 0);
                if (m_showLast)   cell(itm, tint, has(q ? q->last   : 0), "%.2f", q ? q->last   : 0);
                if (m_showVolume) cell(itm, tint, has(q ? q->volume : 0), "%.0f", q ? q->volume : 0);
                if (m_showOi)     cell(itm, tint, has(q ? q->openInterest : 0), "%.0f", q ? q->openInterest : 0);
                if (m_showIv)     cell(itm, tint, has(q ? q->impliedVol : 0), "%.1f%%",
                                       (q ? q->impliedVol : 0) * 100.0);
                if (m_showGamma)  cell(itm, tint, has(q ? q->gamma : 0), "%.4f", q ? q->gamma : 0);
                if (m_showTheta)  cell(itm, tint, has(q ? q->theta : 0), "%.3f", q ? q->theta : 0);
                if (m_showVega)   cell(itm, tint, has(q ? q->vega  : 0), "%.3f", q ? q->vega  : 0);
            }
        };

        side('C', callItm, kCallItmBg, true);

        ImGui::TableSetColumnIndex(c++);
        ImGui::Text("%.2f", strike);

        side('P', putItm, kPutItmBg, false);
    }

    const ImVec2 tblMin = ImGui::GetItemRectMin();
    const ImVec2 tblMax = ImGui::GetItemRectMax();
    ImGui::EndTable();

    // Publish the on-screen span for SyncSubscriptions (same frame).
    if (visHi >= 0) { m_renderVisLo = visLo; m_renderVisHi = visHi; }
    else            { m_renderVisLo = m_renderVisHi = -1; }

    // Overlay the spot / sigma rules across the table width.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const Rule& ru : rules) {
        if (ru.y < tblMin.y || ru.y > tblMax.y) continue;   // scrolled out of view
        if (ru.dashed) DashedHLine(dl, tblMin.x, tblMax.x, ru.y, ru.col);
        else           dl->AddLine(ImVec2(tblMin.x, ru.y), ImVec2(tblMax.x, ru.y), ru.col, 1.2f);
        dl->AddText(ImVec2(tblMin.x + em(4), ru.y - em(11)), ru.col, ru.label);
    }
}

// ── Order ticket ─────────────────────────────────────────────────────────────

void OptionsChainWindow::StageTicket(const core::OptionContractKey& key, bool buy) {
    // Two-click vertical: a second click on a different strike of the same
    // expiry + right, opposite action, turns the single leg into a spread.
    if (StageSpreadLeg(key, buy)) return;

    // Otherwise start a fresh single-leg ticket.
    m_ticketActive   = true;
    m_ticketIsSpread = false;
    m_leg1ConId = m_leg2ConId = 0;
    m_ticketKey    = key;
    m_ticketBuy    = buy;
    if (m_ticketQty < 1) m_ticketQty = 1;

    // Open at the mid: buying at the ask / selling at the bid is the worst
    // price available, and a ticket should not default to crossing the spread.
    const core::OptionQuote* q = FindQuote(key);
    const double mid = q ? core::services::QuoteMid(q->bid, q->ask) : 0.0;
    m_ticketLimit = mid > 0.0 ? core::services::RoundToTick(mid, 0.01) : 0.0;

    RecomputeTicketMetrics();
}

bool OptionsChainWindow::StageSpreadLeg(const core::OptionContractKey& key, bool buy) {
    if (!m_ticketActive || m_ticketIsSpread) return false;
    if (key.expiry != m_ticketKey.expiry)    return false;  // verticals: same expiry
    if (key.right  != m_ticketKey.right)      return false;  //          + same right
    if (key.strike == m_ticketKey.strike)     return false;  //          + different strike
    if (buy == m_ticketBuy)                    return false;  //          + one buy, one sell

    m_ticketIsSpread = true;
    m_leg2Key = key;
    m_leg2Buy = buy;
    if (m_ticketQty < 1) m_ticketQty = 1;
    // Default the limit to the current net (signed debit+/credit-).
    m_ticketLimit = core::services::RoundToTick(SpreadNetMid(), 0.01);
    ResolveSpreadConIds();
    RecomputeTicketMetrics();
    return true;
}

double OptionsChainWindow::SpreadNetMid() const {
    const core::OptionQuote* q1 = FindQuote(m_ticketKey);
    const core::OptionQuote* q2 = FindQuote(m_leg2Key);
    const double m1 = q1 ? core::services::QuoteMid(q1->bid, q1->ask) : 0.0;
    const double m2 = q2 ? core::services::QuoteMid(q2->bid, q2->ask) : 0.0;
    // Account perspective: pay for the buy leg, receive for the sell leg.
    return (m_ticketBuy ? m1 : -m1) + (m_leg2Buy ? m2 : -m2);
}

void OptionsChainWindow::ResolveSpreadConIds() {
    m_leg1ConId = m_leg2ConId = 0;
    if (!OnReqOptionLegConId) return;
    OnReqOptionLegConId(kLegConIdReqA, m_ticketKey);
    OnReqOptionLegConId(kLegConIdReqB, m_leg2Key);
}

void OptionsChainWindow::OnLegConId(int reqId, const std::string& expiry,
                                    double strike, const std::string& right,
                                    long conId) {
    if (conId <= 0 || right.empty()) return;
    auto matches = [&](const core::OptionContractKey& k) {
        return k.expiry == expiry && k.strike == strike && k.right == right[0];
    };
    if      (reqId == kLegConIdReqA && matches(m_ticketKey)) m_leg1ConId = conId;
    else if (reqId == kLegConIdReqB && matches(m_leg2Key))   m_leg2ConId = conId;
}

void OptionsChainWindow::RecomputeTicketMetrics() {
    m_ticketMetrics = core::services::StrategyMetrics{};
    if (!m_ticketActive) return;

    const int    qty  = m_ticketQty > 0 ? m_ticketQty : 1;
    const double mult = m_meta.multiplier.empty()
                            ? 100.0 : std::atof(m_meta.multiplier.c_str());

    if (m_ticketIsSpread) {
        // Two legs at their mids (so extrinsic / greeks are per-leg real), and
        // the net the user entered as the total premium (signed debit+/credit-).
        auto mkLeg = [&](const core::OptionContractKey& k, bool buy) {
            const core::OptionQuote* q = FindQuote(k);
            core::services::StrategyLeg leg;
            leg.strike = k.strike;
            leg.right  = k.right;
            leg.ratio  = (buy ? 1 : -1) * qty;
            leg.price  = q ? core::services::QuoteMid(q->bid, q->ask) : 0.0;
            if (q) { leg.delta = q->delta; leg.theta = q->theta; }
            return leg;
        };
        std::vector<core::services::StrategyLeg> legs = {
            mkLeg(m_ticketKey, m_ticketBuy), mkLeg(m_leg2Key, m_leg2Buy) };
        m_ticketMetrics = core::services::ComputeStrategyMetrics(
            legs, m_ticketLimit * qty, mult > 0.0 ? mult : 100.0, m_underlyingPrice);
        return;
    }

    const core::OptionQuote* q = FindQuote(m_ticketKey);
    core::services::StrategyLeg leg;
    leg.strike = m_ticketKey.strike;
    leg.right  = m_ticketKey.right;
    leg.ratio  = (m_ticketBuy ? 1 : -1) * qty;
    leg.price  = m_ticketLimit;
    if (q) { leg.delta = q->delta; leg.theta = q->theta; }

    // Net price is signed from the account's perspective: a buy is a debit.
    const double netPrice = (m_ticketBuy ? 1.0 : -1.0) * m_ticketLimit * qty;
    m_ticketMetrics = core::services::ComputeStrategyMetrics(
        {leg}, netPrice, mult > 0.0 ? mult : 100.0, m_underlyingPrice);
}

float OptionsChainWindow::kTicketBandHeight() const {
    // Two-column band: legs table on the left, order controls on the right.
    // Height is driven by the taller column. The left grows with a spread
    // (header + 2 legs + synthetic quote + "legs ready"); the right holds the
    // inputs / price anchors / stats / actions, which can wrap on a narrow
    // window. Reserve generously so the Send / Clear row is never trimmed.
    const float lines = m_ticketIsSpread ? 7.0f : 5.0f;
    return ImGui::GetFrameHeightWithSpacing() * lines + em(16);
}

void OptionsChainWindow::DrawOrderTicket() {
    if (!m_ticketActive) return;

    ImGui::Separator();
    // Fixed band pinned below the table; scrolls internally if it wraps.
    ImGui::BeginChild("##opt_ticket", ImVec2(0.0f, kTicketBandHeight() - em(6)),
                      ImGuiChildFlags_None);

    const core::OptionQuote* q = FindQuote(m_ticketKey);

    // ── Left column: legs ─────────────────────────────────────────────────────
    // The legs table's fixed columns sum to ~em(446); size the column to that
    // plus child padding so its right border isn't clipped. The order controls
    // sit to the right (after an explicit gap) so a two-leg spread grows
    // sideways, not down over the buttons.
    const float kLegsColW = em(486);
    ImGui::BeginChild("##opt_ticket_legs_col", ImVec2(kLegsColW, 0.0f),
                      ImGuiChildFlags_None);

    // ── Legs table ────────────────────────────────────────────────────────────
    // One row per leg: Leg | Symbol | Action | Expiry | Strike | Side | Bid | Ask.
    {
        auto legRow = [&](const char* tag, const core::OptionContractKey& k, bool buy) {
            const core::OptionQuote* qq = FindQuote(k);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(kDim, "%s", tag);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(m_symbol.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(buy ? kUp : kDown, "%s", buy ? "BUY" : "SELL");
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(k.expiry.c_str());
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", k.strike);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%c", k.right);
            ImGui::TableSetColumnIndex(6);
            if (qq && qq->bid > 0.0) ImGui::Text("%.2f", qq->bid);
            else                     ImGui::TextColored(kDim, "-");
            ImGui::TableSetColumnIndex(7);
            if (qq && qq->ask > 0.0) ImGui::Text("%.2f", qq->ask);
            else                     ImGui::TextColored(kDim, "-");
        };

        const ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##opt_ticket_legs", 8, tf)) {
            ImGui::TableSetupColumn("Leg",    ImGuiTableColumnFlags_WidthFixed, em(44));
            ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthFixed, em(64));
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, em(48));
            ImGui::TableSetupColumn("Expiry", ImGuiTableColumnFlags_WidthFixed, em(84));
            ImGui::TableSetupColumn("Strike", ImGuiTableColumnFlags_WidthFixed, em(60));
            ImGui::TableSetupColumn("Side",   ImGuiTableColumnFlags_WidthFixed, em(38));
            ImGui::TableSetupColumn("Bid",    ImGuiTableColumnFlags_WidthFixed, em(56));
            ImGui::TableSetupColumn("Ask",    ImGuiTableColumnFlags_WidthFixed, em(56));
            ImGui::TableHeadersRow();

            legRow("Leg 1", m_ticketKey, m_ticketBuy);
            if (m_ticketIsSpread) {
                legRow("Leg 2", m_leg2Key, m_leg2Buy);

                // Synthetic combo quote. A BAG has no displayed NBBO, so we
                // build it from the legs: net bid (passive/best net you could
                // rest at) = Σ(buy: +bid, sell: −ask); net ask (marketable/now)
                // = Σ(buy: +ask, sell: −bid). Signed: + debit, − credit. Both
                // clickable → send that net into the Net field.
                const core::OptionQuote* q1 = FindQuote(m_ticketKey);
                const core::OptionQuote* q2 = FindQuote(m_leg2Key);
                const bool have = q1 && q2 && q1->bid > 0.0 && q1->ask > 0.0 &&
                                  q2->bid > 0.0 && q2->ask > 0.0;
                const double netBid = have
                    ? (m_ticketBuy ? q1->bid : -q1->ask) + (m_leg2Buy ? q2->bid : -q2->ask)
                    : 0.0;
                const double netAsk = have
                    ? (m_ticketBuy ? q1->ask : -q1->bid) + (m_leg2Buy ? q2->ask : -q2->bid)
                    : 0.0;

                auto netCell = [&](int col, const char* id, double net, const char* tip) {
                    ImGui::TableSetColumnIndex(col);
                    if (!have) { ImGui::TextColored(kDim, "-"); return; }
                    ImGui::PushID(id);
                    char b[24];
                    std::snprintf(b, sizeof(b), "%+.2f", net);
                    if (ImGui::SmallButton(b)) {
                        m_ticketLimit = core::services::RoundToTick(net, 0.01);
                        RecomputeTicketMetrics();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                    ImGui::PopID();
                };

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kDim, "Spread");
                netCell(6, "spr_nb", netBid, "Net bid (passive) \xe2\x86\x92 Net");
                netCell(7, "spr_na", netAsk, "Net ask (marketable) \xe2\x86\x92 Net");
            }
            ImGui::EndTable();
        }

        if (m_ticketIsSpread) {
            const bool resolved = (m_leg1ConId > 0 && m_leg2ConId > 0);
            ImGui::TextColored(resolved ? kUp : kDim, "%s",
                               resolved ? "legs ready" : "resolving legs…");
        }
    }

    ImGui::EndChild();   // left column
    ImGui::SameLine(0.0f, em(20));   // gap between the columns

    // ── Right column: order controls ──────────────────────────────────────────
    // Width 0 = fill to the window's right edge, so the order controls are
    // right-justified with a clear gutter from the legs.
    ImGui::BeginChild("##opt_ticket_order_col", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None);

    // ── Stats strip (sits above the order row) ────────────────────────────────
    if (m_ticketMetrics.valid) {
        const auto& mm = m_ticketMetrics;
        FlexRow row;
        auto stat = [&](const char* label, const char* fmt, double v, ImVec4 col) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), fmt, v);
            row.item(FlexRow::textW(label) + FlexRow::textW(buf) + em(12));
            ImGui::TextColored(kDim, "%s", label);
            ImGui::SameLine(0.0f, em(4));
            ImGui::TextColored(col, "%s", buf);
        };

        stat("EXT",   "%.0f", mm.extrinsic, mm.extrinsic >= 0 ? kUp : kDown);
        stat("Delta", "%.2f", mm.netDelta,  ImVec4(0.85f, 0.86f, 0.9f, 1.0f));
        stat("Theta", "%.3f", mm.netTheta,  ImVec4(0.85f, 0.86f, 0.9f, 1.0f));

        // Unbounded legs must say so — a finite number here would be false.
        row.item(em(120));
        ImGui::TextColored(kDim, "Max Prof");
        ImGui::SameLine(0.0f, em(4));
        if (mm.profitUnbounded) ImGui::TextColored(kUp, "unlimited");
        else                    ImGui::TextColored(kUp, "%.0f", mm.maxProfit);

        row.item(em(120));
        ImGui::TextColored(kDim, "Max Loss");
        ImGui::SameLine(0.0f, em(4));
        if (mm.lossUnbounded) ImGui::TextColored(kDown, "unlimited");
        else                  ImGui::TextColored(kDown, "%.0f", mm.maxLoss);
    }

    // ── Qty / limit / TIF + clickable mid/nat/net ─────────────────────────────
    {
        FlexRow row;
        row.item(em(40));
        ImGui::TextColored(kDim, "Qty");
        row.item(em(60));
        ImGui::SetNextItemWidth(em(60));
        if (ImGui::InputInt("##opt_qty", &m_ticketQty, 0, 0)) {
            if (m_ticketQty < 1) m_ticketQty = 1;
            RecomputeTicketMetrics();
        }

        row.item(em(70));
        ImGui::TextColored(kDim, m_ticketIsSpread ? "Net" : "Limit");
        row.item(em(80));
        ImGui::SetNextItemWidth(em(80));
        if (ImGui::InputDouble("##opt_lmt", &m_ticketLimit, 0.0, 0.0, "%.2f")) {
            // A single leg is always paid/received as a positive premium; a
            // spread's net can be a credit (negative), so only clamp single legs.
            if (!m_ticketIsSpread && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
            RecomputeTicketMetrics();
        }

        row.item(em(70));
        ImGui::SetNextItemWidth(em(70));
        const char* kTifs[] = {"Day", "GTC"};
        ImGui::Combo("##opt_tif", &m_ticketTifIdx, kTifs, 2);

        // Clickable price references — click sends the value into Limit/Net.
        auto priceBtn = [&](const char* label, double value) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %.2f", label, value);
            row.item(FlexRow::buttonW(buf));
            if (ImGui::SmallButton(buf)) {
                m_ticketLimit = core::services::RoundToTick(value, 0.01);
                if (!m_ticketIsSpread && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
                RecomputeTicketMetrics();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use as limit price");
        };

        if (m_ticketIsSpread) {
            const double net = SpreadNetMid();
            priceBtn(net >= 0 ? "net mid (debit)" : "net mid (credit)", net);
        } else if (q) {
            // bid | mid | ask, annotated by which side is marketable for this
            // action: buying crosses the ask (nat), selling crosses the bid.
            priceBtn(m_ticketBuy ? "bid (opp)" : "bid (nat)", q->bid);
            priceBtn("mid", core::services::QuoteMid(q->bid, q->ask));
            priceBtn(m_ticketBuy ? "ask (nat)" : "ask (opp)", q->ask);
        }
    }

    // ── Actions ─────────────────────────────────────────────────────────────
    // Extra vertical space before the buttons (mirrors ChartWindow's trade
    // panel), so Send/Clear sit clear of the inputs. A leading indent gives
    // the row a left gutter; Transmit Instantly gets a wider gap from Clear.
    ImGui::Dummy(ImVec2(0.0f, em(8)));
    ImGui::Indent(em(16));
    {
        FlexRow row;
        row.item(FlexRow::buttonW("Review & Send") + em(4));
        // Single leg: needs a positive premium. Spread: needs both leg conIds
        // resolved (the net may legitimately be a credit, i.e. negative/zero).
        const bool priced = m_ticketIsSpread ? (m_leg1ConId > 0 && m_leg2ConId > 0)
                                             : (m_ticketLimit > 0.0);
        ImGui::BeginDisabled(!priced);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.48f, 0.12f, 1.0f));
        if (ImGui::Button(m_transmitInstantly ? "Send" : "Review & Send")) {
            core::Order o;
            o.symbol     = m_symbol;
            o.type       = core::OrderType::Limit;
            o.tif        = m_ticketTifIdx == 1 ? core::TimeInForce::GTC
                                               : core::TimeInForce::Day;
            o.quantity   = (double)m_ticketQty;
            o.limitPrice = m_ticketLimit;
            o.exchange   = "SMART";
            o.spec.symbol       = m_symbol;
            o.spec.exchange     = "SMART";
            o.spec.currency     = "USD";
            o.spec.multiplier   = m_meta.multiplier.empty() ? "100" : m_meta.multiplier;

            if (m_ticketIsSpread) {
                // A BAG combo: the order buys the spread at the net (positive =
                // debit, negative = credit); each leg carries its own BUY/SELL.
                o.side          = core::OrderSide::Buy;
                o.spec.secType  = "BAG";
                o.spec.comboLegs = {
                    { m_leg1ConId, 1, m_ticketBuy ? "BUY" : "SELL", "SMART" },
                    { m_leg2ConId, 1, m_leg2Buy   ? "BUY" : "SELL", "SMART" },
                };
            } else {
                o.side          = m_ticketBuy ? core::OrderSide::Buy
                                              : core::OrderSide::Sell;
                o.spec.secType  = "OPT";
                o.spec.lastTradeDateOrContractMonth = m_ticketKey.expiry;
                o.spec.strike   = m_ticketKey.strike;
                o.spec.right    = std::string(1, m_ticketKey.right);
                // tradingClass deliberately omitted, same as the streaming path:
                // the merged class from the flattened chain can mismatch a
                // contract and IB rejects it with error 200. IB resolves the
                // standard class from symbol+expiry+strike+right.
                o.spec.tradingClass = "";
            }

            m_pendingOrder = o;
            if (m_transmitInstantly) {
                if (OnOrderSubmit) OnOrderSubmit(m_pendingOrder);
                m_ticketActive = false;
            } else {
                m_showConfirm = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();

        row.item(FlexRow::buttonW("Clear"));
        if (ImGui::Button("Clear")) m_ticketActive = false;

        row.item(FlexRow::checkboxW("Transmit Instantly"), em(24));
        ImGui::Checkbox("Transmit Instantly", &m_transmitInstantly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: every order goes through the confirmation dialog.");
    }
    ImGui::Unindent(em(16));

    ImGui::EndChild();   // right column
    ImGui::EndChild();   // ticket band
}

void OptionsChainWindow::DrawConfirmPopup() {
    if (m_showConfirm) {
        ImGui::OpenPopup("Confirm Option Order##optchain_confirm");
        m_showConfirm = false;
    }
    // Centre on this window's own viewport — a modal that opens on the main
    // viewport is invisible when the chain has been dragged out, while still
    // swallowing input.
    ImGui::SetNextWindowPos(ImGui::GetWindowViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em(340), 0), ImGuiCond_Always);

    if (!ImGui::BeginPopupModal("Confirm Option Order##optchain_confirm", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const core::Order& o = m_pendingOrder;
    const bool buy = (o.side == core::OrderSide::Buy);
    const bool isSpread = (o.spec.secType == "BAG");
    const double mult = std::atof(o.spec.multiplier.c_str());

    if (isSpread) {
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "VERTICAL");
        ImGui::Separator();
        ImGui::Text("%s  %s", o.symbol.c_str(), m_ticketKey.expiry.c_str());
        ImGui::Text("%s %.2f%c   /   %s %.2f%c",
                    m_ticketBuy ? "BUY" : "SELL", m_ticketKey.strike, m_ticketKey.right,
                    m_leg2Buy   ? "BUY" : "SELL", m_leg2Key.strike,   m_leg2Key.right);
        ImGui::Text("Qty %.0f  x%s", o.quantity, o.spec.multiplier.c_str());
        const bool credit = o.limitPrice < 0.0;
        ImGui::Text("Net %+.2f  (%s)   %s", o.limitPrice, credit ? "credit" : "debit",
                    o.tif == core::TimeInForce::GTC ? "GTC" : "DAY");
        ImGui::TextColored(kDim, "Est. %s %.2f", credit ? "credit" : "debit",
                           std::fabs(o.limitPrice) * o.quantity * (mult > 0 ? mult : 100.0));
    } else {
        // Deliberately not labelled "to open" / "to close": the chain does not
        // track existing option positions, so it cannot know which this is.
        ImGui::TextColored(buy ? kUp : kDown, "%s", buy ? "BUY" : "SELL");
        ImGui::Separator();
        ImGui::Text("%s  %s  %.2f %s", o.symbol.c_str(),
                    o.spec.lastTradeDateOrContractMonth.c_str(),
                    o.spec.strike, o.spec.right.c_str());
        ImGui::Text("Qty %.0f  x%s", o.quantity, o.spec.multiplier.c_str());
        ImGui::Text("Limit %.2f   %s", o.limitPrice,
                    o.tif == core::TimeInForce::GTC ? "GTC" : "DAY");
        ImGui::TextColored(kDim, "Est. %s %.2f", buy ? "debit" : "credit",
                           o.limitPrice * o.quantity * (mult > 0 ? mult : 100.0));
    }

    if (m_ticketMetrics.valid) {
        ImGui::Separator();
        if (m_ticketMetrics.lossUnbounded)
            ImGui::TextColored(kDown, "Max loss: unlimited");
        else
            ImGui::TextColored(kDown, "Max loss: %.0f", m_ticketMetrics.maxLoss);
        if (m_ticketMetrics.profitUnbounded)
            ImGui::TextColored(kUp, "Max profit: unlimited");
        else
            ImGui::TextColored(kUp, "Max profit: %.0f", m_ticketMetrics.maxProfit);
    }

    ImGui::Separator();
    if (ImGui::Button("Confirm", ImVec2(em(120), em(24)))) {
        if (OnOrderSubmit) OnOrderSubmit(m_pendingOrder);
        m_ticketActive = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(em(120), em(24))) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool OptionsChainWindow::Render() {
    if (!m_open) return false;

    char title[96];
    std::snprintf(title, sizeof(title), "Options Chain%s%s G%d###optionschain",
                  m_symbol.empty() ? "" : " ", m_symbol.c_str(), m_groupId);

    ImGui::SetNextWindowSize(ImVec2(em(900), em(520)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &m_open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return m_open;
    }

    DrawToolbar();
    ImGui::Separator();

    if (!m_symbol.empty()) {
        DrawUnderlyingStrip();
        ImGui::Separator();
    }
    if (m_chainLoaded && !m_meta.expirations.empty()) {
        DrawExpiryTabs();
        ImGui::Separator();
    }

    if (!m_status.empty())
        ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f), "%s", m_status.c_str());

    if (m_symbol.empty())            DrawEmptyState("Enter an underlying symbol to load its chain.");
    else if (m_loading)              DrawEmptyState("Loading chain…");
    else if (!m_chainLoaded)         DrawEmptyState("Press Load Chain.");
    else if (m_meta.strikes.empty()) DrawEmptyState("No strikes returned for this underlying.");
    else                           { DrawChainTable(); DrawLegend(); SyncSubscriptions(); }

    DrawOrderTicket();
    DrawConfirmPopup();

    ImGui::End();
    return m_open;
}

// ── State persistence ────────────────────────────────────────────────────────

void OptionsChainWindow::SerializeSettings(core::services::StateBlock& b) const {
    using namespace core::services;
    SetBool  (b, "OPT_OPEN",        m_open);
    SetInt   (b, "OPT_GROUP",       m_groupId);
    SetString(b, "OPT_SYMBOL",      m_symbol);
    SetInt   (b, "OPT_EXPIRY_IDX",  m_expiryIdx);
    SetInt   (b, "OPT_STRIKE_RANGE",m_strikeRange);
    SetBool  (b, "OPT_AUTO",        m_autoRefresh);
    SetBool  (b, "OPT_COL_LAST",    m_showLast);
    SetBool  (b, "OPT_COL_VOLUME",  m_showVolume);
    SetBool  (b, "OPT_COL_OI",      m_showOi);
    SetBool  (b, "OPT_COL_IV",      m_showIv);
    SetBool  (b, "OPT_COL_DELTA",   m_showDelta);
    SetBool  (b, "OPT_COL_GAMMA",   m_showGamma);
    SetBool  (b, "OPT_COL_THETA",   m_showTheta);
    SetBool  (b, "OPT_COL_VEGA",    m_showVega);
}

void OptionsChainWindow::ApplySettings(const core::services::StateBlock& b) {
    using namespace core::services;
    m_open        = GetBool(b, "OPT_OPEN", m_open);
    m_groupId     = GetInt (b, "OPT_GROUP", m_groupId, 1, core::kNumGroups);
    m_strikeRange = GetInt (b, "OPT_STRIKE_RANGE", m_strikeRange, -1, 200);
    m_expiryIdx   = GetInt (b, "OPT_EXPIRY_IDX", 0, 0, 1000);

    const std::string sym = GetString(b, "OPT_SYMBOL", "");
    if (!sym.empty()) {
        // Restore the symbol only — no IB traffic on apply. The user presses
        // Load Chain, exactly as the Scanner restores filters without scanning.
        m_symbol = sym;
        m_meta.symbol = sym;
        std::snprintf(m_symbolBuf, sizeof(m_symbolBuf), "%s", sym.c_str());
    }

    m_autoRefresh    = GetBool(b, "OPT_AUTO", m_autoRefresh);
    m_showLast   = GetBool(b, "OPT_COL_LAST",   m_showLast);
    m_showVolume = GetBool(b, "OPT_COL_VOLUME", m_showVolume);
    m_showOi     = GetBool(b, "OPT_COL_OI",     m_showOi);
    m_showIv     = GetBool(b, "OPT_COL_IV",     m_showIv);
    m_showDelta  = GetBool(b, "OPT_COL_DELTA",  m_showDelta);
    m_showGamma  = GetBool(b, "OPT_COL_GAMMA",  m_showGamma);
    m_showTheta  = GetBool(b, "OPT_COL_THETA",  m_showTheta);
    m_showVega   = GetBool(b, "OPT_COL_VEGA",   m_showVega);
}

}  // namespace ui
