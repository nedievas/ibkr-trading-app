#pragma once
// Recent-symbols menu shared by Chart, Order Book and Options Chain: a small
// "v" button next to the symbol box opens the last symbols used. Each window
// kind keeps one list (shared by all its instances), saved in the config
// folder.

#include "imgui.h"
#include "core/services/RecentSymbols.h"
#include "core/services/state-io.h"

#include <cstring>
#include <deque>
#include <string>

namespace ui {

class RecentSymbolList {
public:
    // `file` is a name in the config folder. `legacyPath`, when given, is read
    // only if that file doesn't exist yet (where an older build kept the list).
    explicit RecentSymbolList(std::string file, std::string legacyPath = {})
        : m_file(std::move(file)) {
        using namespace core::services;
        bool exists = false;
        std::string text;
        const std::string path = ConfigFilePath(m_file);
        if (!path.empty()) text = ReadTextFile(path, &exists);
        if (!exists && !legacyPath.empty()) text = ReadTextFile(legacyPath, &exists);
        if (exists) m_items = ParseRecentSymbols(text);
    }

    const std::deque<core::services::RecentSymbol>& items() const { return m_items; }

    // `secType` ("IND", "STK") is optional; empty keeps what the list knew.
    void Add(const std::string& symbol, const std::string& secType = {}) {
        using namespace core::services;
        const std::deque<RecentSymbol> before = m_items;
        PushRecentSymbol(m_items, symbol, secType);
        if (m_items == before) return;   // already first: nothing to save
        const std::string path = ConfigFilePath(m_file);
        if (!path.empty()) AtomicWriteText(path, FormatRecentSymbols(m_items));
    }

private:
    std::string                              m_file;
    std::deque<core::services::RecentSymbol> m_items;
};

// The chart list moved from symbol_history.txt in the working folder to the
// config folder in 1.5.86; the old file is read once if the new one is missing.
inline RecentSymbolList& ChartRecentSymbols() {
    static RecentSymbolList l("recent-symbols-chart.txt", "symbol_history.txt");
    return l;
}
inline RecentSymbolList& OrderBookRecentSymbols() {
    static RecentSymbolList l("recent-symbols-orderbook.txt");
    return l;
}
inline RecentSymbolList& OptionChainRecentSymbols() {
    static RecentSymbolList l("recent-symbols-optionchain.txt");
    return l;
}

// Draws the "v" button and its popup. Returns the entry the user picked (an
// empty symbol when nothing was picked). `current` is highlighted; an index is
// tagged "IND".
inline core::services::RecentSymbol DrawRecentSymbolsButton(const char* current,
                                                            const RecentSymbolList& list) {
    core::services::RecentSymbol picked;
    if (ImGui::SmallButton("v##recent_syms")) ImGui::OpenPopup("##recent_syms_pop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recent symbols");
    if (ImGui::BeginPopup("##recent_syms_pop")) {
        ImGui::TextDisabled("Recent symbols");
        ImGui::Separator();
        if (list.items().empty()) {
            ImGui::TextDisabled("(none yet)");
        } else {
            for (const auto& e : list.items()) {
                const bool cur = current && e.symbol == current;
                if (cur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.f, 1.f));
                if (ImGui::Selectable(e.symbol.c_str())) {
                    picked = e;
                    ImGui::CloseCurrentPopup();
                }
                if (cur) ImGui::PopStyleColor();
                if (e.secType == "IND") {
                    ImGui::SameLine();
                    ImGui::TextDisabled("IND");
                }
            }
        }
        ImGui::EndPopup();
    }
    return picked;
}

} // namespace ui
