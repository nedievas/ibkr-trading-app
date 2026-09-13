#include "ui/windows/StrategyAnalysisWindow.h"

#include "ui/UiScale.h"
#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

namespace {
const ImVec4 kUp   (0.36f, 0.78f, 0.45f, 1.0f);   // profit green
const ImVec4 kDown (0.88f, 0.28f, 0.28f, 1.0f);   // loss red
const ImVec4 kDim  (0.62f, 0.64f, 0.70f, 1.0f);
const ImVec4 kLine (0.98f, 0.62f, 0.20f, 1.0f);   // expiry payoff line (orange)
const ImVec4 kTheo (0.44f, 0.70f, 0.98f, 0.95f);  // theoretical "P/L today" (blue)

// Assumed risk-free rate for the Black-Scholes theoretical curve. No rate feed;
// a fixed constant is close enough for a P&L-shape visualisation.
constexpr double kRiskFreeRate = 0.04;

// Small helper: a dim "label value" pair on one FlexRow.
void Stat(FlexRow& row, const char* label, const char* value, ImVec4 col) {
    row.item(FlexRow::textW(label) + FlexRow::textW(value) + em(14));
    ImGui::TextColored(kDim, "%s", label);
    ImGui::SameLine(0.0f, em(4));
    ImGui::TextColored(col, "%s", value);
}
}  // namespace

