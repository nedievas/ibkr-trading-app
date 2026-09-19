#include "ui/windows/OrdersWindow.h"
#include "core/models/MarketData.h"        // BarSession (after-hours guard)
#include "core/services/state-io.h"
#include "core/services/OrderEdit.h"
#include "imgui.h"
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <algorithm>

namespace ui {

// IB embeds literal "<br>" tags in some reject / warning messages. Turn them
// into spaces so the blotter shows clean prose instead of raw markup; the
// tooltip wraps, so a single-line form reads fine there too.
static std::string CleanReason(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "<br>") == 0) { out += ' '; i += 4; }
        else                              { out += s[i]; ++i; }
    }
    return out;
}

// ============================================================================
OrdersWindow::OrdersWindow() {}

// ============================================================================
// State persistence
// ============================================================================

void OrdersWindow::SerializeSettings(core::services::StateBlock& b) const {
    using namespace core::services;
    if (m_filterSymbol[0]) SetString(b, "ORD_FILTER_SYMBOL", m_filterSymbol);
    SetInt (b, "ORD_FILTER_SIDE",  m_filterSideIdx);
}

void OrdersWindow::ApplySettings(const core::services::StateBlock& b) {
    using namespace core::services;
    std::string fs = GetString(b, "ORD_FILTER_SYMBOL", "");
    if (!fs.empty()) { std::strncpy(m_filterSymbol, fs.c_str(), sizeof(m_filterSymbol)-1); }
    m_filterSideIdx = GetInt(b, "ORD_FILTER_SIDE", m_filterSideIdx, 0, 2);
}

void OrdersWindow::SerializeHistory(std::vector<core::services::StateBlock>& out) const {
    using namespace core::services;
    constexpr size_t kMaxHistory = 500;   // bound the file
    std::vector<const core::Order*> terminal;
    for (const auto& [id, o] : m_orders)
        if (IsTerminal(o.status)) terminal.push_back(&o);
    std::sort(terminal.begin(), terminal.end(),
              [](const core::Order* a, const core::Order* b) {
                  return a->updatedAt > b->updatedAt;   // newest first
              });
    if (terminal.size() > kMaxHistory) terminal.resize(kMaxHistory);
    for (const core::Order* op : terminal) {
        const core::Order& o = *op;
        StateBlock b;
        b.instance = o.orderId;
        SetString(b, "SYMBOL", o.symbol);
        SetInt   (b, "SIDE",   (int)o.side);
        SetInt   (b, "TYPE",   (int)o.type);
        SetInt   (b, "TIF",    (int)o.tif);
        SetDouble(b, "QTY",    o.quantity);
        SetDouble(b, "LMT",    o.limitPrice);
        SetDouble(b, "STP",    o.stopPrice);
        SetDouble(b, "AUX",    o.auxPrice);
        SetBool  (b, "EXT",    o.outsideRth);
        SetDouble(b, "FILLED", o.filledQty);
        SetDouble(b, "AVG",    o.avgFillPrice);
        SetDouble(b, "COMM",   o.commission);
        SetInt   (b, "STATUS", (int)o.status);
        SetString(b, "REJECT", o.rejectReason);
        SetDouble(b, "UPDATED",(double)o.updatedAt);
        // Option / combo descriptor so the history row renders its real label.
        if (!o.spec.secType.empty())  SetString(b, "SEC",   o.spec.secType);
        if (!o.spec.lastTradeDateOrContractMonth.empty())
                                      SetString(b, "EXP",   o.spec.lastTradeDateOrContractMonth);
        if (o.spec.strike > 0.0)      SetDouble(b, "STRIKE",o.spec.strike);
        if (!o.spec.right.empty())    SetString(b, "RIGHT", o.spec.right);
        if (!o.spec.comboLegsDescrip.empty())
                                      SetString(b, "COMBO", o.spec.comboLegsDescrip);
        out.push_back(std::move(b));
    }
}

void OrdersWindow::LoadHistory(const std::vector<core::services::StateBlock>& blocks) {
    using namespace core::services;
    for (const auto& b : blocks) {
        if (b.instance <= 0) continue;
        if (m_orders.count(b.instance)) continue;   // live IB data wins
        core::Order o;
        o.orderId      = b.instance;
        o.symbol       = GetString(b, "SYMBOL", "");
        o.side         = (core::OrderSide)  GetInt(b, "SIDE",   0, 0, 1);
        o.type         = (core::OrderType)  GetInt(b, "TYPE",   0, 0, 12);
        o.tif          = (core::TimeInForce)GetInt(b, "TIF",    0, 0, 5);
        o.quantity     = GetDouble(b, "QTY",    0.0, 0.0, 1e12);
        o.limitPrice   = GetDouble(b, "LMT",    0.0, -1e12, 1e12);
        o.stopPrice    = GetDouble(b, "STP",    0.0, 0.0, 1e12);
        o.auxPrice     = GetDouble(b, "AUX",    0.0, -1e12, 1e12);
        o.outsideRth   = GetBool  (b, "EXT",    false);
        o.filledQty    = GetDouble(b, "FILLED", 0.0, 0.0, 1e12);
        o.avgFillPrice = GetDouble(b, "AVG",    0.0, 0.0, 1e12);
        o.commission   = GetDouble(b, "COMM",   0.0, -1e12, 1e12);
        o.status       = (core::OrderStatus)GetInt(b, "STATUS",
                                            (int)core::OrderStatus::Filled, 0, 6);
        o.rejectReason = GetString(b, "REJECT", "");
        o.updatedAt    = (std::time_t)GetDouble(b, "UPDATED", 0.0, 0.0, 4e9);
        o.spec.secType = GetString(b, "SEC", "");
        o.spec.lastTradeDateOrContractMonth = GetString(b, "EXP", "");
        o.spec.strike  = GetDouble(b, "STRIKE", 0.0, 0.0, 1e7);
        o.spec.right   = GetString(b, "RIGHT", "");
        o.spec.comboLegsDescrip = GetString(b, "COMBO", "");
        if (!IsTerminal(o.status)) continue;   // defensive: file holds only these
        m_orders[o.orderId] = std::move(o);
    }
}

