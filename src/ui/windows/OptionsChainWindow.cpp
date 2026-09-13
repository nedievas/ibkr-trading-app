#include "ui/windows/OptionsChainWindow.h"

#include <algorithm>
#include <cctype>
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

void OptionsChainWindow::SetOptionPositions(const std::vector<core::Position>& opts) {
    // Full snapshot: rebuild wholesale so legs that went flat drop out cleanly.
    m_positions.clear();
    for (const auto& p : opts) {
        if (p.assetClass != "OPT" || std::abs(p.quantity) < 1e-9) continue;
        if (p.right.empty()) continue;
        core::OptionContractKey k{ p.symbol, p.expiry, p.strike,
                                   static_cast<char>(std::toupper(p.right[0])) };
        // Sum in case IB reports the same contract twice (it shouldn't).
        HeldLeg& h = m_positions[DeadKey(k)];
        h.qty     += p.quantity;
        h.avgCost  = p.avgCost;
        h.conId    = p.conId;
    }
}

const OptionsChainWindow::HeldLeg*
OptionsChainWindow::HeldFor(const std::string& expiry, double strike, char right) const {
    if (expiry.empty()) return nullptr;
    core::OptionContractKey k{ m_symbol, expiry, strike, right };
    auto it = m_positions.find(DeadKey(k));
    if (it == m_positions.end() || std::abs(it->second.qty) < 1e-9) return nullptr;
    return &it->second;
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

void OptionsChainWindow::OnUnderlyingTick(int field, double value) {
    if (value <= 0.0) return;
    switch (field) {
        case 1: m_underlyingBid = value; break;         // BID
        case 2: m_underlyingAsk = value; break;         // ASK
        case 4: m_underlyingPrice = value; break;       // LAST
        case 9:                                         // prev CLOSE
            m_underlyingPrevClose = value;
            if (m_underlyingPrice <= 0.0) m_underlyingPrice = value;  // pre-market stand-in
            break;
        default: return;
    }
    if (m_underlyingPrevClose > 0.0 && m_underlyingPrice > 0.0) {
        m_underlyingChange    = m_underlyingPrice - m_underlyingPrevClose;
        m_underlyingChangePct = m_underlyingChange / m_underlyingPrevClose * 100.0;
    }
}

void OptionsChainWindow::OnUnderlyingSize(int field, double value) {
    if (field == 8 && value >= 0.0) m_underlyingVol = value;  // VOLUME (cumulative)
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
    switch (field) {
        case 8:                    // VOLUME
            q->volume = size;
            q->lastTick = std::time(nullptr);
            break;
        // IB sends BOTH 27 (OPTION_CALL_OPEN_INTEREST) and 28 (OPTION_PUT_OPEN_
        // INTEREST) to every option contract — the side that doesn't match the
        // contract's right reports 0. Take only the matching field so the 0 can't
        // clobber the real value. 22 = generic OPEN_INTEREST (right-agnostic).
        case 22:
            q->openInterest = size;
            q->lastTick = std::time(nullptr);
            break;
        case 27:
            if (q->key.right == 'C') { q->openInterest = size; q->lastTick = std::time(nullptr); }
            break;
        case 28:
            if (q->key.right == 'P') { q->openInterest = size; q->lastTick = std::time(nullptr); }
            break;
        default: break;
    }
}

void OptionsChainWindow::OnOptionGeneric(int /*reqId*/, int /*tickType*/, double /*value*/) {
    // Open interest is a tickSize (27/28), handled in OnOptionSize. tickGeneric
    // carries no chain field we display today; kept wired for future use.
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
// red puts half, azure SD bands.
namespace {
constexpr ImU32 kCallsHdrBg = IM_COL32( 24,  54,  38, 255);
constexpr ImU32 kPutsHdrBg  = IM_COL32( 60,  28,  30, 255);
constexpr ImU32 kStrikeHdrBg= IM_COL32( 32,  34,  40, 255);
constexpr ImU32 kCallItmBg  = IM_COL32( 22,  46,  33, 110);
constexpr ImU32 kPutItmBg   = IM_COL32( 54,  25,  27, 110);
constexpr ImU32 kSigma1Col  = IM_COL32( 70, 150, 240, 200);
constexpr ImU32 kSigma2Col  = IM_COL32( 70, 150, 240, 140);
constexpr ImU32 kSdPill     = IM_COL32( 70, 150, 240, 255);  // solid azure pill
constexpr ImU32 kSdInk      = IM_COL32( 12,  18,  28, 255);  // inverse (dark) text
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

    row.item(FlexRow::buttonW("Cols"));
    if (ImGui::Button("Cols")) ImGui::OpenPopup("##optchain_cols");
    if (ImGui::BeginPopup("##optchain_cols")) {
        ImGui::TextUnformatted("Visible Columns");
        ImGui::Separator();
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

}

void OptionsChainWindow::DrawUnderlyingStrip() {
    // One line: symbol  last  Chg: x  Chg%: x  Bid: x  Ask: x  Vol: x  IVX: x
    // Exp Move: x — spaced, no separators, labels carry a colon.
    const bool haveChg = (m_underlyingPrevClose > 0.0 && m_underlyingPrice > 0.0);
    auto gap = [&]() { ImGui::SameLine(0.0f, em(14)); };
    auto lab = [&](const char* t) {
        ImGui::TextColored(kDim, "%s", t); ImGui::SameLine(0.0f, em(4));
    };
    auto num = [&](double v) {
        if (v > 0.0) ImGui::Text("%.2f", v);
        else         ImGui::TextColored(kDim, "-");
    };

    ImGui::TextUnformatted(m_symbol.c_str());
    gap(); num(m_underlyingPrice);
    gap(); lab("Chg:");
    if (haveChg) ImGui::TextColored(m_underlyingChange >= 0 ? kUp : kDown,
                                    "%+.2f", m_underlyingChange);
    else         ImGui::TextColored(kDim, "-");
    gap(); lab("Chg%:");
    if (haveChg) ImGui::TextColored(m_underlyingChangePct >= 0 ? kUp : kDown,
                                    "%+.2f%%", m_underlyingChangePct);
    else         ImGui::TextColored(kDim, "-");
    gap(); lab("Bid:");
    if (m_underlyingBid > 0.0) ImGui::TextColored(kUp,   "%.2f", m_underlyingBid);
    else                       ImGui::TextColored(kDim, "-");
    gap(); lab("Ask:");
    if (m_underlyingAsk > 0.0) ImGui::TextColored(kDown, "%.2f", m_underlyingAsk);
    else                       ImGui::TextColored(kDim, "-");
    gap(); lab("Vol:");
    if (m_underlyingVol > 0.0) {
        char vb[24];
        if      (m_underlyingVol >= 1e6) std::snprintf(vb, sizeof(vb), "%.1fM", m_underlyingVol / 1e6);
        else if (m_underlyingVol >= 1e3) std::snprintf(vb, sizeof(vb), "%.0fK", m_underlyingVol / 1e3);
        else                             std::snprintf(vb, sizeof(vb), "%.0f",  m_underlyingVol);
        ImGui::TextUnformatted(vb);
    } else {
        ImGui::TextColored(kDim, "-");
    }
    gap(); lab("IVX:");
    if (m_ivx > 0.0) ImGui::Text("%.1f%%", m_ivx * 100.0);
    else             ImGui::TextColored(kDim, "-");
    gap(); lab("Exp Move:");
    if (m_expectedMove > 0.0 && m_underlyingPrice > 0.0) {
        ImGui::Text("+/-%.2f (%.2f%%)%s", m_expectedMove,
                    m_expectedMove / m_underlyingPrice * 100.0,
                    m_emWeighted ? "" : " ~");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(m_emWeighted
                ? "0.60 x ATM straddle + 0.30 x 1st OTM strangle"
                  " + 0.10 x 2nd OTM strangle."
                : "Approximate: 0.85 x ATM straddle. "
                  "The OTM wings have no quotes yet.");
    } else {
        ImGui::TextColored(kDim, "-");
    }

    // Equity-leg buttons — add the underlying to the ticket cart to build a
    // covered call / married put / collar. Highlight when a same-side stock leg
    // is already staged (click again to toggle it off).
    {
        bool haveBuy = false, haveSell = false;
        for (const TicketLeg& L : m_legs)
            if (L.stock) { (L.buy ? haveBuy : haveSell) = true; }
        const bool ready = (m_underlyingConId > 0);
        gap(); lab("Stock:");
        ImGui::BeginDisabled(!ready);
        auto shBtn = [&](const char* id, bool buy, bool on, ImVec4 col) {
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, col);
            if (ImGui::SmallButton(id)) AddOrToggleStockLeg(buy);
            if (on) ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, em(4));
        };
        shBtn("+Buy 100", true,  haveBuy,  ImVec4(0.14f, 0.45f, 0.20f, 1.0f));
        shBtn("+Sell 100", false, haveSell, ImVec4(0.55f, 0.14f, 0.14f, 1.0f));
        ImGui::EndDisabled();
        if (!ready && ImGui::IsItemHovered())
            ImGui::SetTooltip("Load the chain first (resolving the underlying).");
    }

    // Far-right toggle: collapse the (wrapping) expiry tabs into one scrollable
    // row with < > arrows, or expand them back to wrap.
    const float btnW = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - btnW);
    if (ImGui::ArrowButton("##exp_mode", m_expirySingleRow ? ImGuiDir_Down : ImGuiDir_Up))
        m_expirySingleRow = !m_expirySingleRow;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m_expirySingleRow ? "Expand expirations (wrap to rows)"
                                            : "Collapse expirations to one scrollable row");
}

