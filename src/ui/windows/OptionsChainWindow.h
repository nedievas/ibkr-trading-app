#pragma once

#include "imgui.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

#include "core/models/OptionData.h"
#include "core/models/OrderData.h"
#include "core/models/PortfolioData.h"
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
    static constexpr int kStrikeEnumReqId = 21003;  // per-expiry strike enumeration
    // Combo-leg conId resolution: one reqId per staged leg, kLegConIdBase + legIdx.
    static constexpr int kMaxLegs         = 6;      // Phase A cap (iron condor / fly / condor)
    static constexpr int kLegConIdBase    = 21010;  // .. 21015 (kMaxLegs legs)

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
    // A resolved leg conId from the reqContractDetails round-trip (reqIds
    // kLegConIdBase + legIdx), matched to a cart leg by index + (expiry,
    // strike, right).
    void OnLegConId(int reqId, const std::string& expiry, double strike,
                    const std::string& right, long conId);
    // One tradeable strike for `expiry`, from the enumeration request.
    // `tradingClass` is the contract's class; only strikes in the underlying's
    // standard class are kept, so adjusted / non-standard listings (which have
    // greeks but no market — e.g. a 311 strike) are dropped, as on tastytrade.
    void OnStrikeEnum(const std::string& expiry, double strike,
                      const std::string& tradingClass);

    // Live underlying quote (ATM detection, moneyness shading, header strip).
    // field: 1=bid, 2=ask, 4=last, 9=prev close.
    void OnUnderlyingTick(int field, double value);
    // field: 8=volume (cumulative day).
    void OnUnderlyingSize(int field, double value);

    // Option market data, routed by reqId (pool 22000-22999).
    void OnOptionPrice  (int reqId, int field, double price);
    void OnOptionSize   (int reqId, int field, double size);
    void OnOptionGeneric(int reqId, int tickType, double value);
    void OnOptionGreeks (int reqId, int tickType, double impliedVol, double delta,
                         double gamma, double vega, double theta, double undPrice);

    // Held option positions for the current underlying (Phase 2 qty pills).
    // main.cpp feeds the conId-keyed position set filtered to this symbol; the
    // window keys them by (expiry, strike, right) so each strike row can show a
    // signed green (long) / red (short) qty pill in its ITM gutter. Passing the
    // full snapshot each call (positions that went flat are simply absent) keeps
    // the window's map authoritative without per-leg flat bookkeeping.
    void SetOptionPositions(const std::vector<core::Position>& opts);

    // Cancel every live option subscription (disconnect / window close / shutdown).
    void CancelAll();

    // ── Callbacks wired by main.cpp ─────────────────────────────────────────
    // Resolve the underlying's conId and start its quote; reqSecDefOptParams
    // cannot be issued without the conId.
    std::function<void(const std::string& sym)>               OnRequestUnderlying;
    // Enumerate the exact tradeable strikes for one (symbol, expiry) via
    // reqContractDetails, so the display and subscriptions use IB's real strike
    // set for that expiry rather than the union across all expirations.
    std::function<void(int reqId, const std::string& sym,
                       const std::string& expiry)>            OnReqOptionStrikes;
    // Resolve a single option leg's conId (reqContractDetails on a full spec),
    // needed to build a BAG combo for a vertical spread.
    std::function<void(int reqId, const core::OptionContractKey& key)>
                                                              OnReqOptionLegConId;
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
    void DrawEmptyState(const char* msg);
    void RequestChain();
    void DrawOrderTicket();
    float kTicketBandHeight() const;
    void DrawConfirmPopup();

    // Recompute the ticket's payoff metrics from the staged legs + limit price.
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

    // Held option positions for the current underlying, keyed "expiry|strike|right"
    // (DeadKey format); signed qty (+long / -short). conId + avgCost retained for
    // the hover tooltip now and the close/roll actions in Phase 3.
    struct HeldLeg { double qty = 0.0; double avgCost = 0.0; long conId = 0; };
    std::unordered_map<std::string, HeldLeg> m_positions;
    const HeldLeg* HeldFor(const std::string& expiry, double strike, char right) const;

    bool        m_open   = true;
    int         m_groupId = 1;

    std::string m_symbol;
    char        m_symbolBuf[33] = {};
    SymbolSearchState m_symSearch;

    int         m_underlyingConId = 0;
    double      m_underlyingPrice = 0.0;

    core::OptionChainMeta m_meta;

    // Authoritative tradeable strikes per expiry (from reqContractDetails).
    // m_meta.strikes is the union across all expirations; this is the exact set
    // for one expiry, which drives display / subscription / ATM once it lands.
    std::unordered_map<std::string, std::vector<double>> m_expiryStrikes;
    std::vector<double> m_activeStrikes;   // strikes for the selected expiry (or union fallback)
    std::string         m_enumRequested;   // expiry whose enumeration is in flight/cached
    void RebuildActiveStrikes();
    void MaybeEnumerateStrikes();
    bool        m_loading      = false;   // chain definition in flight
    bool        m_chainLoaded  = false;
    std::string m_status;                 // one-line status / error text

    int         m_expiryIdx  = 0;         // index into m_meta.expirations
    bool        m_expirySingleRow = false;// false = wrap tabs; true = 1 row + < >
    float       m_expiryScrollReq = 0.0f; // pending horizontal scroll (arrow clicks)
    int         m_strikeRange = 20;       // +/- N strikes around ATM; -1 = all

    // Underlying context strip (sketch: "SPY 450.12 +2.35 (+0.53%) | IVX 18.4%
    // | Expected Move +/-6.85 (1.52%)"). IVX is the underlying's ATM implied
    // vol; expected move is derived from it and days-to-expiry, so both stay
    // zero until option ticks arrive in Task D2.
    double      m_underlyingChange    = 0.0;
    double      m_underlyingChangePct = 0.0;
    double      m_underlyingBid       = 0.0;
    double      m_underlyingAsk       = 0.0;
    double      m_underlyingVol       = 0.0;   // cumulative day volume
    double      m_underlyingPrevClose = 0.0;   // for change computation
    double      m_ivx                 = 0.0;   // fraction, e.g. 0.184
    double      m_expectedMove        = 0.0;   // absolute dollars, 1 sigma
    bool        m_emWeighted          = false; // true = full weighting, false = 0.85 fallback

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

    // Strike-index span actually on screen last render pass (respects scroll),
    // so "Strikes: ALL" streams the rows the user scrolled to rather than only
    // the nearest-ATM slice of a long ladder. -1 = nothing captured yet.
    int    m_renderVisLo  = -1;
    int    m_renderVisHi  = -1;

    // Hard ceiling on concurrent option subscriptions, well under the typical
    // 100-line account limit so charts / DOM / watchlists keep working. Sized
    // to hold a tall viewport (~30 strikes × 2 rights) plus the pinned
    // expected-move core without the cap evicting scrolled-to rows.
    static constexpr int kMaxOptionSubs = 72;

    // ── Order ticket — N-leg cart (Phase A: all legs share one expiry) ───────
    // One click on a chain bid/ask cell adds a leg; clicking the same
    // (strike, right, side) again removes it (toggle). 1 leg = single OPT
    // order; ≥2 legs = a BAG combo. Each leg carries its own BUY/SELL + ratio,
    // so straddles/strangles/flies/condors/iron-condors are all just leg sets.
    struct TicketLeg {
        core::OptionContractKey key;
        bool buy   = true;      // BUY / SELL this leg
        int  ratio = 1;         // per-leg ratio within the combo (≥1)
        long conId = 0;         // resolved leg conId (0 = pending); combos only
    };
    std::vector<TicketLeg>  m_legs;                       // the cart
    bool                    m_ticketActive = false;       // == !m_legs.empty()
    int                     m_ticketQty    = 1;           // combos placed
    double                  m_ticketLimit  = 0.0;         // per-contract (1 leg) or net (combo)
    int                     m_ticketTifIdx = 0;           // 0 = DAY, 1 = GTC
    bool                    m_transmitInstantly = false;  // off: always confirm
    bool                    m_showConfirm  = false;
    core::Order             m_pendingOrder;
    core::services::StrategyMetrics m_ticketMetrics;

    bool   isCombo() const { return m_legs.size() >= 2; }
    // Add a leg, or toggle it off if the same (strike,right,side) is staged.
    void   AddOrToggleLeg(const core::OptionContractKey& key, bool buy);
    void   RemoveLeg(int idx);
    // (Re-)issue the per-leg conId reqContractDetails round-trips (combos only).
    void   ResolveLegConIds();
    double LegMid(const TicketLeg& L) const;
    // Signed net debit(+)/credit(-) across all legs at their current mids, ×1.
    double NetMid() const;

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