// ============================================================================
// Data push-ins
// ============================================================================
void OrdersWindow::OnOpenOrder(const core::Order& order) {
    auto it = m_orders.find(order.orderId);
    if (it == m_orders.end()) {
        m_orders[order.orderId] = order;
    } else {
        // Preserve commission and fill info already received from fills/status
        core::Order& existing = it->second;
        existing.symbol     = order.symbol;
        existing.side       = order.side;
        existing.type       = order.type;
        existing.tif        = order.tif;
        existing.quantity   = order.quantity;
        existing.limitPrice = order.limitPrice;
        existing.stopPrice  = order.stopPrice;
        // Only update status/commission if not already terminal from a fill
        if (!IsTerminal(existing.status))
            existing.status = order.status;
        if (existing.commission == 0.0 && order.commission != 0.0)
            existing.commission = order.commission;
        // Hold reason is informational; copy non-empty value through so a
        // late-arriving IB warning ("[404] held until open") gets shown.
        if (!order.holdReason.empty())
            existing.holdReason = order.holdReason;
        existing.updatedAt = order.updatedAt;
    }
}

void OrdersWindow::OnOrderStatus(int orderId, core::OrderStatus status,
                                  double filled, double avgPrice,
                                  const std::string& reason) {
    auto it = m_orders.find(orderId);
    if (it == m_orders.end()) {
        // Status arrived before openOrder — create a skeleton entry
        core::Order o;
        o.orderId      = orderId;
        o.status       = status;
        o.filledQty    = filled;
        o.avgFillPrice = avgPrice;
        o.updatedAt    = std::time(nullptr);
        if (!reason.empty()) o.rejectReason = reason;
        m_orders[orderId] = o;
    } else {
        it->second.status       = status;
        it->second.filledQty    = filled;
        it->second.avgFillPrice = avgPrice;
        it->second.updatedAt    = std::time(nullptr);
        if (!reason.empty()) it->second.rejectReason = reason;
    }
}

void OrdersWindow::OnFill(const core::Fill& fill) {
    auto it = m_orders.find(fill.orderId);
    if (it != m_orders.end()) {
        it->second.commission += fill.commission;
    }
}

void OrdersWindow::OnQueriedFill(const core::Fill& fill) {
    // Deduplicate by execId so repeated Load calls don't stack duplicates
    for (const auto& f : m_queriedFills)
        if (f.execId == fill.execId) return;
    m_queriedFills.push_back(fill);
}