void OptionsChainWindow::DrawExpiryTabs() {
    if (m_meta.expirations.empty()) return;
    static const char* kMon[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                 "Jul","Aug","Sep","Oct","Nov","Dec"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 azure   = IM_COL32( 46, 140, 235, 255);
    const ImU32 bright  = IM_COL32(235, 238, 245, 255);
    const ImU32 dim     = IM_COL32(140, 145, 155, 255);
    const ImU32 hov     = IM_COL32(200, 205, 215, 255);
    const ImU32 dteAct  = IM_COL32(180, 190, 210, 255);

    const float lh = ImGui::GetTextLineHeight();
    const float h  = lh * 2.0f + em(6);   // date line + DTE line + underline slack
    const int   n  = (int)m_meta.expirations.size();

    // "20261016" -> "Oct 16 '26" (line 1) + "42 DTE" (line 2); raw fallback.
    auto labels = [&](int i, char* d1, std::size_t n1, char* d2, std::size_t n2) {
        const std::string& e = m_meta.expirations[(std::size_t)i];
        const int dte = DaysToExpiry(i);
        if (e.size() == 8) {
            const int mo = (e[4]-'0')*10 + (e[5]-'0');
            std::snprintf(d1, n1, "%s %c%c '%c%c",
                          kMon[(mo>=1&&mo<=12)?mo-1:0], e[6], e[7], e[2], e[3]);
        } else {
            std::snprintf(d1, n1, "%s", e.c_str());
        }
        if (dte >= 0) std::snprintf(d2, n2, "%d DTE", dte);
        else          std::snprintf(d2, n2, "-");
    };
    auto tabW = [&](int i) {
        char d1[32], d2[24]; labels(i, d1, sizeof(d1), d2, sizeof(d2));
        return std::max(ImGui::CalcTextSize(d1).x, ImGui::CalcTextSize(d2).x) + em(16);
    };
    auto renderTab = [&](int i, float w) {
        char d1[32], d2[24]; labels(i, d1, sizeof(d1), d2, sizeof(d2));
        const bool active = (i == m_expiryIdx);
        // Current window's draw list: parent when wrapped, child when scrolling —
        // so scrolled-out tabs clip to the strip instead of bleeding out.
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##exp", ImVec2(w, h)) && m_expiryIdx != i) {
            // Suppress selection if this was a drag (scrolling), not a click.
            const float dx = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
            if (dx * dx <= 36.0f) {
                m_expiryIdx = i;
                RebuildActiveStrikes();   // swap to this expiry's strike set (or union)
                MaybeEnumerateStrikes();  // fetch its exact strikes if not cached
            }
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        tdl->AddText(ImVec2(p.x, p.y),      active ? bright : (hovered ? hov : dim), d1);
        tdl->AddText(ImVec2(p.x, p.y + lh), active ? dteAct : dim,                   d2);
        if (active)
            tdl->AddLine(ImVec2(p.x, p.y + h - em(1)),
                         ImVec2(p.x + w - em(10), p.y + h - em(1)), azure, em(2));
        ImGui::PopID();
    };

    // Wrapped (default): tabs flow onto as many rows as needed.
    if (!m_expirySingleRow) {
        FlexRow row;
        for (int i = 0; i < n; ++i) { const float w = tabW(i); row.item(w); renderTab(i, w); }
        return;
    }

    // Single row: < prev | drag-scrollable strip of all tabs | next >.
    // Slim, semi-transparent chevrons drawn full-height so they sit centered in
    // the row (not the boxy, top-aligned ArrowButton).
    const float rowH   = h + em(4);
    const float arrowW = em(18);
    auto chevron = [&](const char* id, bool left) -> bool {
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(arrowW, rowH));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const float cx = cp.x + arrowW * 0.5f, cy = cp.y + rowH * 0.5f;
        const float s = em(8), k = em(4);
        const ImU32 col = IM_COL32(205, 210, 220, hovered ? 235 : 110);
        if (left) {
            dl->AddLine(ImVec2(cx + k, cy - s), ImVec2(cx - k, cy), col, em(1.5f));
            dl->AddLine(ImVec2(cx - k, cy), ImVec2(cx + k, cy + s), col, em(1.5f));
        } else {
            dl->AddLine(ImVec2(cx - k, cy - s), ImVec2(cx + k, cy), col, em(1.5f));
            dl->AddLine(ImVec2(cx + k, cy), ImVec2(cx - k, cy + s), col, em(1.5f));
        }
        return clicked;
    };

    if (chevron("##exp_prev", /*left=*/true)) m_expiryScrollReq = -1.0f;
    ImGui::SameLine(0.0f, em(4));

    const float childW = ImGui::GetContentRegionAvail().x - arrowW - em(8);
    ImGui::BeginChild("##exprow", ImVec2(childW, rowH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (int i = 0; i < n; ++i) { if (i) ImGui::SameLine(0.0f, em(2)); renderTab(i, tabW(i)); }

    // Apply a pending arrow step, then let a left-drag pan the strip.
    if (m_expiryScrollReq != 0.0f) {
        ImGui::SetScrollX(ImGui::GetScrollX() + m_expiryScrollReq * childW * 0.6f);
        m_expiryScrollReq = 0.0f;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f))
        ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseDelta.x);
    ImGui::EndChild();

    ImGui::SameLine(ImGui::GetContentRegionMax().x - arrowW);
    if (chevron("##exp_next", /*left=*/false)) m_expiryScrollReq = 1.0f;
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

    // Label columns carry the overlay pills/badges as normal cells (default row
    // background, no value): SD (leftmost), call-ITM (left of STRIKE), put-ITM
    // (right of STRIKE), and a trailing END margin mirroring SD so the table
    // terminates symmetrically with no bare extension on the right.
    // Order: [SD][calls…][callITM][STRIKE][putITM][puts…][END].
    const int totalCols  = sideCols * 2 + 5;
    const int sdCol      = 0;
    const int callItmCol = 1 + sideCols;
    const int strikeCol  = callItmCol + 1;
    const int putItmCol  = strikeCol + 1;
    const int endCol     = totalCols - 1;

    // No ScrollX: the data columns stretch to fill the window (see setupCalls),
    // so the table's right edge always snaps to the window's right side.
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV;

    // Leave room below for the pinned order-ticket band when a leg is staged —
    // otherwise a full "Strikes: ALL" table pushes the ticket off the bottom of
    // the window and the user has to scroll to reach their own order.
    const float ticketH = m_ticketActive ? kTicketBandHeight() : 0.0f;
    float tableH = ImGui::GetContentRegionAvail().y - ticketH;
    if (tableH < em(120)) tableH = em(120);   // never collapse the table entirely

    // Small side padding so autosized (content-fit) columns aren't cramped —
    // the values are centered, this just keeps them off the column borders.
    const ImVec2 basePad = ImGui::GetStyle().CellPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(basePad.x + em(4), basePad.y));

    // New id ("##optchain2") on purpose: the column structure changed (added the
    // SD / ITM label columns), and reusing the old id would inherit stale, now
    // mismatched column settings from imgui.ini (phantom wide columns).
    // Outer width 0 = fill the window; the WidthStretch data columns absorb it.
    if (!ImGui::BeginTable("##optchain2", totalCols, flags, ImVec2(0.0f, tableH))) {
        ImGui::PopStyleVar();
        return;
    }

    // Every greek/price column stretches with equal weight so they widen
    // symmetrically as the window grows (calls mirror puts). SD/ITM/STRIKE/END
    // stay WidthFixed, so only the data columns absorb the extra width.
    const ImGuiTableColumnFlags autoCol = ImGuiTableColumnFlags_WidthStretch;
    // Calls half is mirrored: greeks outermost, bid/ask nearest the strike.
    auto setupCalls = [&]() {
        if (m_showVega)   ImGui::TableSetupColumn("vega##c",  autoCol, 1.0f);
        if (m_showTheta)  ImGui::TableSetupColumn("theta##c", autoCol, 1.0f);
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##c", autoCol, 1.0f);
        if (m_showIv)     ImGui::TableSetupColumn("iv##c",    autoCol, 1.0f);
        if (m_showOi)     ImGui::TableSetupColumn("oi##c",    autoCol, 1.0f);
        if (m_showVolume) ImGui::TableSetupColumn("vol##c",   autoCol, 1.0f);
        if (m_showLast)   ImGui::TableSetupColumn("last##c",  autoCol, 1.0f);
        if (m_showDelta)  ImGui::TableSetupColumn("delta##c", autoCol, 1.0f);
        ImGui::TableSetupColumn("bid##c", autoCol, 1.0f);
        ImGui::TableSetupColumn("ask##c", autoCol, 1.0f);
    };
    auto setupPuts = [&]() {
        ImGui::TableSetupColumn("bid##p", autoCol, 1.0f);
        ImGui::TableSetupColumn("ask##p", autoCol, 1.0f);
        if (m_showDelta)  ImGui::TableSetupColumn("delta##p", autoCol, 1.0f);
        if (m_showLast)   ImGui::TableSetupColumn("last##p",  autoCol, 1.0f);
        if (m_showVolume) ImGui::TableSetupColumn("vol##p",   autoCol, 1.0f);
        if (m_showOi)     ImGui::TableSetupColumn("oi##p",    autoCol, 1.0f);
        if (m_showIv)     ImGui::TableSetupColumn("iv##p",    autoCol, 1.0f);
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##p", autoCol, 1.0f);
        if (m_showTheta)  ImGui::TableSetupColumn("theta##p", autoCol, 1.0f);
        if (m_showVega)   ImGui::TableSetupColumn("vega##p",  autoCol, 1.0f);
    };
    // Label columns: width of the pill/badge, fixed, non-resizable/reorderable.
    const ImGuiTableColumnFlags labelCol = ImGuiTableColumnFlags_WidthFixed |
                                           ImGuiTableColumnFlags_NoResize   |
                                           ImGuiTableColumnFlags_NoReorder;
    // SD column sized to the pill exactly (75% text + its h-padding) so there is
    // no slack to the right of the pill.
    const float sdColW = ImGui::CalcTextSize("-2 SD").x * 0.75f + em(3) * 2.0f;
    // ITM columns share the SD column width.
    const float itmBadgeW = em(13) + ImGui::CalcTextSize("ITM").x + em(4);
    const float itmColW   = sdColW;
    ImGui::TableSetupColumn("##sd",   labelCol, sdColW);
    setupCalls();
    ImGui::TableSetupColumn("##citm", labelCol, itmColW);   // call-ITM badge
    ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_NoHide |
                                     ImGuiTableColumnFlags_WidthFixed |
                                     ImGuiTableColumnFlags_NoResize, em(66));
    ImGui::TableSetupColumn("##pitm", labelCol, itmColW);   // put-ITM badge
    setupPuts();
    ImGui::TableSetupColumn("##end",  labelCol, sdColW);    // trailing margin (= SD)
    ImGui::TableSetupScrollFreeze(0, 2);

    // ── Group band: CALLS | STRIKE | PUTS ───────────────────────────────────
    // ImGui tables have no spanning cells, so the band is a normal row whose
    // cells are individually tinted, with the label in each group's middle.
    ImGui::TableNextRow();
    const int callsMid  = 1 + sideCols / 2;                 // calls range is [1, sideCols]
    const int putsMid   = putItmCol + 1 + sideCols / 2;     // puts range starts after putITM
    for (int c = 0; c < totalCols; ++c) {
        ImGui::TableSetColumnIndex(c);
        const bool isGutter = (c == sdCol || c == callItmCol || c == putItmCol || c == endCol);
        if (!isGutter)   // gutters keep the default row background (no green/red)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                   c == strikeCol ? kStrikeHdrBg
                                                  : (c < strikeCol ? kCallsHdrBg : kPutsHdrBg));
        if (c == callsMid)          ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.58f, 1.0f), "CALLS");
        else if (c == strikeCol) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float tw = ImGui::CalcTextSize("STRIKE").x;
            if (avail > tw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
            ImGui::TextColored(ImVec4(0.80f, 0.82f, 0.88f, 1.0f), "STRIKE");
        }
        else if (c == putsMid)      ImGui::TextColored(ImVec4(0.92f, 0.48f, 0.48f, 1.0f), "PUTS");
    }

    // ── Sub-header row ──────────────────────────────────────────────────────
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    int col = 1;   // 0 = SD label column, left blank
    auto hdr = [&](const char* label) {
        ImGui::TableSetColumnIndex(col);
        // Centered, matching the centered cell values. (The Headers row supplies
        // its own background, so plain text still reads as a header.)
        const float avail = ImGui::GetContentRegionAvail().x;
        const float tw    = ImGui::CalcTextSize(label).x;
        if (avail > tw)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
        ImGui::TextUnformatted(label);
        ++col;
    };
    if (m_showVega)   hdr("Vega");
    if (m_showTheta)  hdr("Theta");
    if (m_showGamma)  hdr("Gamma");
    if (m_showIv)     hdr("IV");
    if (m_showOi)     hdr("OI");
    if (m_showVolume) hdr("Vol");
    if (m_showLast)   hdr("Last");
    if (m_showDelta)  hdr("Delta");
    hdr("Bid"); hdr("Ask");
    ++col;   // skip call-ITM column
    // Strike column header, centered (TableHeader would left-align it).
    {
        ImGui::TableSetColumnIndex(col);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float tw = ImGui::CalcTextSize("Price").x;
        if (avail > tw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
        ImGui::TextUnformatted("Price");
        ++col;
    }
    ++col;   // skip put-ITM column
    hdr("Bid"); hdr("Ask");
    if (m_showDelta)  hdr("Delta");
    if (m_showLast)   hdr("Last");
    if (m_showVolume) hdr("Vol");
    if (m_showOi)     hdr("OI");
    if (m_showIv)     hdr("IV");
    if (m_showGamma)  hdr("Gamma");
    if (m_showTheta)  hdr("Theta");
    if (m_showVega)   hdr("Vega");

    // Y positions of the rules we overlay after EndTable (drawing inside the
    // table would be clipped by the cell the cursor happens to be in).
    struct Rule { float y; ImU32 col; bool dashed; const char* label; };
    std::vector<Rule> rules;
    const double spot  = m_underlyingPrice;
    const double sigma = m_expectedMove;      // 0 until greeks arrive

    // Captured while rendering, consumed after EndTable to anchor the ITM
    // badges at the at-the-money boundary (tastytrade style).
    float spotRuleY   = -1.0f;   // screen-y of the spot crossing
    float strikeColX0 = -1.0f;   // strike column left / right screen-x
    float strikeColX1 = -1.0f;
    // Label-column screen-x spans, captured on the first rendered row so the
    // overlay pills/badges land inside their own columns.
    float sdColX0 = -1.0f, sdColX1 = -1.0f;
    float callItmColX0 = -1.0f, callItmColX1 = -1.0f;
    float putItmColX0  = -1.0f, putItmColX1  = -1.0f;
    float endColX0 = -1.0f, endColX1 = -1.0f;   // trailing-margin right edge = content width
    auto captureCol = [](float& x0, float& x1) {
        if (x0 >= 0.0f) return;
        // Full column span (content region ± cell padding), so the pill/badge
        // size tracks the column width and not the padding-shrunk content.
        const float pad = ImGui::GetStyle().CellPadding.x;
        const float cl  = ImGui::GetCursorScreenPos().x;
        x0 = cl - pad;
        x1 = cl + ImGui::GetContentRegionAvail().x + pad;
    };

    const std::string& curExpiry =
        (m_expiryIdx >= 0 && m_expiryIdx < (int)m_meta.expirations.size())
            ? m_meta.expirations[(std::size_t)m_expiryIdx] : std::string();

    // Strikes IB has confirmed dead for this expiry (both legs came back 200)
    // are hidden entirely — see the render loop.
    auto isHidden = [&](double strike) {
        if (curExpiry.empty()) return false;
        core::OptionContractKey ck{ m_symbol, curExpiry, strike, 'C' };
        core::OptionContractKey pk{ m_symbol, curExpiry, strike, 'P' };
        return m_deadContracts.count(DeadKey(ck)) &&
               m_deadContracts.count(DeadKey(pk));
    };

    // Track which strike rows are actually on screen this pass; SyncSubscriptions
    // streams that span so scrolling a long "ALL" ladder loads the visible rows.
    const float rowH = ImGui::GetFrameHeight();
    int visLo = INT_MAX, visHi = -1;

    // Signed held-qty pill (Phase 2), centered in the current (ITM gutter) cell:
    // green +N for a long leg, red -N for a short leg. Drawn in-cell so it clips
    // with scroll; the tooltip surfaces avg cost for "what to close and at what
    // qty". Small enough to sit in the narrow ITM column beside its strike.
    auto drawQtyPill = [&](const HeldLeg* h, int idSalt) {
        if (!h) return;
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%+lld", (long long)std::llround(h->qty));
        const bool  lng = h->qty > 0.0;
        const ImU32 bg  = lng ? IM_COL32(38, 148, 74, 240) : IM_COL32(200, 62, 62, 240);
        const ImU32 ink = IM_COL32(255, 255, 255, 255);
        ImFont* f = ImGui::GetFont();
        const float fs  = ImGui::GetFontSize() * 0.85f;
        const float tw  = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf).x;
        const float px  = em(2.5f), py = em(1.0f);
        const float pillW  = tw + px * 2.0f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const ImVec2 cur   = ImGui::GetCursorScreenPos();
        const float  cy    = cur.y + rowH * 0.5f;
        float x0 = cur.x + (availW - pillW) * 0.5f;
        if (x0 < cur.x) x0 = cur.x;
        const ImVec2 p0(x0, cy - fs * 0.5f - py);
        const ImVec2 p1(x0 + pillW, cy + fs * 0.5f + py);
        pdl->AddRectFilled(p0, p1, bg, em(3));
        pdl->AddText(f, fs, ImVec2(p0.x + px, cy - fs * 0.5f), ink, buf);
        // Invisible hover target over the pill for an avg-cost tooltip.
        ImGui::PushID(idSalt);
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y));
        ImGui::InvisibleButton("##pos", ImVec2(pillW, p1.y - p0.y));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s %d @ $%.2f", lng ? "Long" : "Short",
                              (int)std::llround(std::abs(h->qty)), h->avgCost);
        ImGui::PopID();
    };

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
            // Spot is not a full-width rule; it renders as a '<' marker at the
            // strike cell's right border (drawn after EndTable). Just capture y.
            if (crosses(spot)) spotRuleY = rowTop;
            if (sigma > 0.0) {
                if (crosses(spot - sigma))       rules.push_back({rowTop, kSigma1Col, false, "-1 SD"});
                if (crosses(spot + sigma))       rules.push_back({rowTop, kSigma1Col, false, "+1 SD"});
                if (crosses(spot - 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "-2 SD"});
                if (crosses(spot + 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "+2 SD"});
            }
        }

        // In-the-money shading: calls ITM below spot, puts ITM above.
        const bool callItm = spot > 0.0 && strike < spot;
        const bool putItm  = spot > 0.0 && strike > spot;

        int c = 0;

        // SD label column (leftmost): a normal, empty cell — the SD pill draws
        // over it in the overlay pass. No bg override, so it keeps the row stripe.
        ImGui::TableSetColumnIndex(c++);
        captureCol(sdColX0, sdColX1);

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
            if (itm)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
            char buf[24];
            if (have) std::snprintf(buf, sizeof(buf), fmt, v);
            else      std::snprintf(buf, sizeof(buf), "-");
            const float avail = ImGui::GetContentRegionAvail().x;
            const float tw    = ImGui::CalcTextSize(buf).x;
            if (avail > tw)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
            if (have) ImGui::TextUnformatted(buf);
            else      ImGui::TextColored(kDim, "-");
        };

        // Is this cell a staged ticket leg? Returns 1 for a buy leg (green
        // border), 2 for a sell leg (red border), 0 otherwise. A staged buy
        // corresponds to the ask cell (you buy by hitting the ask); a sell to
        // the bid. All rendered rows share the current expiry, so matching on
        // strike + right + side is sufficient.
        auto stagedLeg = [&](double strk, char right, bool isAsk) -> int {
            // A staged BUY corresponds to the ask cell (you buy by hitting the
            // ask), a SELL to the bid. Scan the cart for a matching leg.
            for (const TicketLeg& L : m_legs) {
                if (L.key.strike == strk && L.key.right == right && L.buy == isAsk)
                    return L.buy ? 1 : 2;
            }
            return 0;
        };

        // Clickable bid/ask. Convention follows the platform: clicking the ask
        // buys, clicking the bid sells — you act on the side you can hit.
        auto priceCell = [&](bool itm, ImU32 tint, const core::OptionQuote* q,
                             bool isAsk, char right) {
            ImGui::TableSetColumnIndex(c++);
            if (itm)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
            const double v = q ? (isAsk ? q->ask : q->bid) : 0.0;
            ImGui::PushID(i * 4 + (isAsk ? 1 : 0) + (right == 'P' ? 2 : 0));
            if (v > 0.0) {
                char lbl[24];
                std::snprintf(lbl, sizeof(lbl), "%.2f", v);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.0f));
                if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    core::OptionContractKey k = key;
                    k.right = right;
                    AddOrToggleLeg(k, /*buy=*/isAsk);
                }
                ImGui::PopStyleVar();
            } else {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float tw    = ImGui::CalcTextSize("-").x;
                if (avail > tw)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
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

        // Call-ITM label column: empty except a held-position pill for the call.
        ImGui::TableSetColumnIndex(c++);
        captureCol(callItmColX0, callItmColX1);
        drawQtyPill(HeldFor(curExpiry, strike, 'C'), i * 2);

        ImGui::TableSetColumnIndex(c++);
        // Capture the full strike-cell span (cursor + content width) before the text.
        const float cellAvail = ImGui::GetContentRegionAvail().x;
        if (strikeColX0 < 0.0f) {
            strikeColX0 = ImGui::GetCursorScreenPos().x;
            strikeColX1 = strikeColX0 + cellAvail;
        }
        char sbuf[16];
        std::snprintf(sbuf, sizeof(sbuf), "%.2f", strike);
        const float sbufW = ImGui::CalcTextSize(sbuf).x;
        if (cellAvail > sbufW)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellAvail - sbufW) * 0.5f);
        ImGui::TextUnformatted(sbuf);

        // Put-ITM label column: empty except a held-position pill for the put.
        ImGui::TableSetColumnIndex(c++);
        captureCol(putItmColX0, putItmColX1);
        drawQtyPill(HeldFor(curExpiry, strike, 'P'), i * 2 + 1);

        side('P', putItm, kPutItmBg, false);

        // Trailing END margin column (empty, normal cell); capture its right edge
        // so we know the true content width for next frame's outer size.
        ImGui::TableSetColumnIndex(c);
        captureCol(endColX0, endColX1);
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();   // CellPadding pushed before BeginTable
    // Must read the table's rect AFTER EndTable — that is when the table is
    // submitted as an item. Before EndTable, GetItemRect* returns the last
    // *cell*, which collapses tblMin/tblMax to a sliver and made the guard
    // below reject every spot/sigma rule (so no lines ever drew).
    const ImVec2 tblMin = ImGui::GetItemRectMin();
    const ImVec2 tblMax = ImGui::GetItemRectMax();

    // Publish the on-screen span for SyncSubscriptions (same frame).
    if (visHi >= 0) { m_renderVisLo = visLo; m_renderVisHi = visHi; }
    else            { m_renderVisLo = m_renderVisHi = -1; }

    // Overlay the spot / sigma rules across the table width.
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Scaled-size text for the compact labels: SD pill at 75%, ITM badge at 85%.
    // No bold face is loaded, so fake-bold by double-striking one pixel apart.
    constexpr float kSdScale  = 0.75f;
    constexpr float kItmScale = 1.0f;
    const float sdFs  = ImGui::GetFontSize() * kSdScale;
    const float itmFs = ImGui::GetFontSize() * kItmScale;
    auto boldText = [&](ImVec2 p, ImU32 col, const char* txt, float sz) {
        ImFont* f = ImGui::GetFont();
        dl->AddText(f, sz, p, col, txt);
        dl->AddText(f, sz, ImVec2(p.x + 1.0f, p.y), col, txt);
    };

    // Draw the SD/ATM rules across the DATA columns only — not over the SD label
    // column (left) or the empty END margin column (right).
    const float lineL = (sdColX1 > 0.0f)   ? sdColX1   : tblMin.x;
    const float lineR = (endColX0 > 0.0f)  ? endColX0  : tblMax.x;

    for (const Rule& ru : rules) {
        if (ru.y < tblMin.y || ru.y > tblMax.y) continue;   // scrolled out of view
        if (ru.dashed) DashedHLine(dl, lineL, lineR, ru.y, ru.col);
        else           dl->AddLine(ImVec2(lineL, ru.y), ImVec2(lineR, ru.y), ru.col, 1.2f);
        // Solid azure pill with bold inverse (dark) 75%-size text, centered in
        // the SD col.
        const float tw = ImGui::CalcTextSize(ru.label).x * kSdScale;
        const float px = em(3.0f), py = em(1.5f), pillW = tw + px * 2.0f;
        const float cx = (sdColX0 >= 0.0f)
                            ? sdColX0 + ((sdColX1 - sdColX0) - pillW) * 0.5f
                            : tblMin.x + em(4);
        const ImVec2 p0(cx, ru.y - sdFs * 0.5f - py);
        const ImVec2 p1(cx + pillW, ru.y + sdFs * 0.5f + py);
        dl->AddRectFilled(p0, p1, kSdPill, em(2));
        boldText(ImVec2(p0.x + px, ru.y - sdFs * 0.5f), kSdInk, ru.label, sdFs);
    }

    // ITM badges straddling the at-the-money line (tastytrade style): ▲ ITM on
    // the calls side, ▼ ITM on the puts side. Calls are ITM above the spot line
    // (lower strikes), puts below it — the arrows point into each ITM region.
    if (spotRuleY > tblMin.y && spotRuleY < tblMax.y &&
        callItmColX0 > 0.0f && putItmColX0 > 0.0f) {
        // Yellow ATM boundary line. The badges straddle the line (^ITM above,
        // vITM below), so the line runs continuously under/over them — the only
        // gap is the strike column (which holds the red '<' spot marker).
        const ImU32 atmLine = IM_COL32(212, 190, 60, 220);
        dl->AddLine(ImVec2(lineL, spotRuleY),
                    ImVec2(strikeColX0 - em(2), spotRuleY), atmLine, em(1.5f));
        dl->AddLine(ImVec2(strikeColX1 + em(2), spotRuleY),
                    ImVec2(lineR, spotRuleY), atmLine, em(1.5f));

        // Full-size badge sized to its content (triangle + "ITM"); the column is
        // sized to match, so it hugs the badge like the SD pill.
        auto itmBadge = [&](float colX0, float colX1, float cy, bool up) {
            (void)colX1;
            const float h = em(15);
            const float badgeW = em(13) + ImGui::CalcTextSize("ITM").x * kItmScale + em(4);
            const ImVec2 a(colX0 + em(1), cy - h * 0.5f);
            const ImVec2 b(a.x + badgeW, cy + h * 0.5f);
            dl->AddRectFilled(a, b, IM_COL32(212, 175, 55, 235), em(3));
            const float cx = a.x + em(6), t = em(3.3f);
            const ImU32 ink = IM_COL32(20, 20, 20, 255);
            if (up) dl->AddTriangleFilled(ImVec2(cx - t, cy + t * 0.7f),
                                          ImVec2(cx + t, cy + t * 0.7f),
                                          ImVec2(cx,     cy - t), ink);
            else    dl->AddTriangleFilled(ImVec2(cx - t, cy - t * 0.7f),
                                          ImVec2(cx + t, cy - t * 0.7f),
                                          ImVec2(cx,     cy + t), ink);
            boldText(ImVec2(a.x + em(13), cy - itmFs * 0.5f), ink, "ITM", itmFs);
        };
        // Calls ^ ITM sits just above the line, puts v ITM just below it.
        itmBadge(callItmColX0, callItmColX1, spotRuleY - rowH * 0.5f, /*up=*/true);
        itmBadge(putItmColX0,  putItmColX1,  spotRuleY + rowH * 0.5f, /*up=*/false);

        // Spot marker: a red '<' at the strike cell's right border (no label).
        const float sx = strikeColX1, sy = spotRuleY, s = em(5);
        const ImU32 spotMark = IM_COL32(230, 70, 70, 255);
        dl->AddLine(ImVec2(sx + s, sy - s), ImVec2(sx, sy), spotMark, em(2));
        dl->AddLine(ImVec2(sx, sy), ImVec2(sx + s, sy + s), spotMark, em(2));
    }
}

