#pragma once

#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

#include "core/models/OptionData.h"
#include "core/services/OptionChain.h"
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
    static constexpr int kSecDefReqId = 21000;

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

    // Live underlying price, used for ATM detection and moneyness shading.
    void OnUnderlyingPrice(double last);

    // ── Callbacks wired by main.cpp ─────────────────────────────────────────
    std::function<void(const std::string& sym)>               OnRequestUnderlying;
    std::function<void(int reqId, const std::string& sym,
                       int underlyingConId)>                  OnReqSecDefOptParams;
    std::function<void(const std::string& pattern)>           OnReqMatchingSymbols;
    std::function<void(const std::string& sym)>               OnBroadcastSymbol;

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

    // Days to expiry for m_meta.expirations[idx]; -1 when unparseable.
    int  DaysToExpiry(int idx) const;

    // Strikes currently visible under the range filter, as [lo, hi] indices.
    core::services::StrikeRange VisibleStrikeRange() const;

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

    // Auto-refresh (sketch toolbar: "Auto ON  15s").
    bool        m_autoRefresh    = false;
    int         m_autoRefreshSec = 15;

    // Column visibility. Bid/Ask/Last are always shown; these are the
    // optional ones, mirroring the Scanner's column-toggle pattern.
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