// ============================================================================
// Render
// ============================================================================
bool OrdersWindow::Render() {
    if (!m_open) return false;

    ImGui::SetNextWindowSize(ImVec2(880, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Orders###Orders", &m_open, ImGuiWindowFlags_NoFocusOnAppearing)) { ImGui::End(); return m_open; }

    // Counts live in the tab labels (no separate header row — it read as a
    // duplicate you couldn't click to switch tabs). Stable ###ids keep tab
    // selection while the counts update.
    int nOpen = 0, nHistory = 0;
    for (const auto& [id, o] : m_orders)
        (IsTerminal(o.status) ? nHistory : nOpen)++;
    char openLbl[32], histLbl[32];
    std::snprintf(openLbl, sizeof(openLbl), "Open (%d)###ordopen", nOpen);
    std::snprintf(histLbl, sizeof(histLbl), "History (%d)###ordhist", nHistory);

    if (ImGui::BeginTabBar("##orderstabs")) {
        if (ImGui::BeginTabItem(openLbl)) {
            m_activeTab = 0;
            DrawOpenTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(histLbl)) {
            m_activeTab = 1;
            DrawHistoryTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    return m_open;
}

// ============================================================================
// Tabs
// ============================================================================
void OrdersWindow::DrawOpenTab() {
    bool anyOpen = false;
    for (const auto& [id, o] : m_orders)
        if (!IsTerminal(o.status)) { anyOpen = true; break; }

    if (!anyOpen) {
        ImGui::Spacing();
        ImGui::TextDisabled("  No open orders.");
        return;
    }

    static ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##open", 15, flags, ImVec2(-1, -1))) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_WidthFixed,  52);
    ImGui::TableSetupColumn("Symbol",   ImGuiTableColumnFlags_WidthFixed, 130);
    ImGui::TableSetupColumn("Side",     ImGuiTableColumnFlags_WidthFixed,  42);
    ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed,  72);
    ImGui::TableSetupColumn("Qty",      ImGuiTableColumnFlags_WidthFixed,  55);
    ImGui::TableSetupColumn("Price",    ImGuiTableColumnFlags_WidthFixed,  80);
    ImGui::TableSetupColumn("Aux",      ImGuiTableColumnFlags_WidthFixed,  78);
    ImGui::TableSetupColumn("TIF",      ImGuiTableColumnFlags_WidthFixed,  38);
    ImGui::TableSetupColumn("Ext",      ImGuiTableColumnFlags_WidthFixed,  30);
    ImGui::TableSetupColumn("Filled",   ImGuiTableColumnFlags_WidthFixed,  52);
    ImGui::TableSetupColumn("Avg $",    ImGuiTableColumnFlags_WidthFixed,  70);
    ImGui::TableSetupColumn("Comm $",   ImGuiTableColumnFlags_WidthFixed,  62);
    ImGui::TableSetupColumn("Time",     ImGuiTableColumnFlags_WidthFixed,  62);
    ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_WidthFixed, 100);
    ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_WidthFixed,  96);
    ImGui::TableHeadersRow();

    for (auto& [id, o] : m_orders) {
        if (IsTerminal(o.status)) continue;
        DrawOrderRow(o, true);
    }
    ImGui::EndTable();

    // Price-ladder box for the row being edited (rendered after the table so it
    // floats above it without nesting inside a cell).
    if (m_ladderActive && m_editOrderId != -1) DrawPriceLadder();

    // Attach-bracket popup for the right-clicked working order.
    if (m_attachOrderId != -1) {
        auto it = m_orders.find(m_attachOrderId);
        if (it == m_orders.end() || IsTerminal(it->second.status)) {
            m_attachOrderId = -1;   // order vanished / filled — drop it
        } else {
            const core::Order& parent = it->second;
            char summary[128];
            if (parent.spec.secType == "BAG")
                std::snprintf(summary, sizeof(summary), "%s combo (%d legs)  Net %+.2f  Qty %.0f",
                              parent.symbol.c_str(), (int)parent.spec.comboLegs.size(),
                              parent.limitPrice, parent.quantity);
            else
                std::snprintf(summary, sizeof(summary), "%s %s %.0f %s  @ %.2f  Qty %.0f",
                              parent.symbol.c_str(),
                              parent.spec.lastTradeDateOrContractMonth.c_str(),
                              parent.spec.strike, parent.spec.right.c_str(),
                              parent.limitPrice, parent.quantity);
            const bool extHours = core::BarSession(std::time(nullptr)) != core::Session::Regular;
            if (ui::DrawBracketAttachPopup("Attach TP / SL##ord_attach_modal",
                                           m_attachOpen, "Attach bracket to working order",
                                           summary, parent, m_attachBracket, extHours,
                                           m_attachChildren)) {
                if (OnAttachBracket && !m_attachChildren.empty())
                    OnAttachBracket(m_attachOrderId, m_attachChildren);
                m_attachChildren.clear();
                m_attachOrderId = -1;
            }
        }
    }

    // Esc discards an in-progress inline edit (same as the row's "x" button).
    if (m_editOrderId != -1 && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        CancelEditOrder();
}

void OrdersWindow::DrawHistoryTab() {
    // ── Filter toolbar ────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(88);
    ImGui::InputText("##fsym", m_filterSymbol, sizeof(m_filterSymbol));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Symbol (empty = all)");
    ImGui::SameLine(0, 4);
    static constexpr const char* kSides[] = {"All", "BUY", "SELL"};
    ImGui::SetNextItemWidth(62);
    ImGui::Combo("##fside", &m_filterSideIdx, kSides, 3);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Load##hist")) {
        if (OnLoadHistory) {
            const char* sideStr = (m_filterSideIdx == 1) ? "BUY"
                                : (m_filterSideIdx == 2) ? "SELL" : "";
            // IB's reqExecutions only serves the last ~24h (since midnight);
            // older history isn't available via the API — so no date filter.
            OnLoadHistory(m_filterSymbol, sideStr, "");
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Load fills from the last 24 h (since midnight)\n"
                          "filtered by symbol / side above.\n"
                          "IB's API doesn't serve older execution history.");
    if (!m_queriedFills.empty()) {
        ImGui::SameLine(0, 8);
        if (ImGui::SmallButton("Clear##qf")) m_queriedFills.clear();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear queried results");
    }
    ImGui::Separator();

    bool anyHistory = false;
    for (const auto& [id, o] : m_orders)
        if (IsTerminal(o.status)) { anyHistory = true; break; }
    bool hasQueried = !m_queriedFills.empty();

    float availH = ImGui::GetContentRegionAvail().y;
    float liveH  = hasQueried ? availH * 0.55f : availH;

    // ── Live session history ──────────────────────────────────────────────
    if (!anyHistory) {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::BeginChild("##livehistory", ImVec2(-1, liveH));
        ImGui::Spacing();
        ImGui::TextDisabled("  No live order history.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        static ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##history", 15, flags, ImVec2(-1, liveH))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_WidthFixed,  52);
            ImGui::TableSetupColumn("Symbol",   ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("Side",     ImGuiTableColumnFlags_WidthFixed,  42);
            ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed,  72);
            ImGui::TableSetupColumn("Qty",      ImGuiTableColumnFlags_WidthFixed,  55);
            ImGui::TableSetupColumn("Price",    ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableSetupColumn("Aux",      ImGuiTableColumnFlags_WidthFixed,  78);
            ImGui::TableSetupColumn("TIF",      ImGuiTableColumnFlags_WidthFixed,  38);
            ImGui::TableSetupColumn("Ext",      ImGuiTableColumnFlags_WidthFixed,  30);
            ImGui::TableSetupColumn("Filled",   ImGuiTableColumnFlags_WidthFixed,  52);
            ImGui::TableSetupColumn("Avg $",    ImGuiTableColumnFlags_WidthFixed,  70);
            ImGui::TableSetupColumn("Comm $",   ImGuiTableColumnFlags_WidthFixed,  62);
            ImGui::TableSetupColumn("Updated",  ImGuiTableColumnFlags_WidthFixed,  62);
            ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Reject",   ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableHeadersRow();

            for (auto& [id, o] : m_orders) {
                if (!IsTerminal(o.status)) continue;
                DrawOrderRow(o, false);
            }
            ImGui::EndTable();
        }
    }

    // ── Queried fills (from filtered reqExecutions) ───────────────────────
    if (hasQueried) {
        float queryH = availH - liveH - ImGui::GetFrameHeightWithSpacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.30f, 1.0f));
        ImGui::Text("Queried results (%d)", static_cast<int>(m_queriedFills.size()));
        ImGui::PopStyleColor();
        ImGui::Separator();

        static ImGuiTableFlags qflags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##qfills", 8, qflags, ImVec2(-1, queryH - ImGui::GetFrameHeightWithSpacing()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthFixed,  62);
            ImGui::TableSetupColumn("Symbol",  ImGuiTableColumnFlags_WidthFixed,  68);
            ImGui::TableSetupColumn("Side",    ImGuiTableColumnFlags_WidthFixed,  42);
            ImGui::TableSetupColumn("Qty",     ImGuiTableColumnFlags_WidthFixed,  55);
            ImGui::TableSetupColumn("Price",   ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableSetupColumn("Comm $",  ImGuiTableColumnFlags_WidthFixed,  62);
            ImGui::TableSetupColumn("P&L",     ImGuiTableColumnFlags_WidthFixed,  72);
            ImGui::TableSetupColumn("OrderId", ImGuiTableColumnFlags_WidthFixed,  58);
            ImGui::TableHeadersRow();

            for (const auto& f : m_queriedFills)
                DrawQueriedFillRow(f);
            ImGui::EndTable();
        }
    }
}