// ── Order ticket ─────────────────────────────────────────────────────────────

void OptionsChainWindow::AddOrToggleLeg(const core::OptionContractKey& key, bool buy) {
    // Toggle: clicking the same (strike, right, side) again removes that leg.
    for (size_t i = 0; i < m_legs.size(); ++i) {
        const TicketLeg& L = m_legs[i];
        if (L.key.expiry == key.expiry && L.key.strike == key.strike &&
            L.key.right == key.right && L.buy == buy) {
            RemoveLeg((int)i);
            return;
        }
    }
    // Phase A: every option leg shares one expiry (stock legs are exempt).
    for (const TicketLeg& L : m_legs) {
        if (!L.stock && key.expiry != L.key.expiry) {
            m_status = "All option legs must share the same expiry (Phase A).";
            return;
        }
    }
    if ((int)m_legs.size() >= kMaxLegs) {
        m_status = "Max " + std::to_string(kMaxLegs) + " legs per combo.";
        return;
    }

    TicketLeg leg;
    leg.key = key;
    leg.buy = buy;
    m_legs.push_back(leg);
    m_ticketActive = true;
    if (m_ticketQty < 1) m_ticketQty = 1;

    // Default limit to the mid: single leg → its per-contract mid (positive
    // premium); combo → signed net (debit+/credit-). A ticket should never
    // default to crossing the spread.
    const double dflt = isCombo() ? NetMid() : LegMid(m_legs[0]);
    m_ticketLimit = core::services::RoundToTick(dflt, 0.01);
    if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;

    ResolveLegConIds();
    RecomputeTicketMetrics();
}

