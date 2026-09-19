#pragma once

// ============================================================================
// BracketChildForm — the Close-At-Profit (TP) + Stop-Loss (SL) boxes shared by
// the Options Chain order ticket and (later) the "Attach TP/SL" / "Protect"
// popups. Pure ImGui + the OptionChain bracket helpers; no window/IB coupling,
// so every call site renders and computes identically. See options-brackets.md.
//
// The caller owns a BracketChildState (persisted where it lives) and, each
// frame, builds a BracketContext from the entry it is protecting, calls
// BracketRecompute(state, ctx), then DrawBracketChildForm(state, ctx). It reads
// the resolved prices back off the state to build the closing child orders.
//
// If used more than once in a frame, wrap each call in ImGui::PushID / PopID.
// ============================================================================

#include "imgui.h"
#include "ui/UiScale.h"
#include "core/services/OptionChain.h"   // BracketClosePrice / BracketPctFromPrice / BracketEstPnL

#include <cstdio>

namespace ui {

// Persisted per call site. The enables + $/% modes + percents + stop type +
// TIFs are the "sticky" preference; the resolved prices re-derive from the entry.
struct BracketChildState {
    bool   tpOn         = false;
    bool   slOn         = false;
    bool   tpPctMode    = true;    // true = %, false = $
    bool   slPctMode    = true;
    double tpPct        = 0.50;    // fraction of the entry premium
    double slPct        = 0.50;
    double tpPrice      = 0.0;     // resolved close-net magnitude (display / $ mode)
    double slTrigger    = 0.0;     // resolved stop trigger magnitude
    double slLimit      = 0.0;     // resolved stop-limit magnitude (Stop Limit)
    bool   slLimitManual= false;   // user overrode the auto (= trigger) limit
    int    slStopType   = 1;       // 0 = Stop, 1 = Stop Limit
    int    tpTif        = 1;       // 0 = Day, 1 = GTC
    int    slTif        = 1;
};

// Rebuilt every frame from the entry being protected.
struct BracketContext {
    double entryNetMag    = 0.0;   // |entry net premium|
    bool   creditStrategy = false; // entry was a net credit (short the position)
    double multiplier     = 100.0;
    int    qty            = 1;
    double tick           = 0.01;
    bool   priced         = false; // entryNetMag usable (> 0)
};

// Derive the child prices from the context. In % mode the price follows the
// percent; in $ mode the percent follows the typed price. The SL limit tracks
// the trigger until the user overrides it.
inline void BracketRecompute(BracketChildState& s, const BracketContext& c) {
    using core::services::BracketClosePrice;
    using core::services::BracketPctFromPrice;
    if (s.tpPctMode) s.tpPrice = BracketClosePrice(c.entryNetMag, s.tpPct, /*isTP=*/true,  c.creditStrategy, c.tick);
    else             s.tpPct   = BracketPctFromPrice(c.entryNetMag, s.tpPrice);
    if (s.slPctMode) s.slTrigger = BracketClosePrice(c.entryNetMag, s.slPct, /*isTP=*/false, c.creditStrategy, c.tick);
    else             s.slPct     = BracketPctFromPrice(c.entryNetMag, s.slTrigger);
    if (!s.slLimitManual) s.slLimit = s.slTrigger;
}

// ── Small shared controls ────────────────────────────────────────────────────
inline void BracketDollarPctToggle(bool& pctMode, const char* id) {
    const ImVec4 on(0.30f, 0.55f, 0.95f, 1.0f);
    ImGui::PushID(id);
    if (!pctMode) ImGui::PushStyleColor(ImGuiCol_Button, on);
    if (ImGui::SmallButton("$")) pctMode = false;
    if (!pctMode) ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, em(2));
    if (pctMode) ImGui::PushStyleColor(ImGuiCol_Button, on);
    if (ImGui::SmallButton("%")) pctMode = true;
    if (pctMode) ImGui::PopStyleColor();
    ImGui::PopID();
}

// 10 / 25 / 50 / 75 % quick-set row. Selecting a preset switches to % mode.
inline void BracketPctPresets(double& pct, bool& pctMode, const char* id) {
    ImGui::PushID(id);
    static const int kP[] = {10, 25, 50, 75};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.0f, em(3));
        char b[8];
        std::snprintf(b, sizeof(b), "%d%%", kP[i]);
        const bool active = pctMode && (int)(pct * 100.0 + 0.5) == kP[i];
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.95f, 1.0f));
        if (ImGui::SmallButton(b)) { pct = kP[i] / 100.0; pctMode = true; }
        if (active) ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