// ============================================================================
// Inline order-modify — enter / commit
// ============================================================================
void OrdersWindow::BeginEditOrder(const core::Order& o) {
    m_editOrderId = o.orderId;
    std::snprintf(m_editQty, sizeof(m_editQty), "%.0f", o.quantity);
    const core::services::OrderEditSpec spec = core::services::OrderEditFields(o.type);
    if (spec.primary != core::services::OrderPriceField::None)
        std::snprintf(m_editPrimary, sizeof(m_editPrimary), "%.2f",
                      core::services::GetOrderPriceField(o, spec.primary));
    else m_editPrimary[0] = '\0';
    if (spec.secondary != core::services::OrderPriceField::None)
        std::snprintf(m_editSecondary, sizeof(m_editSecondary), "%.2f",
                      core::services::GetOrderPriceField(o, spec.secondary));
    else m_editSecondary[0] = '\0';
    m_editTif = static_cast<int>(o.tif);

    // Price ladder: subscribe for a live bid/mid/ask while the primary price is
    // editable. A single contract streams directly; a combo (BAG) is priced by
    // synthesizing its net from each leg's own quote — IB does not serve a BAG
    // quote on paper/delayed feeds.
    m_ladderActive = false;
    m_ladderCombo  = false;
    m_ladderBid = m_ladderAsk = m_ladderLast = 0.0;
    m_ladderTick = 0.01;
    m_legQuotes.clear();
    const bool isCombo = (o.spec.secType == "BAG" && !o.spec.comboLegs.empty());
    if (spec.primary != core::services::OrderPriceField::None) {
        if (isCombo && OnRequestLegQuotes) {
            std::vector<core::ContractSpec> legSpecs;
            for (const auto& L : o.spec.comboLegs) {
                if ((int)m_legQuotes.size() >= kMaxLegQuotes) break;
                LegQuote lq;
                lq.ratio = L.ratio;
                lq.buy   = (L.action == "BUY");
                lq.stock = (L.ratio >= 100);   // equity leg (shares/contract)
                m_legQuotes.push_back(lq);
                core::ContractSpec cs;
                cs.conId    = L.conId;
                cs.secType  = lq.stock ? "STK" : "OPT";
                cs.exchange = "SMART";
                legSpecs.push_back(cs);
            }
            m_ladderActive = true;
            m_ladderCombo  = true;
            m_ladderCenter = true;
            OnRequestLegQuotes(legSpecs);
        } else if (!isCombo && OnRequestQuote) {
            core::ContractSpec cs = o.spec;
            if (cs.symbol.empty())  cs.symbol  = o.symbol;
            if (cs.secType.empty()) cs.secType = "STK";
            m_ladderActive = true;
            m_ladderCenter = true;   // center the ladder on the money on first draw
            OnRequestQuote(cs);
        }
    }
}

void OrdersWindow::StopLadder() {
    if (!m_ladderActive) return;
    m_ladderActive = false;
    if (OnCancelQuote) OnCancelQuote();
}

void OrdersWindow::CancelEditOrder() {
    m_editOrderId = -1;
    StopLadder();
}

void OrdersWindow::OnQuoteTick(int field, double price) {
    if (price < 0.0) return;
    switch (field) {
        case 1: m_ladderBid  = price; break;   // BID
        case 2: m_ladderAsk  = price; break;   // ASK
        case 4: m_ladderLast = price; break;   // LAST
        default: break;
    }
}

void OrdersWindow::OnQuoteParams(double minTick) {
    if (minTick > 0.0) m_ladderTick = minTick;
}

void OrdersWindow::OnLegQuoteTick(int legIdx, int field, double price) {
    if (legIdx < 0 || legIdx >= (int)m_legQuotes.size() || price < 0.0) return;
    LegQuote& lq = m_legQuotes[legIdx];
    switch (field) {
        case 1: lq.bid  = price; break;   // BID
        case 2: lq.ask  = price; break;   // ASK
        case 4: lq.last = price; break;   // LAST (fallback when no bid/ask)
        default: return;
    }
    RecomputeComboQuote();
}

