#pragma once

#include "imgui.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

#include "core/models/OptionData.h"
#include "core/models/OrderData.h"
#include "core/services/OptionChain.h"
#include <unordered_set>

#include "ui/SymbolSearch.h"

namespace core::services { struct StateBlock; }

namespace ui {

// ============================================================================
// OptionsChainWindow — expirations x strikes for one underlying
//
// Singleton (one per app). Deliberately not multi-instance: every open chain
// costs market-data lines, and the window is a lookup surface rather than
// something you tile.
//
// Layout is the conventional mirrored chain — Calls | Strike | Puts — one row
// per strike of the selected expiration, with the ATM strike highlighted.
//
// reqId layout (see architecture.md):
//   21000        — reqSecDefOptParams (cancel-before-reissue)
//   21001–21099  — option reqContractDetails (leg conId resolution)
//   22000–22999  — option market-data rotating pool
// ============================================================================
class OptionsChainWindow {
public:
    static constexpr int kSecDefReqId     = 21000;  // reqSecDefOptParams
    static constexpr int kUnderlyingCdId  = 21001;  // underlying reqContractDetails
    static constexpr int kUnderlyingMktId = 21002;  // underlying quote (ATM / expected move)

    OptionsChainWindow();

    bool  Render();
    bool& open() { return m_open; }

    // ── Symbol / group ──────────────────────────────────────────────────────
    void               SetSymbol(const std::string& sym);
    const std::string& symbol() const { return m_symbol; }
    void setGroupId(int id) { m_groupId = id; }
    [[nodiscard]] int  groupId() const { return m_groupId; }

    // ── Data in (routed from main.cpp) ──────────────────────────────────────
    // Underlying conId resolved via reqContractDetails — needed before the
    // chain definition can be requested at all.
    void OnUnderlyingConId(int conId);
    // Plain-parameter form on purpose: UI windows must not include
    // IBKRClient.h (see architecture.md "UI never calls IB API directly").
    // main.cpp adapts MsgSecDefOptParams into this call.
    void OnSecDefOptParams(int reqId, const std::string& tradingClass,
                           const std::string& multiplier, int underlyingConId,
                           const std::vector<std::string>& expirations,
                           const std::vector<double>& strikes);
    void OnSecDefOptParamsEnd(int reqId);

    // IB error routing (from main.cpp onError).
    void OnChainError(int code, const std::string& msg);   // reqSecDefOptParams
    void OnOptionError(int reqId, int code, const std::string& msg); // a subscription

    // Live underlying price, used for ATM detection and moneyness shading.
    void OnUnderlyingPrice(double last);
    void OnUnderlyingChange(double chg, double chgPct);

    // Option market data, routed by reqId (pool 22000-22999).
    void OnOptionPrice  (int reqId, int field, double price);
    void OnOptionSize   (int reqId, int field, double size);
    void OnOptionGeneric(int reqId, int tickType, double value);
    void OnOptionGreeks (int reqId, int tickType, double impliedVol, double delta,
                         double gamma, double vega, double theta, double undPrice);

    // Cancel every live option subscription (disconnect / window close / shutdown).
    void CancelAll();

    // ── Callbacks wired by main.cpp ─────────────────────────────────────────
    // Resolve the underlying's conId and start its quote; reqSecDefOptParams
    // cannot be issued without the conId.
    std::function<void(const std::string& sym)>               OnRequestUnderlying;
    std::function<void(int reqId, const std::string& sym,
                       int underlyingConId)>                  OnReqSecDefOptParams;
    std::function<void(const std::string& pattern)>           OnReqMatchingSymbols;
    std::function<void(const std::string& sym)>               OnBroadcastSymbol;

    // Subscription plumbing. The window decides *what* should be live; main.cpp
    // owns the reqId pool and the IB calls.
    std::function<int()>                                      OnAllocOptionReqId;
    std::function<void(int reqId, const core::OptionContractKey& key,
                       const std::string& tradingClass,
                       const std::string& multiplier)>        OnSubscribeOption;
    std::function<void(int reqId)>                            OnCancelOption;

    // ── Order submission ────────────────────────────────────────────────────
    // main.cpp stamps the account and calls PlaceOrder; the window never
    // touches IB directly.
    std::function<void(const core::Order&)>                   OnOrderSubmit;

    // ── State persistence ───────────────────────────────────────────────────
    void SerializeSettings(core::services::StateBlock& b) const;
    void ApplySettings    (const core::services::StateBlock& b);

private:
    void DrawToolbar();
    void DrawUnderlyingStrip();
    void DrawExpiryTabs();
    void DrawChainTable();
    void DrawLegend();
    void DrawEmptyState(const char* msg);
    void RequestChain();
    void DrawOrderTicket();
    void DrawConfirmPopup();