void OptionsChainWindow::AddOrToggleStockLeg(bool buy) {
    // Toggle off a same-side equity leg.
    for (size_t i = 0; i < m_legs.size(); ++i) {
        if (m_legs[i].stock && m_legs[i].buy == buy) { RemoveLeg((int)i); return; }
    }
    if (m_underlyingConId <= 0) {
        m_status = "Underlying not resolved yet — load the chain first.";
        return;
    }
    if ((int)m_legs.size() >= kMaxLegs) {
        m_status = "Max " + std::to_string(kMaxLegs) + " legs per combo.";
        return;
    }
    TicketLeg leg;
    leg.stock = true;
    leg.buy   = buy;
    leg.ratio = 100;                 // 100 shares per option contract
    leg.conId = m_underlyingConId;   // already resolved; no round-trip
    m_legs.push_back(leg);
    m_ticketActive = true;
    if (m_ticketQty < 1) m_ticketQty = 1;
    m_ticketLimit = core::services::RoundToTick(
        isCombo() ? NetMid() : LegMid(m_legs[0]), 0.01);
    if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
    ResolveLegConIds();
    RecomputeTicketMetrics();
}

void OptionsChainWindow::RemoveLeg(int idx) {
    if (idx < 0 || idx >= (int)m_legs.size()) return;
    m_legs.erase(m_legs.begin() + idx);
    if (m_legs.empty()) {
        m_ticketActive = false;
        m_ticketMetrics = core::services::StrategyMetrics{};
        return;
    }
    // reqIds are index-based, so a removal shifts every following leg — re-resolve.
    ResolveLegConIds();
    m_ticketLimit = core::services::RoundToTick(
        isCombo() ? NetMid() : LegMid(m_legs[0]), 0.01);
    if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
    RecomputeTicketMetrics();
}