void OrdersWindow::OnLegQuoteParams(int legIdx, double minTick) {
    if (legIdx < 0 || legIdx >= (int)m_legQuotes.size() || minTick <= 0.0) return;
    m_legQuotes[legIdx].tick = minTick;
    // The combo net must conform to the coarsest leg's increment.
    double coarsest = 0.0;
    for (const auto& lq : m_legQuotes) coarsest = std::max(coarsest, lq.tick);
    if (coarsest > 0.0) m_ladderTick = coarsest;
}

// Combine per-leg quotes into a synthetic combo NBBO, using the same signed-net
// convention as the order's limit (BUY leg adds, SELL leg subtracts; an equity
// leg's share ratio is normalised by 100 to the per-contract scale). net-bid =
// the passive fill (buy@bid / sell@ask); net-ask = the marketable fill
// (buy@ask / sell@bid). Requires every leg to have a two-sided (or last) quote.
void OrdersWindow::RecomputeComboQuote() {
    double nbid = 0.0, nask = 0.0;
    for (const LegQuote& lq : m_legQuotes) {
        const double lb = lq.bid > 0.0 ? lq.bid : lq.last;
        const double la = lq.ask > 0.0 ? lq.ask : lq.last;
        if (lb <= 0.0 || la <= 0.0) return;   // wait until every leg has priced
        const double eff = lq.stock ? lq.ratio / 100.0 : lq.ratio;
        if (lq.buy) { nbid += eff * lb; nask += eff * la; }
        else        { nbid -= eff * la; nask -= eff * lb; }
    }
    m_ladderBid  = nbid;
    m_ladderAsk  = nask;
    m_ladderLast = (nbid + nask) * 0.5;
}

void OrdersWindow::CommitEditOrder() {
    auto it = m_orders.find(m_editOrderId);
    if (it == m_orders.end()) { m_editOrderId = -1; return; }
    core::Order ed = it->second;             // base — keeps symbol/type/side/spec
    const core::services::OrderEditSpec spec = core::services::OrderEditFields(ed.type);
    double q = std::atof(m_editQty);
    if (q > 0.0) ed.quantity = q;
    if (spec.primary != core::services::OrderPriceField::None && m_editPrimary[0])
        core::services::SetOrderPriceField(ed, spec.primary, std::atof(m_editPrimary));
    if (spec.secondary != core::services::OrderPriceField::None && m_editSecondary[0])
        core::services::SetOrderPriceField(ed, spec.secondary, std::atof(m_editSecondary));
    ed.tif = static_cast<core::TimeInForce>(m_editTif);

    it->second = ed;                          // reflect locally at once
    if (OnModifyOrderFull) OnModifyOrderFull(ed);
    m_editOrderId = -1;
    StopLadder();
}

