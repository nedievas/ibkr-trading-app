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

const ImVec4 kProb (0.72f, 0.52f, 0.95f, 0.85f);  // probability cone (purple)

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

double StrategyAnalysisWindow::probSigmaT() const {
    double maxDte = 0.0, ivSum = 0.0;
    int n = 0;
    for (const auto& l : m_in.legs) {
        if (l.stock) continue;
        maxDte = std::max(maxDte, l.dte);
        if (l.iv > 0.0) { ivSum += l.iv; ++n; }
    }
    if (maxDte <= 0.0 || n == 0) return 0.0;
    return (ivSum / n) * std::sqrt(maxDte / 365.0);
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

    // ── POP / P50 (lognormal terminal estimates) ──────────────────────────────
    const double sigmaT = probSigmaT();
    const ImVec4 cVal(0.85f, 0.86f, 0.9f, 1.0f);
    if (sigmaT > 0.0 && m_in.spot > 0.0) {
        const double pop = core::services::ProbPayoffAtLeast(
            m_in.legs, m_in.netPrice, m_in.multiplier, m_in.spot, sigmaT, 0.0);
        if (pop >= 0.0) {
            std::snprintf(buf, sizeof(buf), "%.0f%%", pop * 100.0);
            Stat(row, "POP", buf, cVal);
        }
        // P50 = probability of finishing at ≥ 50% of max profit — only defined
        // when max profit is finite and positive.
        if (m.valid && !m.profitUnbounded && m.maxProfit > 0.0) {
            const double p50 = core::services::ProbPayoffAtLeast(
                m_in.legs, m_in.netPrice, m_in.multiplier, m_in.spot, sigmaT,
                0.5 * m.maxProfit);
            if (p50 >= 0.0) {
                std::snprintf(buf, sizeof(buf), "%.0f%%", p50 * 100.0);
                Stat(row, "P50", buf, cVal);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lognormal estimate at expiry (mean leg IV), not a\n"
                              "path-dependent probability. For reference only.");
    }

    row.item(em(150));
    ImGui::Checkbox(m_totalMode ? "Total P&L" : "Per-contract P&L", &m_totalMode);
    row.item(FlexRow::checkboxW("Prob"));
    ImGui::Checkbox("Prob", &m_showProb);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lognormal probability cone (mean leg IV over max DTE).");

    // ── Evaluate-at-date control for the theoretical curve ────────────────────
    double maxDte = 0.0;
    for (const auto& l : m_in.legs) if (!l.stock) maxDte = std::max(maxDte, l.dte);
    if (maxDte > 0.0) {
        if (m_evalDays > maxDte) m_evalDays = maxDte;
        FlexRow r2;
        r2.item(FlexRow::textW("Evaluate:"));
        ImGui::TextColored(kDim, "Evaluate:");

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
    const double axisYMin = yMin - yPad, axisYMax = yMax + yPad;

    // Probability cone in data space (a real ImPlot item so it appears in the
    // legend and toggles), scaled to the lower 45% of the Y range.
    std::vector<double> yc(N);
    bool haveCone = false;
    if (m_showProb) {
        const double sigmaT = probSigmaT();
        if (sigmaT > 0.0 && m_in.spot > 0.0) {
            std::vector<double> pd(N);
            double pmax = 0.0;
            for (int i = 0; i < N; ++i) {
                pd[i] = core::services::LognormalPdf(xs[i], m_in.spot, sigmaT);
                pmax = std::max(pmax, pd[i]);
            }
            if (pmax > 0.0) {
                for (int i = 0; i < N; ++i)
                    yc[i] = axisYMin + (pd[i] / pmax) * 0.45 * (axisYMax - axisYMin);
                haveCone = true;
            }
        }
    }

    if (!ImPlot::BeginPlot("##payoff", ImVec2(-1, -1), ImPlotFlags_NoMouseText))
        return;
    ImPlot::SetupAxes("Underlying at expiry", "P&L ($)",
                      ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisLimits(ImAxis_X1, lo, hi, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, axisYMin, axisYMax, ImGuiCond_Always);

    // Profit / loss shading (hidden from the legend via ## ids).
    ImPlot::SetNextFillStyle(ImVec4(kUp.x, kUp.y, kUp.z, 1.0f), 0.16f);
    ImPlot::PlotShaded("##profit", xs.data(), yPos.data(), N, 0.0);
    ImPlot::SetNextFillStyle(ImVec4(kDown.x, kDown.y, kDown.z, 1.0f), 0.16f);
    ImPlot::PlotShaded("##loss", xs.data(), yNeg.data(), N, 0.0);

    // Named series — these carry the native, draggable legend (click to toggle).
    if (haveCone) {
        ImPlot::SetNextLineStyle(kProb, 1.4f);
        ImPlot::PlotLine("Price probability at expiry", xs.data(), yc.data(), N);
    }
    if (showTheo) {
        char lbl[40];
        const int d = (int)(m_evalDays + 0.5);
        std::snprintf(lbl, sizeof(lbl), d > 0 ? "P/L in %d day%s" : "P/L today",
                      d, d == 1 ? "" : "s");
        ImPlot::SetNextLineStyle(kTheo, 1.6f);
        ImPlot::PlotLine(lbl, xs.data(), yt.data(), N);
    }
    ImPlot::SetNextLineStyle(kLine, 2.0f);
    ImPlot::PlotLine("P/L at expiry", xs.data(), ys.data(), N);

    if (m_in.spot > 0.0) {
        double sp = m_in.spot;
        ImPlot::SetNextLineStyle(ImVec4(0.86f, 0.86f, 0.92f, 0.85f), 1.2f);
        ImPlot::PlotInfLines("Spot", &sp, 1);
    }
    if (!bes.empty()) {
        ImPlot::SetNextLineStyle(ImVec4(0.92f, 0.80f, 0.35f, 0.9f), 1.2f);
        ImPlot::PlotInfLines("Break-even", bes.data(), (int)bes.size());
    }

    // Strike gridlines (hidden from the legend).
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

    // Break-even value labels on the zero line (the vertical lines themselves
    // are the native "Break-even" series above).
    for (double be : bes) {
        if (be < rect.X.Min || be > rect.X.Max) continue;
        ImVec2 p = ImPlot::PlotToPixels(be, 0.0);
        char lbl[24]; std::snprintf(lbl, sizeof(lbl), "%.2f", be);
        dl->AddText(ImVec2(p.x + 4, p.y + 2), IM_COL32(235, 205, 90, 235), lbl);
    }

    // ── Hover crosshair + P/L readout ─────────────────────────────────────────
    // Moving the pointer over the plot shows the underlying price under the
    // cursor plus the P/L at expiry and (when live) the theoretical P/L there.
    if (ImPlot::IsPlotHovered()) {
        const ImPlotPoint mp = ImPlot::GetPlotMousePos();
        const double S = mp.x;
        if (S >= rect.X.Min && S <= rect.X.Max) {
            const double plExp = core::services::PayoffAtExpiry(
                m_in.legs, m_in.netPrice, mult, S) * sc;
            const double plTheo = showTheo
                ? core::services::TheoreticalPnL(m_in.legs, m_in.netPrice, mult,
                                                 S, m_evalDays, kRiskFreeRate) * sc
                : 0.0;

            // Vertical guide + a dot on each curve at this price.
            const ImVec2 top = ImPlot::PlotToPixels(S, rect.Y.Max);
            const ImVec2 bot = ImPlot::PlotToPixels(S, rect.Y.Min);
            dl->AddLine(top, bot, IM_COL32(200, 200, 210, 90), 1.0f);
            const ImVec2 dExp = ImPlot::PlotToPixels(S, plExp);
            dl->AddCircleFilled(dExp, 3.5f, ImGui::ColorConvertFloat4ToU32(kLine));
            if (showTheo) {
                const ImVec2 dTheo = ImPlot::PlotToPixels(S, plTheo);
                dl->AddCircleFilled(dTheo, 3.5f, ImGui::ColorConvertFloat4ToU32(kTheo));
            }

            // Readout box near the cursor, clamped inside the plot.
            char l0[32], l1[40], l2[40];
            std::snprintf(l0, sizeof(l0), "Price  %.2f", S);
            std::snprintf(l1, sizeof(l1), "P/L exp   %+.0f", plExp);
            std::snprintf(l2, sizeof(l2), "P/L theo  %+.0f", plTheo);
            const float pad = em(6), lh = ImGui::GetTextLineHeight();
            float w = ImGui::CalcTextSize(l0).x;
            w = std::max(w, ImGui::CalcTextSize(l1).x);
            if (showTheo) w = std::max(w, ImGui::CalcTextSize(l2).x);
            const int rows = showTheo ? 3 : 2;
            const float bw = w + pad * 2, bh = lh * rows + pad * 2;
            const ImVec2 mpx = ImGui::GetMousePos();
            float bx = mpx.x + em(14), by = mpx.y + em(14);
            const ImVec2 rMax = ImPlot::PlotToPixels(rect.X.Max, rect.Y.Min);
            const ImVec2 rMin = ImPlot::PlotToPixels(rect.X.Min, rect.Y.Max);
            if (bx + bw > rMax.x) bx = mpx.x - em(14) - bw;   // flip left near the edge
            if (bx < rMin.x)      bx = rMin.x + em(2);
            if (by + bh > rMax.y) by = rMax.y - bh - em(2);
            if (by < rMin.y)      by = rMin.y + em(2);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                              IM_COL32(20, 22, 28, 235), em(4));
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                        IM_COL32(90, 94, 105, 220), em(4));
            const ImU32 cDim = IM_COL32(180, 182, 190, 255);
            dl->AddText(ImVec2(bx + pad, by + pad), cDim, l0);
            dl->AddText(ImVec2(bx + pad, by + pad + lh),
                        ImGui::ColorConvertFloat4ToU32(kLine), l1);
            if (showTheo)
                dl->AddText(ImVec2(bx + pad, by + pad + lh * 2),
                            ImGui::ColorConvertFloat4ToU32(kTheo), l2);
        }
    }

    ImPlot::EndPlot();
}

}  // namespace ui