double OptionsChainWindow::LegMid(const TicketLeg& L) const {
    if (L.stock) {
        const double m = core::services::QuoteMid(m_underlyingBid, m_underlyingAsk);
        return m > 0.0 ? m : m_underlyingPrice;
    }
    const core::OptionQuote* q = FindQuote(L.key);
    return q ? core::services::QuoteMid(q->bid, q->ask) : 0.0;
}

double OptionsChainWindow::NetMid() const {
    // Signed per-share net (matches TWS's buy-write convention): options are
    // priced per share (×multiplier applied by IB), the equity leg per share
    // with its ratio normalised by the multiplier so 100 shares == one contract.
    const double mult = m_meta.multiplier.empty() ? 100.0
                                                  : std::atof(m_meta.multiplier.c_str());
    const double m = mult > 0.0 ? mult : 100.0;
    double net = 0.0;
    for (const TicketLeg& L : m_legs) {
        const double eff = L.stock ? (L.ratio / m) : (double)L.ratio;
        net += (L.buy ? 1.0 : -1.0) * eff * LegMid(L);
    }
    return net;
}

void OptionsChainWindow::ResolveLegConIds() {
    // Option legs resolve their conId via reqContractDetails; the equity leg
    // already carries the resolved underlying conId, so skip it.
    if (!OnReqOptionLegConId) return;
    for (size_t i = 0; i < m_legs.size(); ++i) {
        if (m_legs[i].stock) continue;
        m_legs[i].conId = 0;
        OnReqOptionLegConId(kLegConIdBase + (int)i, m_legs[i].key);
    }
}