// Floating price-ladder box under the edited Price cell: Ask / Mid / Bid rows
// plus a scrollable ladder stepping by the contract's real minTick. Click any
// row/rung to set the primary price buffer. NoFocusOnAppearing so it doesn't
// steal typing focus from the cell's InputText.
void OrdersWindow::DrawPriceLadder() {
    const double bid = m_ladderBid, ask = m_ladderAsk;
    const double tick = m_ladderTick > 0.0 ? m_ladderTick : 0.01;
    // The mid is an average of two tick-aligned quotes, so it is often half a
    // tick off the ladder grid — snap it so it lands on a rung and gets tagged.
    const bool   haveMid = (bid != 0.0 && ask != 0.0) || m_ladderLast != 0.0;
    const double midRaw  = (bid != 0.0 && ask != 0.0) ? (bid + ask) * 0.5 : m_ladderLast;
    const double mid     = haveMid ? std::round(midRaw / tick) * tick : 0.0;
    const int dec = tick < 0.001 ? 4 : (tick < 0.01 ? 3 : 2);

    auto setPx = [&](double p) {
        std::snprintf(m_editPrimary, sizeof(m_editPrimary), "%.*f", dec, p);
    };
    const double cur = std::atof(m_editPrimary);

    ImGui::SetNextWindowPos(ImVec2(m_ladderAnchorMin.x, m_ladderAnchorMax.y + 2.0f),
                            ImGuiCond_Always);
    const ImGuiWindowFlags fl =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##pxladder", nullptr, fl)) {
        const ImU32 kAskCol = IM_COL32(230, 120, 120, 255);   // red
        const ImU32 kBidCol = IM_COL32(120, 200, 140, 255);   // green
        const ImU32 kMidCol = IM_COL32(235, 215,  90, 255);   // yellow

        // Ladder around mid (or the current value when no quote yet), high→low.
        // The ask / bid / mid rungs are colour-coded and tagged inline; the
        // current value is selected. Wide span to scroll; auto-centres once.
        auto onTick = [&](double a, double b) { return std::fabs(a - b) < tick * 0.5; };
        double center = (mid != 0.0) ? mid : (cur != 0.0 ? cur : 0.0);
        center = std::round(center / tick) * tick;
        ImGui::BeginChild("##rungs", ImVec2(160, 240), false);
        const int span = 80;   // ±80 ticks
        for (int k = span; k >= -span; --k) {
            const double p = std::round((center + k * tick) / tick) * tick;
            const bool sel = onTick(p, cur);
            ImU32 col = 0; bool hasCol = true;
            const char* tag = nullptr;
            if      (ask != 0.0 && onTick(p, ask)) { col = kAskCol; tag = "ask"; }
            else if (bid != 0.0 && onTick(p, bid)) { col = kBidCol; tag = "bid"; }
            else if (mid != 0.0 && onTick(p, mid)) { col = kMidCol; tag = "mid"; }
            else hasCol = false;
            char b[32];
            if (tag) std::snprintf(b, sizeof(b), "%+.*f  %s", dec, p, tag);
            else     std::snprintf(b, sizeof(b), "%+.*f", dec, p);
            if (hasCol) ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::Selectable(b, sel)) setPx(p);
            if (hasCol) ImGui::PopStyleColor();
            if (k == 0 && m_ladderCenter) { ImGui::SetScrollHereY(0.5f); m_ladderCenter = false; }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// ============================================================================
// Single order row
// Columns (0-14):
//   0 ID | 1 Symbol | 2 Side | 3 Type | 4 Qty | 5 Price | 6 Aux | 7 TIF |
//   8 Ext | 9 Filled | 10 Avg$ | 11 Comm$ | 12 Time | 13 Status |
//   14 Action(open=Cancel) / Reject reason(history)
// ============================================================================
void OrdersWindow::DrawOrderRow(core::Order& o, bool showCancel) {
    ImGui::TableNextRow();
    ImGui::PushID(o.orderId);

    // Inline-modify state for this row.
    const bool active  = showCancel && !IsTerminal(o.status);
    const bool editing = active && (m_editOrderId == o.orderId);
    const core::services::OrderEditSpec espec = core::services::OrderEditFields(o.type);
    // Clickable value → enter edit mode (call right after rendering the value).
    auto editHint = [&]() {
        if (!active || editing) return;
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) BeginEditOrder(o);
    };

    // Row tint
    ImVec4 rowTint = (o.side == core::OrderSide::Buy)
        ? ImVec4(0.10f, 0.22f, 0.12f, 0.30f)
        : ImVec4(0.26f, 0.10f, 0.10f, 0.30f);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
        ImGui::ColorConvertFloat4ToU32(rowTint));

    // 0 — ID
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%d", o.orderId);

    // Right-click an active option/combo order → attach a TP/SL bracket. Gated
    // to OPT/BAG (options-only scope); the child popup is drawn once after the
    // table. Bound to the ID cell so it doesn't fight the value cells' click-
    // to-edit.
    if (active && (o.spec.secType == "OPT" || o.spec.secType == "BAG")) {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Right-click: attach TP / SL");
        if (ImGui::BeginPopupContextItem("##ord_attach")) {
            if (ImGui::MenuItem("Attach TP / SL…")) {
                m_attachOrderId = o.orderId;
                m_attachOpen    = true;
                m_attachBracket = ui::BracketChildState{};   // fresh each open
                m_attachBracket.tpOn = true;                 // TP is the common case
            }
            ImGui::EndPopup();
        }
    }

    // 1 — Symbol (option legs show "TSLA Oct16'26 310 Put"; combos show "TSLA spread")
    ImGui::TableSetColumnIndex(1);
    if (o.spec.secType == "BAG") {
        // IB's comboLegsDescrip is raw "conId|ratio,conId|ratio", not readable,
        // so show a clean label by leg count (2 legs = vertical). The real
        // per-leg strikes appear in History once the combo fills (each leg is
        // its own OPT execution, labelled via OptionDisplayLabel).
        // Locally-built combos carry spec.comboLegs; IB's openOrder ack instead
        // fills comboLegsDescrip ("conId|ratio,…"). Count legs + track the max
        // leg ratio from whichever is present, so a freshly-sent combo reads the
        // same as after a reload. A leg with ratio ≥ 100 is the equity leg of a
        // stock+option combo (covered call / collar), which is NOT a vertical.
        int legs = (int)o.spec.comboLegs.size();
        int maxRatio = 0;
        for (const auto& L : o.spec.comboLegs) maxRatio = std::max(maxRatio, L.ratio);
        if (legs == 0 && !o.spec.comboLegsDescrip.empty()) {
            const std::string& d = o.spec.comboLegsDescrip;
            legs = 1;
            for (std::size_t i = 0; i < d.size(); ++i) {
                if (d[i] == ',') ++legs;
                if (d[i] == '|') maxRatio = std::max(maxRatio, std::atoi(d.c_str() + i + 1));
            }
        }
        const bool hasStock = (maxRatio >= 100);
        if (hasStock)       ImGui::Text("%s combo (%d legs)", o.symbol.c_str(), legs);
        else if (legs == 2) ImGui::Text("%s vertical", o.symbol.c_str());
        else if (legs > 2)  ImGui::Text("%s combo (%d legs)", o.symbol.c_str(), legs);
        else                ImGui::Text("%s spread", o.symbol.c_str());
        if (ImGui::IsItemHovered() && !o.spec.comboLegsDescrip.empty())
            ImGui::SetTooltip("combo legs: %s", o.spec.comboLegsDescrip.c_str());
    } else
        ImGui::TextUnformatted(core::OptionDisplayLabel(
            o.symbol, o.spec.lastTradeDateOrContractMonth, o.spec.strike, o.spec.right).c_str());

    // 2 — Side
    ImGui::TableSetColumnIndex(2);
    ImGui::PushStyleColor(ImGuiCol_Text,
        o.side == core::OrderSide::Buy
            ? ImVec4(0.20f, 0.90f, 0.40f, 1.f)
            : ImVec4(0.95f, 0.30f, 0.30f, 1.f));
    ImGui::TextUnformatted(core::OrderSideStr(o.side));
    ImGui::PopStyleColor();

    // 3 — Type
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(core::OrderTypeStr(o.type));

    // 4 — Qty
    ImGui::TableSetColumnIndex(4);
    if (editing) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##eq", m_editQty, sizeof(m_editQty),
                         ImGuiInputTextFlags_CharsDecimal);
    } else {
        ImGui::Text("%.0f", o.quantity);
        editHint();
    }

    // 5 — Price (main price per order type)
    ImGui::TableSetColumnIndex(5);
    if (editing && espec.primary != core::services::OrderPriceField::None) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##ep", m_editPrimary, sizeof(m_editPrimary),
                         ImGuiInputTextFlags_CharsDecimal);
        // Anchor the floating price-ladder box under this cell (drawn after the
        // table so it doesn't nest inside the cell).
        m_ladderAnchorMin = ImGui::GetItemRectMin();
        m_ladderAnchorMax = ImGui::GetItemRectMax();
    } else {
    switch (o.type) {
        case core::OrderType::Market:
        case core::OrderType::MOC:
        case core::OrderType::MTL:
            ImGui::TextDisabled("MKT");
            break;
        case core::OrderType::Limit:
        case core::OrderType::LOC:
            // A combo (BAG) limit is a signed NET (debit + / credit −), so it can
            // be negative or zero — show it whenever we have a combo; only the
            // single-contract limit is gated on > 0.
            if (o.spec.secType == "BAG") ImGui::Text("%+.2f", o.limitPrice);
            else if (o.limitPrice > 0.0) ImGui::Text("$%.2f", o.limitPrice);
            else                         ImGui::TextDisabled("—");
            break;
        case core::OrderType::Stop:
        case core::OrderType::StopLimit:
            if (o.stopPrice > 0.0) ImGui::Text("stp $%.2f", o.stopPrice);
            else                   ImGui::TextDisabled("—");
            break;
        case core::OrderType::Trail:
        case core::OrderType::TrailLimit:
            if (o.trailingPercent > 0.0) ImGui::Text("tr %.2f%%", o.trailingPercent);
            else if (o.auxPrice  > 0.0)  ImGui::Text("tr $%.2f",  o.auxPrice);
            else                         ImGui::TextDisabled("—");
            break;
        case core::OrderType::MIT:
            if (o.auxPrice > 0.0) ImGui::Text("trig $%.2f", o.auxPrice);
            else                  ImGui::TextDisabled("—");
            break;
        case core::OrderType::LIT:
            if (o.auxPrice > 0.0) ImGui::Text("trig $%.2f", o.auxPrice);
            else                  ImGui::TextDisabled("—");
            break;
        case core::OrderType::Midprice:
            ImGui::TextDisabled("mid");
            break;
        case core::OrderType::Relative:
            if (o.auxPrice > 0.0) ImGui::Text("off $%.3f", o.auxPrice);
            else                  ImGui::TextDisabled("REL");
            break;
        default:
            ImGui::TextDisabled("—");
            break;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (o.limitPrice      > 0.0) ImGui::Text("Limit:    $%.4f", o.limitPrice);
        if (o.stopPrice       > 0.0) ImGui::Text("Stop:     $%.4f", o.stopPrice);
        if (o.auxPrice        > 0.0) ImGui::Text("Aux:      $%.4f", o.auxPrice);
        if (o.trailingPercent > 0.0) ImGui::Text("Trail %%: %.4f",  o.trailingPercent);
        if (o.trailStopPrice  > 0.0) ImGui::Text("Stop cap: $%.4f", o.trailStopPrice);
        if (o.lmtPriceOffset != 0.0) ImGui::Text("Lmt off:  $%.4f", o.lmtPriceOffset);
        ImGui::EndTooltip();
    }
        editHint();
    }

    // 6 — Aux (secondary price for dual-leg / trail orders)
    ImGui::TableSetColumnIndex(6);
    if (editing && espec.secondary != core::services::OrderPriceField::None) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##es", m_editSecondary, sizeof(m_editSecondary),
                         ImGuiInputTextFlags_CharsDecimal);
    } else {
    switch (o.type) {
        case core::OrderType::StopLimit:
            if (o.limitPrice > 0.0) ImGui::Text("lmt $%.2f", o.limitPrice);
            else                    ImGui::TextDisabled("—");
            break;
        case core::OrderType::LIT:
            if (o.limitPrice > 0.0) ImGui::Text("lmt $%.2f", o.limitPrice);
            else                    ImGui::TextDisabled("—");
            break;
        case core::OrderType::TrailLimit:
            if (o.lmtPriceOffset != 0.0) ImGui::Text("off $%.3f", o.lmtPriceOffset);
            else                         ImGui::TextDisabled("—");
            if (o.trailStopPrice > 0.0 && ImGui::IsItemHovered())
                ImGui::SetTooltip("Stop cap: $%.2f", o.trailStopPrice);
            break;
        case core::OrderType::Trail:
            if (o.trailStopPrice > 0.0) ImGui::Text("cap $%.2f", o.trailStopPrice);
            else                        ImGui::TextDisabled("—");
            break;
        case core::OrderType::Midprice:
            if (o.limitPrice > 0.0) ImGui::Text("cap $%.2f", o.limitPrice);
            else                    ImGui::TextDisabled("—");
            break;
        default:
            ImGui::TextDisabled("—");
            break;
    }
        editHint();
    }

    // 7 — TIF
    ImGui::TableSetColumnIndex(7);
    if (editing) {
        static const char* kTifs[] = {"DAY","GTC","IOC","FOK","OVERNIGHT","OPG"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##et", &m_editTif, kTifs, IM_ARRAYSIZE(kTifs));
    } else {
        ImGui::TextUnformatted(core::TIFStr(o.tif));
        editHint();
    }

    // 8 — Ext RTH
    ImGui::TableSetColumnIndex(8);
    if (o.outsideRth)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted(o.outsideRth ? "Y" : "—");
    if (o.outsideRth) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Outside Regular Trading Hours allowed");
    }

    // 9 — Filled qty
    ImGui::TableSetColumnIndex(9);
    if (o.filledQty > 0.0)
        ImGui::Text("%.0f", o.filledQty);
    else
        ImGui::TextDisabled("—");

    // 10 — Avg fill price
    ImGui::TableSetColumnIndex(10);
    if (o.avgFillPrice > 0.0)
        ImGui::Text("$%.2f", o.avgFillPrice);
    else
        ImGui::TextDisabled("—");

    // 11 — Commission
    ImGui::TableSetColumnIndex(11);
    if (o.commission > 0.0)
        ImGui::Text("-$%.2f", o.commission);
    else
        ImGui::TextDisabled("—");

    // 12 — Time (submitted for open orders, updated for history)
    ImGui::TableSetColumnIndex(12);
    {
        std::time_t ts = showCancel ? o.submittedAt : o.updatedAt;
        if (ts != 0) {
            char tbuf[16];
            std::tm* lt = std::localtime(&ts);
            std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt);
            ImGui::TextDisabled("%s", tbuf);
        } else {
            ImGui::TextDisabled("—");
        }
    }

    // 13 — Status. Append amber ⚠ HELD chip when IB has held the order
    // (typically pre/post-market submission held until RTH open). Hover the
    // status text for the full IB warning message.
    ImGui::TableSetColumnIndex(13);
    ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(o.status));
    ImGui::TextUnformatted(core::OrderStatusStr(o.status));
    ImGui::PopStyleColor();
    if (!o.holdReason.empty() && !IsTerminal(o.status)) {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", CleanReason(o.holdReason).c_str());
        ImGui::SameLine(0, 4);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.20f, 1.0f));
        ImGui::TextUnformatted("HELD");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", CleanReason(o.holdReason).c_str());
    }

    // 14 — Cancel (open) or Reject reason (history)
    ImGui::TableSetColumnIndex(14);
    if (showCancel) {
        if (active && editing) {
            // Update commits the buffered edits; the small "×" discards them.
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.45f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.28f, 1.0f));
            if (ImGui::SmallButton("Update")) CommitEditOrder();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("x")) CancelEditOrder();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Discard changes");
        } else if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
            if (ImGui::SmallButton("Cancel") && OnCancelOrder)
                OnCancelOrder(o.orderId);
            ImGui::PopStyleColor(2);
        }
    } else {
        if (!o.rejectReason.empty()) {
            const std::string reason = CleanReason(o.rejectReason);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.35f, 0.35f, 1.0f));
            ImGui::TextUnformatted(reason.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                ImGui::TextUnformatted(reason.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        } else {
            ImGui::TextDisabled("—");
        }
    }

    ImGui::PopID();
}

