#pragma once
// User-saved window layouts (Windows > Presets > Save current layout...).
// Pure logic, no ImGui / IB dependency, so tests-core covers it.
//
// A preset holds ImGui's layout text (window positions, sizes and docking, as
// ImGui::SaveIniSettingsToMemory gives it) plus the open state and group of
// every window instance. Saved in window-presets.cfg as one WINDOW:<name>
// block per preset: LAYOUT is the layout text on one line (backslash and
// newline escaped), and W_<kind>_<index> is "<open>,<group>".

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/services/state-io.h"

namespace core::services {

struct PresetWindow {
    std::string kind;    // "chart", "dom", "portfolio", ...
    int  index = 0;      // instance index; 0 for single windows
    bool open  = false;
    int  group = 0;      // 0 = the window has no group
};

struct CustomWindowPreset {
    std::string name;
    std::string layout;  // ImGui ini text
    std::vector<PresetWindow> windows;
};

// One line: '\' -> "\\", newline -> "\n", carriage returns dropped.
inline std::string EscapeMultiline(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    for (char c : s) {
        if (c == '\\')      out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

inline std::string UnescapeMultiline(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += (n == 'n') ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

inline const PresetWindow* FindPresetWindow(const CustomWindowPreset& p,
                                            const std::string& kind, int index) {
    for (const auto& w : p.windows)
        if (w.kind == kind && w.index == index) return &w;
    return nullptr;
}

// Highest saved index of `kind`, -1 when none.
inline int MaxPresetIndex(const CustomWindowPreset& p, const std::string& kind) {
    int m = -1;
    for (const auto& w : p.windows)
        if (w.kind == kind) m = std::max(m, w.index);
    return m;
}

inline std::string FormatCustomPresets(const std::vector<CustomWindowPreset>& presets) {
    std::vector<StateBlock> blocks;
    for (const auto& p : presets) {
        if (p.name.empty()) continue;
        StateBlock b;
        b.windowName = p.name;
        SetString(b, "LAYOUT", EscapeMultiline(p.layout));
        for (const auto& w : p.windows) {
            if (w.kind.empty() || w.index < 0) continue;
            char val[32];
            std::snprintf(val, sizeof(val), "%d,%d", w.open ? 1 : 0, w.group);
            SetString(b, "W_" + w.kind + "_" + std::to_string(w.index), val);
        }
        blocks.push_back(std::move(b));
    }
    return FormatStateBlocks(blocks);
}

inline std::vector<CustomWindowPreset> ParseCustomPresets(const std::string& text) {
    std::vector<CustomWindowPreset> out;
    for (const auto& b : ParseStateBlocks(text)) {
        if (b.windowName.empty()) continue;
        CustomWindowPreset p;
        p.name   = b.windowName;
        p.layout = UnescapeMultiline(GetString(b, "LAYOUT", ""));
        for (const auto& [key, val] : b.fields) {
            if (key.rfind("W_", 0) != 0) continue;
            const size_t us = key.rfind('_');
            if (us <= 2 || us + 1 >= key.size()) continue;
            PresetWindow w;
            w.kind = key.substr(2, us - 2);
            int open = 0, group = 0;
            if (std::sscanf(key.c_str() + us + 1, "%d", &w.index) != 1 || w.index < 0) continue;
            if (std::sscanf(val.c_str(), "%d,%d", &open, &group) < 1) continue;
            w.open  = open != 0;
            w.group = group;
            p.windows.push_back(std::move(w));
        }
        // fields is unordered; keep the windows in a stable order.
        std::sort(p.windows.begin(), p.windows.end(), [](const auto& a, const auto& b) {
            return a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
        });
        // A later block with the same name replaces an earlier one.
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const auto& q) { return q.name == p.name; });
        if (it != out.end()) *it = std::move(p);
        else out.push_back(std::move(p));
    }
    return out;
}

// Replace the preset with the same name in place, or append it.
inline void UpsertCustomPreset(std::vector<CustomWindowPreset>& presets,
                               CustomWindowPreset p) {
    auto it = std::find_if(presets.begin(), presets.end(),
                           [&](const auto& q) { return q.name == p.name; });
    if (it != presets.end()) *it = std::move(p);
    else presets.push_back(std::move(p));
}

inline bool RemoveCustomPreset(std::vector<CustomWindowPreset>& presets,
                               const std::string& name) {
    auto it = std::find_if(presets.begin(), presets.end(),
                           [&](const auto& q) { return q.name == name; });
    if (it == presets.end()) return false;
    presets.erase(it);
    return true;
}

} // namespace core::services