void OptionsChainWindow::OnLegConId(int reqId, const std::string& expiry,
                                    double strike, const std::string& right,
                                    long conId) {
    if (conId <= 0 || right.empty()) return;
    const int idx = reqId - kLegConIdBase;
    if (idx < 0 || idx >= (int)m_legs.size()) return;
    TicketLeg& L = m_legs[idx];
    if (L.key.expiry == expiry && L.key.strike == strike && L.key.right == right[0])
        L.conId = conId;
}

void OptionsChainWindow::RecomputeTicketMetrics() {
    m_ticketMetrics = core::services::StrategyMetrics{};
    if (m_legs.empty()) return;

    const int    qty  = m_ticketQty > 0 ? m_ticketQty : 1;
    const double mult = m_meta.multiplier.empty()
                            ? 100.0 : std::atof(m_meta.multiplier.c_str());
    const bool   combo = isCombo();

    std::vector<core::services::StrategyLeg> legs;
    legs.reserve(m_legs.size());
    for (const TicketLeg& L : m_legs) {
        core::services::StrategyLeg leg;
        leg.ratio = (L.buy ? 1 : -1) * L.ratio * qty;
        if (L.stock) {
            // Equity leg: linear payoff, greeks/strike/right irrelevant. Its
            // share price still feeds the net (below); the engine derives its
            // slope from ratio/multiplier.
            leg.stock = true;
            legs.push_back(leg);
            continue;
        }
        const core::OptionQuote* q = FindQuote(L.key);
        leg.strike = L.key.strike;
        leg.right  = L.key.right;
        // Combo: per-leg mids so extrinsic / greeks are real, with the user's
        // net entered as the total premium. Single leg: the user's limit is the
        // premium for the one contract.
        leg.price  = combo ? LegMid(L) : m_ticketLimit;
        if (q) { leg.delta = q->delta; leg.theta = q->theta; }
        legs.push_back(leg);
    }

    // Net premium from the account's perspective (debit+/credit-). Combo: the
    // signed net the user entered. Single: signed by its own side.
    const double netPrice = combo
        ? m_ticketLimit * qty
        : (m_legs[0].buy ? 1.0 : -1.0) * m_ticketLimit * qty;

    m_ticketMetrics = core::services::ComputeStrategyMetrics(
        legs, netPrice, mult > 0.0 ? mult : 100.0, m_underlyingPrice);
}