// ============================================================================
// Queried fill row (8 columns: Time | Symbol | Side | Qty | Price | Comm | P&L | OrderId)
// ============================================================================
void OrdersWindow::DrawQueriedFillRow(const core::Fill& f) {
    ImGui::TableNextRow();
    ImGui::PushID(f.execId.c_str());

    // Amber tint to distinguish from live-session orders
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.20f, 0.05f, 0.35f)));

    // 0 — Time
    ImGui::TableSetColumnIndex(0);
    if (f.timestamp != 0) {
        char tbuf[16];
        std::tm* lt = std::localtime(&f.timestamp);
        std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt);
        ImGui::TextDisabled("%s", tbuf);
    } else {
        ImGui::TextDisabled("—");
    }

    // 1 — Symbol (option legs show "TSLA Oct16'26 310 Put")
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(
        core::OptionDisplayLabel(f.symbol, f.expiry, f.strike, f.right).c_str());

    // 2 — Side
    ImGui::TableSetColumnIndex(2);
    ImGui::PushStyleColor(ImGuiCol_Text,
        f.side == core::OrderSide::Buy
            ? ImVec4(0.20f, 0.90f, 0.40f, 1.f)
            : ImVec4(0.95f, 0.30f, 0.30f, 1.f));
    ImGui::TextUnformatted(core::OrderSideStr(f.side));
    ImGui::PopStyleColor();

    // 3 — Qty
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%.0f", f.quantity);

    // 4 — Price
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("$%.2f", f.price);

    // 5 — Commission
    ImGui::TableSetColumnIndex(5);
    if (f.commission > 0.0) ImGui::Text("-$%.2f", f.commission);
    else                    ImGui::TextDisabled("—");

    // 6 — Realized P&L
    ImGui::TableSetColumnIndex(6);
    if (f.realizedPnL != 0.0) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            f.realizedPnL >= 0.0
                ? ImVec4(0.20f, 0.90f, 0.40f, 1.f)
                : ImVec4(0.95f, 0.30f, 0.30f, 1.f));
        ImGui::Text("%+.2f", f.realizedPnL);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("—");
    }

    // 7 — OrderId
    ImGui::TableSetColumnIndex(7);
    ImGui::TextDisabled("%d", f.orderId);

    ImGui::PopID();
}

// ============================================================================
// Helpers
// ============================================================================
ImVec4 OrdersWindow::StatusColor(core::OrderStatus s) {
    switch (s) {
        case core::OrderStatus::Pending:
        case core::OrderStatus::Working:     return ImVec4(1.0f, 0.85f, 0.20f, 1.0f);
        case core::OrderStatus::PartialFill: return ImVec4(1.0f, 0.60f, 0.10f, 1.0f);
        case core::OrderStatus::Filled:      return ImVec4(0.20f, 0.90f, 0.40f, 1.0f);
        case core::OrderStatus::Cancelled:   return ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
        case core::OrderStatus::Rejected:    return ImVec4(0.90f, 0.25f, 0.25f, 1.0f);
        default:                             return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }
}

bool OrdersWindow::IsTerminal(core::OrderStatus s) {
    return s == core::OrderStatus::Filled   ||
           s == core::OrderStatus::Cancelled ||
           s == core::OrderStatus::Rejected;
}

}  // namespace ui