bool StrategyAnalysisWindow::Render() {
    if (!m_open) return false;

    ImGui::SetNextWindowSize(ImVec2(em(720), em(520)), ImGuiCond_FirstUseEver);
    char title[96];
    std::snprintf(title, sizeof(title), "Strategy Analysis%s%s###stratanalysis",
                  m_in.valid ? " " : "", m_in.valid ? m_in.symbol.c_str() : "");
    if (!ImGui::Begin(title, &m_open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return m_open;
    }

    if (!m_in.valid || m_in.legs.empty()) {
        ImGui::Dummy(ImVec2(0, em(8)));
        ImGui::TextColored(kDim,
            "Stage an order in the Options Chain to see its payoff.");
        ImGui::TextColored(kDim,
            "Click bid/ask cells to build a cart, then press Analysis.");
        ImGui::End();
        return m_open;
    }

    if (!m_in.summary.empty()) ImGui::TextUnformatted(m_in.summary.c_str());
    DrawStatsStrip();
    ImGui::Separator();
    DrawPayoffPlot();

    ImGui::End();
    return m_open;
}

void StrategyAnalysisWindow::DrawStatsStrip() {
    const auto& m = m_in.metrics;
    const double sc = (m_totalMode || m_in.qty <= 0) ? 1.0 : 1.0 / m_in.qty;
    char buf[48];
    FlexRow row;

    if (m.valid) {
        if (m.profitUnbounded) Stat(row, "Max Profit", "unlimited", kUp);
        else { std::snprintf(buf, sizeof(buf), "%.0f", m.maxProfit * sc);
               Stat(row, "Max Profit", buf, kUp); }
        if (m.lossUnbounded) Stat(row, "Max Loss", "unlimited", kDown);
        else { std::snprintf(buf, sizeof(buf), "%.0f", m.maxLoss * sc);
               Stat(row, "Max Loss", buf, kDown); }
    }

    const auto bes = core::services::BreakevensAtExpiry(
        m_in.legs, m_in.netPrice, m_in.multiplier);
    if (bes.empty()) {
        Stat(row, "B/E", "—", kDim);
    } else {
        std::string s;
        for (std::size_t i = 0; i < bes.size(); ++i) {
            char b[24]; std::snprintf(b, sizeof(b), "%.2f", bes[i]);
            if (i) s += " / ";
            s += b;
        }
        Stat(row, bes.size() > 1 ? "B/E" : "B/E", s.c_str(),
             ImVec4(0.85f, 0.86f, 0.9f, 1.0f));
    }

    if (m.valid) {
        std::snprintf(buf, sizeof(buf), "%.0f", m.extrinsic * sc);
        Stat(row, "EXT", buf, m.extrinsic >= 0 ? kUp : kDown);
        std::snprintf(buf, sizeof(buf), "%.2f", m.netDelta * sc);
        Stat(row, "Delta", buf, ImVec4(0.85f, 0.86f, 0.9f, 1.0f));
        std::snprintf(buf, sizeof(buf), "%.3f", m.netTheta * sc);
        Stat(row, "Theta", buf, ImVec4(0.85f, 0.86f, 0.9f, 1.0f));
    }

    row.item(em(150));
    ImGui::Checkbox(m_totalMode ? "Total P&L" : "Per-contract P&L", &m_totalMode);

    // ── Evaluate-at-date control for the theoretical curve ────────────────────
    double maxDte = 0.0;
    for (const auto& l : m_in.legs) if (!l.stock) maxDte = std::max(maxDte, l.dte);
    if (maxDte > 0.0) {
        if (m_evalDays > maxDte) m_evalDays = maxDte;
        FlexRow r2;
        // Colour key so the two curves are readable without a legend box.
        r2.item(FlexRow::textW("Today") + FlexRow::textW("At expiry") + em(40));
        ImGui::TextColored(kTheo, "\xE2\x80\x94 Today");
        ImGui::SameLine(0.0f, em(10));
        ImGui::TextColored(kLine, "\xE2\x80\x94 At expiry");

        int days = (int)(m_evalDays + 0.5);
        r2.item(em(220));
        ImGui::SetNextItemWidth(em(150));
        if (ImGui::SliderInt("##evaldays", &days, 0, (int)maxDte,
                             days == 0 ? "today" : "+%d d")) {
            m_evalDays = (double)std::clamp(days, 0, (int)maxDte);
        }
        r2.item(FlexRow::buttonW("Today"));
        if (ImGui::Button("Today")) m_evalDays = 0.0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Evaluate the theoretical curve as of today (max time value).");

        r2.item(em(90));
        ImGui::TextColored(kDim, "%d DTE left", (int)(maxDte - m_evalDays + 0.5));
    }
}

void StrategyAnalysisWindow::DrawPayoffPlot() {
    const double mult = m_in.multiplier > 0 ? m_in.multiplier : 100.0;
    const double sc   = (m_totalMode || m_in.qty <= 0) ? 1.0 : 1.0 / m_in.qty;

    // ── Price band: frame every strike, the spot, and every break-even ───────
    const auto bes = core::services::BreakevensAtExpiry(m_in.legs, m_in.netPrice, mult);
    double lo = m_in.spot > 0 ? m_in.spot : 0.0;
    double hi = lo;
    auto grow = [&](double x) { if (x <= 0) return; lo = std::min(lo, x); hi = std::max(hi, x); };
    if (lo <= 0 && !m_in.strikes.empty()) { lo = hi = m_in.strikes.front(); }
    for (double k : m_in.strikes) grow(k);
    for (double b : bes) grow(b);
    if (hi <= lo) { hi = lo + 1.0; }
    const double pad = std::max({ (hi - lo) * 0.25, (m_in.spot > 0 ? m_in.spot : hi) * 0.08, 1.0 });
    lo = std::max(0.0, lo - pad);
    hi = hi + pad;

    // The theoretical "P/L today" curve differs from the expiry line only while
    // some option leg still has time value left at the evaluation date.
    double maxDte = 0.0;
    for (const auto& l : m_in.legs) if (!l.stock) maxDte = std::max(maxDte, l.dte);
    const bool showTheo = (maxDte > 0.0) && (m_evalDays < maxDte - 1e-9);

    // ── Sample the payoff curve ──────────────────────────────────────────────
    const int N = 256;
    std::vector<double> xs(N), ys(N), yPos(N), yNeg(N), yt(N);
    double yMin = 0.0, yMax = 0.0;
    for (int i = 0; i < N; ++i) {
        const double S = lo + (hi - lo) * (double)i / (double)(N - 1);
        const double p = core::services::PayoffAtExpiry(m_in.legs, m_in.netPrice, mult, S) * sc;
        xs[i] = S; ys[i] = p;
        yPos[i] = std::max(p, 0.0);
        yNeg[i] = std::min(p, 0.0);
        yMin = std::min(yMin, p); yMax = std::max(yMax, p);
        if (showTheo) {
            const double pt = core::services::TheoreticalPnL(
                m_in.legs, m_in.netPrice, mult, S, m_evalDays, kRiskFreeRate) * sc;
            yt[i] = pt;
            yMin = std::min(yMin, pt); yMax = std::max(yMax, pt);
        }
    }
    const double yPad = std::max((yMax - yMin) * 0.12, 1.0);

    if (!ImPlot::BeginPlot("##payoff", ImVec2(-1, -1),
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) return;
    ImPlot::SetupAxes("Underlying at expiry", "P&L ($)",
                      ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisLimits(ImAxis_X1, lo, hi, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, yMin - yPad, yMax + yPad, ImGuiCond_Always);

    // Profit / loss shading against the zero line.
    ImPlot::SetNextFillStyle(ImVec4(kUp.x, kUp.y, kUp.z, 1.0f), 0.16f);
    ImPlot::PlotShaded("##profit", xs.data(), yPos.data(), N, 0.0);
    ImPlot::SetNextFillStyle(ImVec4(kDown.x, kDown.y, kDown.z, 1.0f), 0.16f);
    ImPlot::PlotShaded("##loss", xs.data(), yNeg.data(), N, 0.0);

    // The theoretical "P/L today" curve (drawn under the expiry line).
    if (showTheo) {
        ImPlot::SetNextLineStyle(kTheo, 1.6f);
        ImPlot::PlotLine("##theo", xs.data(), yt.data(), N);
    }

    // The expiry payoff line.
    ImPlot::SetNextLineStyle(kLine, 2.0f);
    ImPlot::PlotLine("##expiry", xs.data(), ys.data(), N);

    // Strike gridlines.
    if (!m_in.strikes.empty()) {
        ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.58f, 0.35f), 1.0f);
        ImPlot::PlotInfLines("##strikes", m_in.strikes.data(), (int)m_in.strikes.size());
    }

    // ── Manual overlays via the plot draw list ───────────────────────────────
    ImDrawList* dl = ImPlot::GetPlotDrawList();
    const ImPlotRect rect = ImPlot::GetPlotLimits();

    // Zero P&L axis (solid, brighter than the gridlines).
    {
        ImVec2 a = ImPlot::PlotToPixels(rect.X.Min, 0.0);
        ImVec2 b = ImPlot::PlotToPixels(rect.X.Max, 0.0);
        dl->AddLine(a, b, IM_COL32(150, 150, 165, 180), 1.0f);
    }

    // Spot marker: dashed vertical + label.
    if (m_in.spot > 0 && m_in.spot >= rect.X.Min && m_in.spot <= rect.X.Max) {
        ImVec2 top = ImPlot::PlotToPixels(m_in.spot, rect.Y.Max);
        ImVec2 bot = ImPlot::PlotToPixels(m_in.spot, rect.Y.Min);
        for (float y = top.y; y < bot.y; y += 7.0f)
            dl->AddLine(ImVec2(top.x, y), ImVec2(top.x, std::min(y + 4.0f, bot.y)),
                        IM_COL32(220, 220, 230, 150), 1.0f);
        char lbl[32]; std::snprintf(lbl, sizeof(lbl), "spot %.2f", m_in.spot);
        dl->AddText(ImVec2(top.x + 4, top.y + 2), IM_COL32(230, 230, 240, 220), lbl);
    }

    // Break-even markers on the zero line.
    for (double be : bes) {
        if (be < rect.X.Min || be > rect.X.Max) continue;
        ImVec2 p = ImPlot::PlotToPixels(be, 0.0);
        dl->AddTriangleFilled(ImVec2(p.x, p.y - 6), ImVec2(p.x - 5, p.y + 4),
                              ImVec2(p.x + 5, p.y + 4), IM_COL32(235, 205, 90, 230));
        char lbl[24]; std::snprintf(lbl, sizeof(lbl), "%.2f", be);
        dl->AddText(ImVec2(p.x + 6, p.y - 16), IM_COL32(235, 205, 90, 230), lbl);
    }

    ImPlot::EndPlot();
}

}  // namespace ui
