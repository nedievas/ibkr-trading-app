#pragma once
// Listed months of each futures root, shared by every window: main.cpp fills
// it from IB (contract details for the root, then a volume probe of the first
// months), Chart and Order Book read it for their month picker. "/CL" with no
// month follows `active`, the month with the most volume.

#include "imgui.h"
#include "core/services/IBKRUtils.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

struct FuturesRootMonths {
    enum class State { None, Looking, Probing, Ready, Failed };
    State state = State::None;
    std::vector<core::services::FuturesMonth> months;   // sorted, not expired
    std::vector<double> volumes;                         // parallel; -1 unknown
    int    active   = -1;      // index into months
    double deadline = 0.0;     // probing ends by this time (glfwGetTime)
};

// Keyed by the app's root ("CL", "SIL").
inline std::unordered_map<std::string, FuturesRootMonths>& FuturesMonthCache() {
    static std::unordered_map<std::string, FuturesRootMonths> cache;
    return cache;
}

// The default month for a root, "" while unknown.
inline std::string ActiveFuturesMonth(const std::string& root) {
    auto& cache = FuturesMonthCache();
    auto it = cache.find(root);
    if (it == cache.end() || it->second.state != FuturesRootMonths::State::Ready) return {};
    const auto& r = it->second;
    if (r.active < 0 || r.active >= (int)r.months.size()) return {};
    return r.months[r.active].month;
}

// IB's months for this futures symbol's root are in, so the picker has
// something to show.
inline bool FuturesMonthPickerReady(const std::string& symbol) {
    using namespace core::services;
    if (!IsFuturesSymbol(symbol)) return false;
    std::string root, month;
    ParseFuturesSymbol(symbol, root, month);
    auto& cache = FuturesMonthCache();
    auto it = cache.find(root);
    return it != cache.end() && it->second.state == FuturesRootMonths::State::Ready &&
           !it->second.months.empty();
}

// A small combo next to the symbol box for a futures symbol: "Active" (the
// bare root, follows the volume default) and each listed month by its local
// symbol. Returns the symbol picked, or "" (nothing drawn for a non-future, or
// before IB's months have arrived).
inline std::string DrawFuturesMonthPicker(const char* id, const std::string& symbol,
                                          float width) {
    using namespace core::services;
    if (!IsFuturesSymbol(symbol)) return {};
    std::string root, month;
    ParseFuturesSymbol(symbol, root, month);
    auto& cache = FuturesMonthCache();
    auto it = cache.find(root);
    if (it == cache.end() || it->second.state != FuturesRootMonths::State::Ready ||
        it->second.months.empty())
        return {};
    const FuturesRootMonths& r = it->second;

    auto labelOf = [](const FuturesMonth& fm) {
        return fm.localSymbol.empty() ? fm.month : fm.localSymbol;
    };
    std::string activeLabel;
    if (r.active >= 0 && r.active < (int)r.months.size())
        activeLabel = labelOf(r.months[r.active]);

    std::string preview;
    if (month.empty()) {
        preview = activeLabel.empty() ? "Active" : activeLabel + " (active)";
    } else {
        preview = month;
        for (const FuturesMonth& fm : r.months)
            if (fm.month == month) { preview = labelOf(fm); break; }
    }

    std::string picked;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        const std::string activeItem =
            activeLabel.empty() ? std::string("Active") : "Active (" + activeLabel + ")";
        if (ImGui::Selectable(activeItem.c_str(), month.empty()))
            picked = FormatFuturesSymbol(root, "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Follow the month with the most volume");
        ImGui::Separator();
        const int shown = std::min<int>((int)r.months.size(), 12);
        for (int i = 0; i < shown; ++i) {
            const FuturesMonth& fm = r.months[i];
            std::string label = labelOf(fm);
            if (i < (int)r.volumes.size() && r.volumes[i] >= 0.0) {
                char vol[32];
                std::snprintf(vol, sizeof(vol), "   vol %.0f", r.volumes[i]);
                label += vol;
            }
            label += "##" + fm.month;
            if (ImGui::Selectable(label.c_str(), fm.month == month))
                picked = FormatFuturesSymbol(root, fm.month);
            if (ImGui::IsItemHovered() && !fm.lastTradeDate.empty())
                ImGui::SetTooltip("%s, last trade %s", fm.month.c_str(),
                                  fm.lastTradeDate.c_str());
        }
        ImGui::EndCombo();
    }
    return picked == symbol ? std::string() : picked;
}

} // namespace ui
