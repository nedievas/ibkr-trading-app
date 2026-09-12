#pragma once

#include <string>
#include <vector>

#include "core/services/OptionChain.h"

namespace ui {

// ============================================================================
// StrategyAnalysisWindow — the staged order ticket's P&L graph.
//
// Singleton. Holds no IBKRClient: like ReplayWindow it renders only from a
// snapshot pushed by main.cpp each frame (built from OptionsChainWindow's cart),
// so it never touches IB. Opened by the "Analysis" button on the chain ticket.
//
// Phase AG-1 (this file): the payoff-at-expiry curve — the orange line, green
// profit / red loss shading, strike gridlines, spot + break-even markers, and
// the Max Profit/Loss / EXT / Delta / Theta stats. The smooth "P/L today"
// theoretical curve (AG-2) and the probability overlay (AG-3) come later.
// See .claude/plans/options-chain.md section 12.
// ============================================================================
class StrategyAnalysisWindow {
public:
    // Snapshot of the staged cart. `legs` / `netPrice` are already scaled by
    // qty and signed exactly as OptionsChainWindow::RecomputeTicketMetrics
    // builds them, so PayoffAtExpiry(legs, netPrice, multiplier, S) reproduces
    // the ticket's own numbers. `strikes` drives the gridlines; `spot` the
    // marker; `metrics` the stats strip.
    struct Input {
        bool                    valid      = false;
        std::string             symbol;
        std::string             summary;      // e.g. "Iron Condor · 4 legs · 1.28 cr"
        double                  spot       = 0.0;
        double                  multiplier = 100.0;
        double                  netPrice   = 0.0;   // signed per-share ×qty
        int                     qty        = 1;
        std::vector<core::services::StrategyLeg> legs;
        core::services::StrategyMetrics          metrics;
        std::vector<double>     strikes;
    };

    void  SetInput(const Input& in) { m_in = in; }
    bool  Render();
    bool& open() { return m_open; }

private:
    void DrawStatsStrip();
    void DrawPayoffPlot();

    bool  m_open = false;
    Input m_in;
    bool  m_totalMode = true;   // true = total (×qty), false = per-contract
};

}  // namespace ui
