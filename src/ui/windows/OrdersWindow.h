#pragma once

#include "imgui.h"
#include "core/models/OrderData.h"
#include "core/models/ContractSpec.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <string>

namespace core::services { struct StateBlock; }

namespace ui {

// ============================================================================
// OrdersWindow — Live order blotter.
//
// Two tabs:
//   Open    — Submitted / Working / Partially filled orders with Cancel button.
//   History — Filled and Cancelled orders.
//
// Data fed by:
//   OnOpenOrder()       — from IBKRClient::openOrder (full detail on submit/reqOpenOrders)
//   OnOrderStatus()     — status+filled+avgPrice updates
//   OnFill()            — actual commission after execution
// ============================================================================
class OrdersWindow {
public:
    OrdersWindow();
    bool Render();   // returns false when window is closed
    bool& open() { return m_open; }

    // ── Data push-ins ─────────────────────────────────────────────────────
    void OnOpenOrder(const core::Order& order);
    void OnOrderStatus(int orderId, core::OrderStatus status,
                       double filled, double avgPrice,
                       const std::string& reason = {});
    void OnFill(const core::Fill& fill);
    void OnQueriedFill(const core::Fill& fill);   // from filtered reqExecutions

    // Live quote for the price-ladder box while a price cell is being edited.
    // Routed from main.cpp for the dedicated modify-quote reqId (kQuoteReqId).
    void OnQuoteTick(int field, double price);    // 1=bid 2=ask 4=last
    void OnQuoteParams(double minTick);           // contract's min price increment

    // Per-leg quote for a combo (BAG) order — IB won't stream a BAG quote on
    // paper/delayed feeds, so we subscribe each leg (reqIds kLegQuoteBase+idx)
    // and synthesize the combo net bid/ask/mid from the legs.
    void OnLegQuoteTick(int legIdx, int field, double price);
    void OnLegQuoteParams(int legIdx, double minTick);

    // Reserved reqId for the on-demand modify-quote market-data subscription
    // (single contract), plus a small block for combo-leg quotes.
    static constexpr int kQuoteReqId    = 8002;
    static constexpr int kLegQuoteBase  = 8003;   // legs use 8003 .. 8003+kMaxLegQuotes-1
    static constexpr int kMaxLegQuotes  = 6;

    // ── Callbacks wired by main.cpp ───────────────────────────────────────
    std::function<void(int orderId)> OnCancelOrder;
    // Inline modify: the edited copy carries orderId + the new modifiable
    // fields (quantity, price legs, TIF). main.cpp merges them onto the
    // authoritative g_liveOrders record and re-issues placeOrder().
    std::function<void(const core::Order& edited)> OnModifyOrderFull;
    // Filter toolbar "Load" → calls ReqExecutions(8001, sym, side, dateFrom)
    std::function<void(const std::string& sym, const std::string& side,
                       const std::string& dateFrom)> OnLoadHistory;
    // Price-ladder quote: subscribe to the order's own contract while a price
    // cell is being edited; cancel when the edit ends. main.cpp uses kQuoteReqId.
    std::function<void(const core::ContractSpec& spec)> OnRequestQuote;
    std::function<void()>                               OnCancelQuote;
    // For a combo order: subscribe one market-data line per leg (in order) so
    // the ladder can synthesize the combo net bid/ask/mid. main.cpp maps leg i
    // to reqId kLegQuoteBase+i and cancels the whole block via OnCancelQuote.
    std::function<void(const std::vector<core::ContractSpec>& legs)> OnRequestLegQuotes;

    // ── State persistence ───────────────────────────────────────────────────
    void SerializeSettings(core::services::StateBlock& b) const;
    void ApplySettings    (const core::services::StateBlock& b);

private:
    bool m_open    = true;
    int  m_activeTab = 0;   // 0 = Open, 1 = History

    std::unordered_map<int, core::Order> m_orders;   // orderId → Order

    // ── Queried fills (from filtered reqExecutions) ───────────────────────
    std::vector<core::Fill> m_queriedFills;

    // ── History tab filter state ──────────────────────────────────────────
    char m_filterSymbol[16] = "";
    int  m_filterSideIdx    = 0;   // 0=All 1=BUY 2=SELL

    // ── Inline order-modify state ─────────────────────────────────────────
    // -1 = no row editing. When set, that row's Qty / Price / Aux / TIF cells
    // render as inputs and the Action column shows Update + revert (⟲).
    int  m_editOrderId       = -1;
    char m_editQty[16]       = "";
    char m_editPrimary[16]   = "";
    char m_editSecondary[16] = "";
    int  m_editTif           = 0;

    // Price-ladder box state (shown while editing the primary price field).
    bool   m_ladderActive = false;
    double m_ladderBid  = 0.0;
    double m_ladderAsk  = 0.0;
    double m_ladderLast = 0.0;
    double m_ladderTick = 0.01;   // updated from the contract's real minTick
    bool   m_ladderCenter = false;// scroll the ladder to the money once, on open
    ImVec2 m_ladderAnchorMin{};   // Price cell rect (captured during the row)
    ImVec2 m_ladderAnchorMax{};

    // Combo (BAG) leg-quote synthesis. When editing a combo's net-limit cell we
    // subscribe every leg and combine their bid/ask into a synthetic combo NBBO
    // (the ladder's m_ladderBid/Ask/Last), since IB won't quote the BAG itself.
    struct LegQuote { int ratio = 1; bool buy = true; bool stock = false;
                      double bid = 0, ask = 0, last = 0, tick = 0; };
    std::vector<LegQuote> m_legQuotes;
    bool m_ladderCombo = false;   // ladder is driven by leg synthesis
    void RecomputeComboQuote();

    void DrawPriceLadder();
    void StopLadder();            // deactivate + cancel the quote subscription

    void BeginEditOrder(const core::Order& o);
    void CommitEditOrder();
    void CancelEditOrder();

    void DrawOpenTab();
    void DrawHistoryTab();
    void DrawOrderRow(core::Order& o, bool showCancel);
    void DrawQueriedFillRow(const core::Fill& f);

    static ImVec4 StatusColor(core::OrderStatus s);
    static bool   IsTerminal(core::OrderStatus s);
};

}  // namespace ui