void OptionsChainWindow::BuildAnalysisInput(StrategyAnalysisWindow::Input& out) const {
    out = StrategyAnalysisWindow::Input{};
    if (m_legs.empty()) return;

    const int    qty  = m_ticketQty > 0 ? m_ticketQty : 1;
    const double mult = m_meta.multiplier.empty()
                            ? 100.0 : std::atof(m_meta.multiplier.c_str());
    const bool   combo = isCombo();

    // Same leg vector + net convention RecomputeTicketMetrics uses, so the graph
    // and the ticket strip agree exactly.
    for (const TicketLeg& L : m_legs) {
        core::services::StrategyLeg leg;
        leg.ratio = (L.buy ? 1 : -1) * L.ratio * qty;
        if (L.stock) { leg.stock = true; out.legs.push_back(leg); continue; }
        const core::OptionQuote* q = FindQuote(L.key);
        leg.strike = L.key.strike;
        leg.right  = L.key.right;
        leg.price  = combo ? LegMid(L) : m_ticketLimit;
        if (q) { leg.delta = q->delta; leg.theta = q->theta; leg.iv = q->impliedVol; }
        // Per-leg days-to-expiry from the leg's own expiry string (all option
        // legs share one expiry today, but keep it per-leg for cross-expiry).
        for (int ei = 0; ei < (int)m_meta.expirations.size(); ++ei) {
            if (m_meta.expirations[(std::size_t)ei] == L.key.expiry) {
                leg.dte = (double)std::max(0, DaysToExpiry(ei));
                break;
            }
        }
        out.legs.push_back(leg);
        out.strikes.push_back(L.key.strike);
    }
    std::sort(out.strikes.begin(), out.strikes.end());
    out.strikes.erase(std::unique(out.strikes.begin(), out.strikes.end()),
                      out.strikes.end());

    out.netPrice   = combo ? m_ticketLimit * qty
                           : (m_legs[0].buy ? 1.0 : -1.0) * m_ticketLimit * qty;
    out.multiplier = mult > 0.0 ? mult : 100.0;
    out.spot       = m_underlyingPrice;
    out.qty        = qty;
    out.symbol     = m_symbol;
    out.metrics    = m_ticketMetrics;

    // Compact one-line summary: "<N legs> · <net> db/cr".
    char sum[96];
    const double net = out.netPrice;
    std::snprintf(sum, sizeof(sum), "%s · %d leg%s · %.2f %s", m_symbol.c_str(),
                  (int)m_legs.size(), m_legs.size() == 1 ? "" : "s",
                  std::abs(net), net >= 0 ? "db" : "cr");
    out.summary = sum;
    out.valid   = true;
}

float OptionsChainWindow::kTicketBandHeight() const {
    // Two-column band: legs table on the left, order controls on the right.
    // Height is driven by the taller column. The left grows with a spread
    // (header + 2 legs + synthetic quote + "legs ready"); the right holds the
    // inputs / price anchors / stats / actions, which can wrap on a narrow
    // window. Reserve generously so the Send / Clear row is never trimmed.
    // Left column: header + one row per leg + (combo: net-quote row + status).
    const float leftLines = 1.0f + (float)m_legs.size() + (isCombo() ? 2.0f : 0.0f);
    const float lines = std::max(5.0f, leftLines) + 0.5f;
    return ImGui::GetFrameHeightWithSpacing() * lines + em(16);
}