    // Stage a single-leg ticket from a clicked bid/ask cell.
    void StageTicket(const core::OptionContractKey& key, bool buy);
    // Recompute the ticket's payoff metrics from the staged leg + limit price.
    void RecomputeTicketMetrics();

    // Days to expiry for m_meta.expirations[idx]; -1 when unparseable.
    int  DaysToExpiry(int idx) const;

    // Strikes currently visible under the range filter, as [lo, hi] indices.
    core::services::StrikeRange VisibleStrikeRange() const;

    // Reconcile live subscriptions against what the table currently shows.
    void SyncSubscriptions();
    // Recompute IVX + expected move from the ATM contracts' implied vol.
    void RecomputeExpectedMove();

    core::OptionQuote*       FindQuote(const core::OptionContractKey& k);
    const core::OptionQuote* FindQuote(const core::OptionContractKey& k) const;
    core::OptionQuote*       QuoteForReqId(int reqId);

    bool        m_open   = true;
    int         m_groupId = 1;

    std::string m_symbol;
    char        m_symbolBuf[33] = {};
    SymbolSearchState m_symSearch;

    int         m_underlyingConId = 0;
    double      m_underlyingPrice = 0.0;

    core::OptionChainMeta m_meta;
    bool        m_loading      = false;   // chain definition in flight
    bool        m_chainLoaded  = false;
    std::string m_status;                 // one-line status / error text

    int         m_expiryIdx  = 0;         // index into m_meta.expirations
    int         m_strikeRange = 20;       // +/- N strikes around ATM; -1 = all

    // Underlying context strip (sketch: "SPY 450.12 +2.35 (+0.53%) | IVX 18.4%
    // | Expected Move +/-6.85 (1.52%)"). IVX is the underlying's ATM implied
    // vol; expected move is derived from it and days-to-expiry, so both stay
    // zero until option ticks arrive in Task D2.
    double      m_underlyingChange    = 0.0;
    double      m_underlyingChangePct = 0.0;
    double      m_ivx                 = 0.0;   // fraction, e.g. 0.184
    double      m_expectedMove        = 0.0;   // absolute dollars, 1 sigma
    bool        m_emWeighted          = false; // true = full weighting, false = 0.85 fallback

    // Auto: stream quotes for the visible strikes (no poll interval).
    bool        m_autoRefresh    = false;

    // Column visibility. Bid/Ask/Last are always shown; these are the
    // optional ones, mirroring the Scanner's column-toggle pattern.
    // Live quotes for whatever is currently subscribed, plus the reqId index
    // used to route ticks back. Keyed lookups go through FindQuote so the two
    // never drift.
    std::vector<core::OptionQuote>       m_quotes;
    std::unordered_map<int, std::size_t> m_reqIdToQuote;

    // Scroll debounce: a fast flick must not fire hundreds of subscribe/cancel
    // pairs, so the visible set has to settle before we act on it.
    double m_nextSyncAt   = 0.0;
    int    m_subscribedExpiryIdx = -1;   // expiry the live subs belong to

    // Contracts IB rejected (error 200): the flat strikes x flat expiries set
    // contains combos that do not trade, so a rejected key must not be
    // re-requested on the next debounce. Keyed "expiry|strike|right".
    std::unordered_set<std::string> m_deadContracts;
    static std::string DeadKey(const core::OptionContractKey& k);
    int    m_lastVisLo    = -1;
    int    m_lastVisHi    = -1;

    // Hard ceiling on concurrent option subscriptions, well under the typical
    // 100-line account limit so charts / DOM / watchlists keep working.
    static constexpr int kMaxOptionSubs = 60;

    // ── Order ticket (single leg; verticals land in Task F) ─────────────────
    bool                    m_ticketActive = false;
    core::OptionContractKey m_ticketKey;
    bool                    m_ticketBuy    = true;
    int                     m_ticketQty    = 1;
    double                  m_ticketLimit  = 0.0;
    int                     m_ticketTifIdx = 0;          // 0 = DAY, 1 = GTC
    bool                    m_transmitInstantly = false; // off: always confirm
    bool                    m_showConfirm  = false;
    core::Order             m_pendingOrder;
    core::services::StrategyMetrics m_ticketMetrics;

    bool m_showLast   = false;
    bool m_showVolume = true;
    bool m_showOi     = true;
    bool m_showIv     = true;
    bool m_showDelta  = true;
    bool m_showGamma  = false;
    bool m_showTheta  = false;
    bool m_showVega   = false;
};

}  // namespace ui
