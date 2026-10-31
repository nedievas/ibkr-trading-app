#pragma once
// The trading shortcuts in use, and how they meet the keyboard. Keys live in
// app-prefs.cfg as text; see core/services/TradingShortcuts.h.

#include "imgui.h"
#include "core/services/TradingShortcuts.h"

#include <cstring>

namespace ui {

inline core::services::ShortcutMap& TradingShortcuts() {
    static core::services::ShortcutMap s_map = core::services::DefaultShortcuts();
    return s_map;
}

inline bool IsBindableKey(int key) {
    return key >= ImGuiKey_Tab && key < ImGuiKey_GamepadStart &&
           !(key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper);
}

inline ImGuiKey ShortcutKeyFromName(const std::string& name) {
    for (int k = ImGuiKey_Tab; k < ImGuiKey_GamepadStart; ++k)
        if (IsBindableKey(k) && name == ImGui::GetKeyName(static_cast<ImGuiKey>(k)))
            return static_cast<ImGuiKey>(k);
    return ImGuiKey_None;
}

// No key repeat: holding the key fires once.
inline bool ShortcutPressed(core::services::ShortcutAction action) {
    const auto& c = TradingShortcuts()[static_cast<int>(action)];
    if (c.empty()) return false;
    const ImGuiKey key = ShortcutKeyFromName(c.key);
    if (key == ImGuiKey_None) return false;
    ImGuiKeyChord chord = key;
    if (c.ctrl)  chord |= ImGuiMod_Ctrl;
    if (c.shift) chord |= ImGuiMod_Shift;
    if (c.alt)   chord |= ImGuiMod_Alt;
    return ImGui::IsKeyChordPressed(chord);
}

// The chord pressed this frame, for rebinding. Empty when none was.
inline core::services::ShortcutChord CaptureShortcutChord() {
    core::services::ShortcutChord c;
    for (int k = ImGuiKey_Tab; k < ImGuiKey_GamepadStart; ++k) {
        if (!IsBindableKey(k) || !ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
        const ImGuiIO& io = ImGui::GetIO();
        c.ctrl  = io.KeyCtrl;
        c.shift = io.KeyShift;
        c.alt   = io.KeyAlt;
        c.key   = ImGui::GetKeyName(static_cast<ImGuiKey>(k));
        break;
    }
    return c;
}

} // namespace ui