// ── The two boxes ────────────────────────────────────────────────────────────
inline void DrawBracketChildForm(BracketChildState& s, const BracketContext& c) {
    const ImVec4 kGreen(0.35f, 0.80f, 0.45f, 1.0f);
    const ImVec4 kRed  (0.90f, 0.40f, 0.40f, 1.0f);
    const ImVec4 kDim  (0.65f, 0.66f, 0.70f, 1.0f);
    const char* kTif[] = {"Day", "GTC"};

    auto pctReadout = [&](double pct) {
        if (!c.priced) { ImGui::TextColored(kDim, "— from entry"); return; }
        ImGui::TextColored(kDim, "%.2f%% from entry", pct * 100.0);
    };

    // ── Close At Profit (TP) ─────────────────────────────────────────────────
    ImGui::Checkbox("##bcf_tp_on", &s.tpOn);
    ImGui::SameLine(0.0f, em(6));
    ImGui::TextColored(kGreen, "Close At Profit");
    if (s.tpOn) {
        ImGui::Indent(em(10));
        {
            ImGui::TextColored(kDim, "Limit");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(90));
            if (ImGui::InputDouble("##bcf_tp_px", &s.tpPrice, c.tick, c.tick * 10.0, "%.2f")) {
                if (s.tpPrice < 0.0) s.tpPrice = 0.0;
                s.tpPctMode = false;                                  // typing = $ mode
                s.tpPct = core::services::BracketPctFromPrice(c.entryNetMag, s.tpPrice);
            }
            ImGui::SameLine(0.0f, em(6));
            BracketDollarPctToggle(s.tpPctMode, "bcf_tpmode");
        }
        BracketPctPresets(s.tpPct, s.tpPctMode, "bcf_tp_ps");
        pctReadout(s.tpPct);
        {
            ImGui::TextColored(kDim, "TIF");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(64));
            ImGui::Combo("##bcf_tp_tif", &s.tpTif, kTif, 2);
            ImGui::SameLine(0.0f, em(14));
            const double pnl = core::services::BracketEstPnL(c.entryNetMag, s.tpPct, c.qty, c.multiplier);
            ImGui::TextColored(kDim, "Est. Profit");
            ImGui::SameLine(0.0f, em(4));
            if (c.priced) ImGui::TextColored(kGreen, "+%.2f (%.2f%%)", pnl, s.tpPct * 100.0);
            else          ImGui::TextColored(kDim, "—");
        }
        ImGui::Unindent(em(10));
    }

    // ── Stop Loss (SL) ───────────────────────────────────────────────────────
    ImGui::Checkbox("##bcf_sl_on", &s.slOn);
    ImGui::SameLine(0.0f, em(6));
    ImGui::TextColored(kRed, "Stop Loss");
    if (s.slOn) {
        ImGui::Indent(em(10));
        {
            ImGui::TextColored(kDim, "Type");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(104));
            const char* kStop[] = {"Stop", "Stop Limit"};
            ImGui::Combo("##bcf_sl_type", &s.slStopType, kStop, 2);
        }
        {
            ImGui::TextColored(kDim, "Trigger");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(90));
            if (ImGui::InputDouble("##bcf_sl_trig", &s.slTrigger, c.tick, c.tick * 10.0, "%.2f")) {
                if (s.slTrigger < 0.0) s.slTrigger = 0.0;
                s.slPctMode = false;
                s.slPct = core::services::BracketPctFromPrice(c.entryNetMag, s.slTrigger);
            }
            ImGui::SameLine(0.0f, em(6));
            BracketDollarPctToggle(s.slPctMode, "bcf_slmode");
        }
        if (s.slStopType == 1) {
            ImGui::TextColored(kDim, "Limit  ");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(90));
            if (ImGui::InputDouble("##bcf_sl_lmt", &s.slLimit, c.tick, c.tick * 10.0, "%.2f")) {
                if (s.slLimit < 0.0) s.slLimit = 0.0;
                s.slLimitManual = true;
            }
            ImGui::SameLine(0.0f, em(6));
            if (ImGui::SmallButton("= trig")) { s.slLimit = s.slTrigger; s.slLimitManual = false; }
        }
        BracketPctPresets(s.slPct, s.slPctMode, "bcf_sl_ps");
        pctReadout(s.slPct);
        {
            ImGui::TextColored(kDim, "TIF");
            ImGui::SameLine(0.0f, em(6));
            ImGui::SetNextItemWidth(em(64));
            ImGui::Combo("##bcf_sl_tif", &s.slTif, kTif, 2);
            ImGui::SameLine(0.0f, em(14));
            const double pnl = core::services::BracketEstPnL(c.entryNetMag, s.slPct, c.qty, c.multiplier);
            ImGui::TextColored(kDim, "Est. Loss");
            ImGui::SameLine(0.0f, em(4));
            if (c.priced) ImGui::TextColored(kRed, "-%.2f (%.2f%%)", pnl, s.slPct * 100.0);
            else          ImGui::TextColored(kDim, "—");
        }
        ImGui::Unindent(em(10));
    }
}

}  // namespace ui