void OptionsChainWindow::DrawOrderTicket() {
    if (!m_ticketActive) return;

    ImGui::Separator();
    // Fixed band pinned below the table; scrolls internally if it wraps.
    ImGui::BeginChild("##opt_ticket", ImVec2(0.0f, kTicketBandHeight() - em(6)),
                      ImGuiChildFlags_None);

    const core::OptionQuote* q = m_legs.empty() ? nullptr : FindQuote(m_legs[0].key);

    // ── Left column: legs ─────────────────────────────────────────────────────
    // Split the band roughly at the chain's strike column: the chain is a
    // centered mirrored layout (calls | strike | puts), so ~50% of the band
    // width puts the legs table under the calls and the order form under the
    // puts (matching the design). Clamp so the order column always keeps room
    // for its actions row and the legs table stays usable on a narrow window.
    const float kBandAvail = ImGui::GetContentRegionAvail().x;
    const float kLegsMax   = std::max(em(320), kBandAvail - em(400));
    const float kLegsColW  = std::clamp(kBandAvail * 0.5f - em(10),
                                        em(320), kLegsMax);
    ImGui::BeginChild("##opt_ticket_legs_col", ImVec2(kLegsColW, 0.0f),
                      ImGuiChildFlags_None);

    // ── Legs table ────────────────────────────────────────────────────────────
    // One row per leg: # | Symbol | Action | Expiry | Strike | Side | Ratio |
    // Bid | Ask | (× remove). Ratio is editable per leg; × drops the leg.
    int removeIdx = -1;   // deferred so we don't mutate m_legs mid-render
    {
        const ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##opt_ticket_legs", 10, tf)) {
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, em(24));
            ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthFixed, em(58));
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, em(46));
            ImGui::TableSetupColumn("Expiry", ImGuiTableColumnFlags_WidthFixed, em(82));
            ImGui::TableSetupColumn("Strike", ImGuiTableColumnFlags_WidthFixed, em(58));
            ImGui::TableSetupColumn("Side",   ImGuiTableColumnFlags_WidthFixed, em(34));
            ImGui::TableSetupColumn("Ratio",  ImGuiTableColumnFlags_WidthFixed, em(104));
            ImGui::TableSetupColumn("Bid",    ImGuiTableColumnFlags_WidthFixed, em(52));
            ImGui::TableSetupColumn("Ask",    ImGuiTableColumnFlags_WidthFixed, em(52));
            ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed, em(26));
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)m_legs.size(); ++i) {
                TicketLeg& L = m_legs[i];
                const double bid = L.stock ? m_underlyingBid
                                           : (FindQuote(L.key) ? FindQuote(L.key)->bid : 0.0);
                const double ask = L.stock ? m_underlyingAsk
                                           : (FindQuote(L.key) ? FindQuote(L.key)->ask : 0.0);
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(kDim, "%d", i + 1);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(m_symbol.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(L.buy ? kUp : kDown, "%s", L.buy ? "BUY" : "SELL");
                ImGui::TableSetColumnIndex(3);
                if (L.stock) ImGui::TextColored(kDim, "STOCK");
                else         ImGui::TextUnformatted(L.key.expiry.c_str());
                ImGui::TableSetColumnIndex(4);
                if (L.stock) ImGui::TextColored(kDim, "shares");
                else         ImGui::Text("%.2f", L.key.strike);
                ImGui::TableSetColumnIndex(5);
                if (L.stock) ImGui::TextColored(kDim, "-");
                else         ImGui::Text("%c", L.key.right);
                ImGui::TableSetColumnIndex(6);
                // Wide enough for a 3-digit stock ratio (100) plus the +/- step
                // buttons; option ratios are single-digit but share the column.
                ImGui::SetNextItemWidth(em(100));
                if (ImGui::InputInt("##ratio", &L.ratio, L.stock ? 100 : 1, 0)) {
                    if (L.ratio < 1) L.ratio = 1;
                    m_ticketLimit = core::services::RoundToTick(
                        isCombo() ? NetMid() : LegMid(m_legs[0]), 0.01);
                    if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
                    RecomputeTicketMetrics();
                }
                ImGui::TableSetColumnIndex(7);
                if (bid > 0.0) ImGui::Text("%.2f", bid);
                else           ImGui::TextColored(kDim, "-");
                ImGui::TableSetColumnIndex(8);
                if (ask > 0.0) ImGui::Text("%.2f", ask);
                else           ImGui::TextColored(kDim, "-");
                ImGui::TableSetColumnIndex(9);
                if (ImGui::SmallButton("x")) removeIdx = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove leg");
                ImGui::PopID();
            }

            // Synthetic combo quote row (BAG has no NBBO). Net bid (passive net
            // you could rest at) = Σ ratio·(buy:+bid, sell:−ask); net ask
            // (marketable now) = Σ ratio·(buy:+ask, sell:−bid). Signed + debit /
            // − credit. Both clickable → send that net into the Net field.
            if (isCombo()) {
                const double mult = m_meta.multiplier.empty() ? 100.0
                                        : std::atof(m_meta.multiplier.c_str());
                const double mm = mult > 0.0 ? mult : 100.0;
                bool   have   = true;
                double netBid = 0.0, netAsk = 0.0;
                for (const TicketLeg& L : m_legs) {
                    double bid, ask;
                    if (L.stock) { bid = m_underlyingBid; ask = m_underlyingAsk; }
                    else {
                        const core::OptionQuote* qq = FindQuote(L.key);
                        bid = qq ? qq->bid : 0.0;
                        ask = qq ? qq->ask : 0.0;
                    }
                    if (bid <= 0.0 || ask <= 0.0) { have = false; break; }
                    const double eff = L.stock ? (L.ratio / mm) : (double)L.ratio;
                    netBid += eff * (L.buy ? bid : -ask);
                    netAsk += eff * (L.buy ? ask : -bid);
                }
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
                ImGui::TextColored(kDim, "Net");
                netCell(7, "spr_nb", netBid, "Net bid (passive) \xe2\x86\x92 Net");
                netCell(8, "spr_na", netAsk, "Net ask (marketable) \xe2\x86\x92 Net");
            }
            ImGui::EndTable();
        }

        if (isCombo()) {
            bool resolved = true;
            for (const TicketLeg& L : m_legs) if (L.conId <= 0) { resolved = false; break; }
            ImGui::TextColored(resolved ? kUp : kDim, "%s",
                               resolved ? "legs ready" : "resolving legs…");
        }
    }
    if (removeIdx >= 0) RemoveLeg(removeIdx);

    ImGui::EndChild();   // left column

    // ── Right column: order controls ──────────────────────────────────────────
    // Right-anchor the order box against the band's right edge (under the puts):
    // give it a fixed content width and place it so its right border meets the
    // band edge, leaving the empty gutter in the middle (under the strike area)
    // rather than trailing to the right of the controls. On a narrow band it
    // collapses to "just right of the legs" so nothing overlaps.
    const float kOrderW = std::min(em(460), kBandAvail - kLegsColW - em(20));
    ImGui::SameLine(kBandAvail - kOrderW);
    ImGui::BeginChild("##opt_ticket_order_col", ImVec2(kOrderW, 0.0f),
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
        ImGui::TextColored(kDim, isCombo() ? "Net" : "Limit");
        row.item(em(80));
        ImGui::SetNextItemWidth(em(80));
        if (ImGui::InputDouble("##opt_lmt", &m_ticketLimit, 0.0, 0.0, "%.2f")) {
            // A single leg is always paid/received as a positive premium; a
            // combo's net can be a credit (negative), so only clamp single legs.
            if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
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
                if (!isCombo() && m_ticketLimit < 0.0) m_ticketLimit = 0.0;
                RecomputeTicketMetrics();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use as limit price");
        };

        if (isCombo()) {
            const double net = NetMid();
            priceBtn(net >= 0 ? "net mid (debit)" : "net mid (credit)", net);
        } else if (q) {
            // bid | mid | ask, annotated by which side is marketable for this
            // action: buying crosses the ask (nat), selling crosses the bid.
            const bool buy = m_legs[0].buy;
            priceBtn(buy ? "bid (opp)" : "bid (nat)", q->bid);
            priceBtn("mid", core::services::QuoteMid(q->bid, q->ask));
            priceBtn(buy ? "ask (nat)" : "ask (opp)", q->ask);
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
        // Single leg: needs a positive premium. Combo: needs every leg conId
        // resolved (the net may legitimately be a credit, i.e. negative/zero).
        bool hasOption = false;
        for (const TicketLeg& L : m_legs) if (!L.stock) { hasOption = true; break; }
        bool priced;
        if (!hasOption) {
            priced = false;   // a lone equity leg isn't an option strategy
        } else if (isCombo()) {
            priced = true;    // every leg (incl. the equity leg) needs a conId
            for (const TicketLeg& L : m_legs) if (L.conId <= 0) { priced = false; break; }
        } else {
            priced = (m_ticketLimit > 0.0);
        }
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

            if (isCombo()) {
                // A BAG combo: the order buys the strategy at the net (positive =
                // debit, negative = credit); each leg carries its own BUY/SELL +
                // ratio.
                o.side          = core::OrderSide::Buy;
                o.spec.secType  = "BAG";
                for (const TicketLeg& L : m_legs)
                    o.spec.comboLegs.push_back(
                        { L.conId, L.ratio, L.buy ? "BUY" : "SELL", "SMART" });
            } else {
                const TicketLeg& L = m_legs[0];
                o.side          = L.buy ? core::OrderSide::Buy : core::OrderSide::Sell;
                o.spec.secType  = "OPT";
                o.spec.lastTradeDateOrContractMonth = L.key.expiry;
                o.spec.strike   = L.key.strike;
                o.spec.right    = std::string(1, L.key.right);
                // tradingClass deliberately omitted, same as the streaming path:
                // the merged class from the flattened chain can mismatch a
                // contract and IB rejects it with error 200. IB resolves the
                // standard class from symbol+expiry+strike+right.
                o.spec.tradingClass = "";
            }

            m_pendingOrder = o;
            if (m_transmitInstantly) {
                if (OnOrderSubmit) OnOrderSubmit(m_pendingOrder);
                m_legs.clear();
                m_ticketActive = false;
            } else {
                m_showConfirm = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();

        row.item(FlexRow::buttonW("Clear"));
        if (ImGui::Button("Clear")) { m_legs.clear(); m_ticketActive = false; }

        // Open the payoff graph for the staged cart.
        row.item(FlexRow::buttonW("Analysis"));
        if (ImGui::Button("Analysis") && OnShowAnalysis) OnShowAnalysis();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open the P&L-at-expiry graph for this strategy.");

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
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "COMBO — %d legs", (int)m_legs.size());
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "%s", hdr);
        ImGui::Separator();
        ImGui::Text("%s  %s", o.symbol.c_str(),
                    m_legs.empty() ? "" : m_legs.front().key.expiry.c_str());
        for (const TicketLeg& L : m_legs) {
            if (L.stock)
                ImGui::TextColored(L.buy ? kUp : kDown, "%s %d shares",
                                   L.buy ? "BUY" : "SELL", L.ratio);
            else
                ImGui::TextColored(L.buy ? kUp : kDown, "%s %dx %.2f%c",
                                   L.buy ? "BUY" : "SELL", L.ratio, L.key.strike, L.key.right);
        }
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
        m_legs.clear();
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
    if (!m_open) {
        // Closing the window stops the stream — no point holding ~60 market-data
        // lines for a hidden chain. Reopening re-subscribes the visible rows.
        if (!m_quotes.empty()) CancelAll();
        return false;
    }

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
    else                           { DrawChainTable(); SyncSubscriptions(); }

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
    SetBool  (b, "OPT_EXP_1ROW",    m_expirySingleRow);
    SetInt   (b, "OPT_STRIKE_RANGE",m_strikeRange);
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
    m_expirySingleRow = GetBool(b, "OPT_EXP_1ROW", m_expirySingleRow);

    const std::string sym = GetString(b, "OPT_SYMBOL", "");
    if (!sym.empty()) {
        // Restore the symbol only — no IB traffic on apply. The user presses
        // Load Chain, exactly as the Scanner restores filters without scanning.
        m_symbol = sym;
        m_meta.symbol = sym;
        std::snprintf(m_symbolBuf, sizeof(m_symbolBuf), "%s", sym.c_str());
    }

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
