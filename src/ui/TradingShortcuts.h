#pragma once
// The trading shortcuts in use, and how they meet the keyboard. Keys live in
// app-prefs.cfg as text; see core/services/TradingShortcuts.h.

#include "imgui.h"
#include "imgui_internal.h"   // NavMoveRequestCancel
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
    if (ImGui::IsKeyChordPressed(chord)) return true;
    // Either Enter key will do.
    return key == ImGuiKey_Enter &&
           ImGui::IsKeyChordPressed((chord & ImGuiMod_Mask_) | ImGuiKey_KeypadEnter);
}

// Inside a window: may this action fire now? With the cursor in a text field,
// only if it is a number field the caller says is in use and the chord can't be
// typed there, so a symbol box or filter keeps its keys. Otherwise the window
// must have focus, with no popup open.
inline bool ShortcutAllowed(core::services::ShortcutAction action, bool numberFieldActive) {
    if (ImGui::GetIO().WantTextInput)
        return numberFieldActive && core::services::ShortcutWorksInNumberField(
                                        TradingShortcuts()[static_cast<int>(action)]);
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                  ImGuiFocusedFlags_NoPopupHierarchy);
}

// Size and price stepping: this window has focus and nothing is being typed
// (Ctrl / Shift + arrow move the cursor in a text field).
inline bool StepKeysActive() {
    return !ImGui::GetIO().WantTextInput &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                  ImGuiFocusedFlags_NoPopupHierarchy);
}

// Ctrl + arrow = small step, Shift + arrow = big step, repeating while held.
// Signed: `up` adds, `down` subtracts. The app's keyboard navigation also reads
// the arrows, so a step cancels its move.
inline int ArrowStep(ImGuiKey up, ImGuiKey down, int small, int big) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyAlt || io.KeyCtrl == io.KeyShift) return 0;
    const int step = io.KeyCtrl ? small : big;
    int delta = 0;
    if (ImGui::IsKeyPressed(up, true))        delta = step;
    else if (ImGui::IsKeyPressed(down, true)) delta = -step;
    if (delta != 0) ImGui::NavMoveRequestCancel();
    return delta;
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
