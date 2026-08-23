#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "imgui.h"
#include "core/services/IBKRUtils.h"
namespace ui {
struct SymbolResult {
    std::string symbol;
    std::string secType;
    std::string primaryExch;
    std::string currency;
};

// Set by main.cpp once the IB client is connected; cleared on disconnect.
// DrawSymbolInput calls this with the debounced search pattern.
inline std::function<void(const std::string&)> g_symbolSearchFn;

// Called from main.cpp's onSymbolSamples callback to populate the dropdown.
inline void UpdateSymbolSearchResults(std::vector<SymbolResult> r);

// Per-field search state. Each symbol input owns one (as a window member) so
// two visible fields never fight over a single shared global.
struct SymbolSearchState {
    std::vector<SymbolResult> results;
    double debounceEnd = 0.0;
    int selected = -1;
    char lastQuery[33] = {};
    char lastConfirmed[33] = {};
    char searchedQuery[33] = {};
    bool popupOpen = false;
    ImGuiID ownerID = 0;
    bool searching = false;
    bool searched = false;
    bool reclaimFocus = false;
};

namespace detail {
    inline SymbolSearchState g_ss;

    inline void CopyZ(char* dst, size_t dstSize, const char* src) {
        if (!dst || dstSize == 0) return;
        if (!src) { dst[0] = '\0'; return; }
        std::strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }
}

// Points at the field whose search is currently in flight.
inline SymbolSearchState* g_activeSymSearch = nullptr;

inline void UpdateSymbolSearchResults(std::vector<SymbolResult> r) {
    if (!g_activeSymSearch)   // stale reply — no field is awaiting results
        return;
    auto& ss = *g_activeSymSearch;
    ss.searching = false;
    ss.searched = true;
    ss.results = std::move(r);
    // Do NOT auto-highlight the first result. Enter with selected=-1 commits
    // exactly what was typed; arrow/click to pick a suggestion.
    ss.selected = -1;
    ss.popupOpen = !ss.results.empty();
}

// Reusable symbol InputText with live IB search dropdown.
//
// The list is drawn on the viewport foreground draw list — not an ImGui
// window. A real window was on top the first time it was created, then sat
// behind the chart (or at height 0) on every later search, which is why
// TSLA showed a list and COIN never did, on every field.
inline bool DrawSymbolInput(const char* id, char* buf, int bufSize, float width,
                            const std::function<void(const std::string&)>& onConfirm,
                            SymbolSearchState& state = detail::g_ss) {
    auto& ss = state;

    if (ss.reclaimFocus) {
        ImGui::SetKeyboardFocusHere();
        ss.reclaimFocus = false;
    }

    ImGui::SetNextItemWidth(width);
    const bool textChanged = ImGui::InputText(id, buf, bufSize,
                                               ImGuiInputTextFlags_CharsUppercase |
                                               ImGuiInputTextFlags_AutoSelectAll);
    const ImGuiID itemID = ImGui::GetItemID();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    const bool inputDeact = ImGui::IsItemDeactivated();
    const bool justActivated = ImGui::IsItemActivated();
    const bool inputHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (justActivated || (inputActive && ss.ownerID != 0 && ss.ownerID != itemID)) {
        ss.ownerID = itemID;
        ss.popupOpen = false;
        ss.results.clear();
        ss.debounceEnd = 0.0;
        ss.selected = -1;
        ss.searching = false;
        ss.searched = false;
        std::memset(ss.lastQuery, 0, sizeof(ss.lastQuery));
        std::memset(ss.searchedQuery, 0, sizeof(ss.searchedQuery));
        detail::CopyZ(ss.lastConfirmed, sizeof(ss.lastConfirmed), buf);
        if (g_activeSymSearch == &ss)
            g_activeSymSearch = nullptr;
    } else if (inputActive && ss.ownerID == 0) {
        ss.ownerID = itemID;
    }

    bool confirmed = false;
    if (ss.ownerID != itemID)
        return false;

    auto closePopup = [&]() {
        ss.popupOpen = false;
        ss.results.clear();
        ss.debounceEnd = 0.0;
        ss.searching = false;
        ss.searched = false;
        ss.selected = -1;
        if (g_activeSymSearch == &ss)
            g_activeSymSearch = nullptr;
    };

    auto commit = [&](const std::string& chosen) {
        detail::CopyZ(buf, (size_t)bufSize, chosen.c_str());
        detail::CopyZ(ss.lastConfirmed, sizeof(ss.lastConfirmed), chosen.c_str());
        detail::CopyZ(ss.lastQuery, sizeof(ss.lastQuery), chosen.c_str());
        onConfirm(chosen);
        closePopup();
        confirmed = true;
    };

    if (textChanged) {
        if (buf[0] == '\0') {
            closePopup();
            std::memset(ss.lastQuery, 0, sizeof(ss.lastQuery));
            std::memset(ss.searchedQuery, 0, sizeof(ss.searchedQuery));
        } else if (std::strncmp(buf, ss.lastQuery, sizeof(ss.lastQuery) - 1) != 0) {
            detail::CopyZ(ss.lastQuery, sizeof(ss.lastQuery), buf);
            ss.debounceEnd = ImGui::GetTime() + 0.25;
            ss.searched = false;
            // Keep g_activeSymSearch / results so the in-flight reply still
            // refreshes the list in place while the user types the next letter.
        }
    }

    if (ss.debounceEnd > 0.0 && ImGui::GetTime() >= ss.debounceEnd && buf[0] != '\0') {
        ss.debounceEnd = 0.0;
        detail::CopyZ(ss.searchedQuery, sizeof(ss.searchedQuery), buf);
        std::string pattern(buf);
        if (core::services::IsFuturesSymbol(pattern)) {
            std::string base = core::services::StripFuturesPrefix(pattern);
            ss.results.clear();
            ss.results.push_back({base, "FUT", "CME", "USD"});
            ss.searching = false;
            ss.searched = true;
            ss.selected = 0;
            ss.popupOpen = true;
        } else {
            ss.searching = true;
            ss.searched = false;
            g_activeSymSearch = &ss;
            if (g_symbolSearchFn) g_symbolSearchFn(std::move(pattern));
        }
    }

    if (ss.searched && ss.results.empty()
            && std::strncmp(buf, ss.searchedQuery, sizeof(ss.searchedQuery) - 1) == 0) {
        ss.popupOpen = false;
    }

    if (inputActive && ss.popupOpen && !ss.results.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            ss.selected = std::min(ss.selected + 1, (int)ss.results.size() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            ss.selected = std::max(ss.selected - 1, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closePopup();
            ss.ownerID = 0;
        }
    }

    if (!confirmed && inputActive && buf[0] != '\0'
            && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        std::string chosen;
        if (ss.popupOpen && ss.selected >= 0
                && ss.selected < (int)ss.results.size())
            chosen = ss.results[ss.selected].symbol;
        else
            chosen = std::string(buf);
        commit(chosen);
    }

    // Foreground overlay — no ImGui::Begin, so nothing to recycle, clip, or
    // bury under the chart window.
    bool popupHovered = false;
    if (ss.popupOpen && !ss.results.empty() && !confirmed) {
        const float dropW = std::max(width + 80.0f, itemMax.x - itemMin.x + 80.0f);
        const int nShow = std::min((int)ss.results.size(), 16);
        const float rowH = ImGui::GetFrameHeightWithSpacing();
        const float padX = 6.0f;
        const float padY = 4.0f;
        const float dropH = padY * 2.0f + (float)nShow * rowH;
        const ImVec2 p0(itemMin.x, itemMax.y + 2.0f);
        const ImVec2 p1(p0.x + dropW, p0.y + dropH);

        ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        const ImU32 colBg     = ImGui::GetColorU32(ImGuiCol_PopupBg);
        const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 colText   = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 colHov    = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
        const ImU32 colSel    = ImGui::GetColorU32(ImGuiCol_Header);

        dl->AddRectFilled(p0, p1, colBg, 3.0f);
        dl->AddRect(p0, p1, colBorder, 3.0f);

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        popupHovered = mouse.x >= p0.x && mouse.x < p1.x
                    && mouse.y >= p0.y && mouse.y < p1.y;

        for (int i = 0; i < nShow; ++i) {
            const auto& r = ss.results[(size_t)i];
            const ImVec2 r0(p0.x + 2.0f, p0.y + padY + (float)i * rowH);
            const ImVec2 r1(p1.x - 2.0f, r0.y + rowH);
            const bool hov = mouse.x >= r0.x && mouse.x < r1.x
                          && mouse.y >= r0.y && mouse.y < r1.y;
            const bool isSel = (i == ss.selected);
            if (hov || isSel)
                dl->AddRectFilled(r0, r1, hov ? colHov : colSel, 2.0f);

            char row[80];
            std::snprintf(row, sizeof(row), "%-8s %-4s %-8s %s",
                          r.symbol.c_str(), r.secType.c_str(),
                          r.primaryExch.c_str(), r.currency.c_str());
            const float ty = r0.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(r0.x + padX, ty), colText, row);

            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                commit(r.symbol);
        }

        if (popupHovered) {
            ImGui::GetIO().WantCaptureMouse = true;
            ImGui::SetNextFrameWantCaptureMouse(true);
        }
    }

    const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool clickedOutside = mouseClicked && !inputHovered && !popupHovered;
    if (!confirmed && clickedOutside && ss.ownerID == itemID) {
        std::string chosen;
        if (ss.popupOpen && ss.selected >= 0 && ss.selected < (int)ss.results.size())
            chosen = ss.results[ss.selected].symbol;
        else if (buf[0] != '\0')
            chosen = std::string(buf);
        if (!chosen.empty())
            commit(chosen);
        else
            closePopup();
        ss.ownerID = 0;
    } else if (inputDeact && !confirmed && ss.popupOpen && !popupHovered && !mouseClicked) {
        ss.reclaimFocus = true;
    } else if (inputDeact && !confirmed && !ss.popupOpen && !inputHovered) {
        if (buf[0] != '\0')
            commit(std::string(buf));
        ss.ownerID = 0;
    }
    return confirmed;
}
} // namespace ui
