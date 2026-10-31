#pragma once
// Rebindable trading shortcuts. Pure logic, no ImGui / IB dependency, so
// tests-core covers it. A key is held by its ImGui name ("B", "F5", "Keypad1"),
// and a chord is saved as text: "B", "Ctrl+Shift+F5".

#include <array>
#include <string>

namespace core::services {

enum class ShortcutAction : int { ArmBuy = 0, ArmSell, Count };
inline constexpr int kShortcutCount = static_cast<int>(ShortcutAction::Count);

struct ShortcutChord {
    bool ctrl = false, shift = false, alt = false;
    std::string key;   // empty = not set
    [[nodiscard]] bool empty() const { return key.empty(); }
    bool operator==(const ShortcutChord& o) const {
        return ctrl == o.ctrl && shift == o.shift && alt == o.alt && key == o.key;
    }
};

using ShortcutMap = std::array<ShortcutChord, kShortcutCount>;

inline ShortcutMap DefaultShortcuts() {
    ShortcutMap m;
    m[static_cast<int>(ShortcutAction::ArmBuy)].key  = "B";
    m[static_cast<int>(ShortcutAction::ArmSell)].key = "S";
    return m;
}

inline const char* ShortcutActionLabel(int action) {
    return action == static_cast<int>(ShortcutAction::ArmBuy) ? "Arm Buy on chart"
                                                              : "Arm Sell on chart";
}

// Key in app-prefs.cfg.
inline const char* ShortcutPrefKey(int action) {
    return action == static_cast<int>(ShortcutAction::ArmBuy) ? "SHORTCUT_ARM_BUY"
                                                              : "SHORTCUT_ARM_SELL";
}

inline std::string FormatChord(const ShortcutChord& c) {
    if (c.empty()) return {};
    std::string s;
    if (c.ctrl)  s += "Ctrl+";
    if (c.shift) s += "Shift+";
    if (c.alt)   s += "Alt+";
    return s + c.key;
}

// Empty chord on malformed text (an unknown modifier or no key).
inline ShortcutChord ParseChord(const std::string& text) {
    ShortcutChord c;
    size_t pos = 0;
    for (size_t plus; (plus = text.find('+', pos)) != std::string::npos; pos = plus + 1) {
        const std::string mod = text.substr(pos, plus - pos);
        if (mod == "Ctrl")       c.ctrl = true;
        else if (mod == "Shift") c.shift = true;
        else if (mod == "Alt")   c.alt = true;
        else return {};
    }
    c.key = text.substr(pos);
    if (c.key.empty()) return {};
    return c;
}

// Keys the app already uses: dialogs and keyboard navigation (Enter, Escape,
// Space, arrows), window switching (Tab) and the text-size shortcuts.
inline bool IsReservedShortcut(const ShortcutChord& c) {
    if (c.key == "Tab") return true;
    if (!c.ctrl && !c.shift && !c.alt &&
        (c.key == "Enter" || c.key == "KeypadEnter" || c.key == "Escape" || c.key == "Space" ||
         c.key == "LeftArrow" || c.key == "RightArrow" || c.key == "UpArrow" || c.key == "DownArrow"))
        return true;
    if (c.ctrl && !c.alt && c.key == "Equal") return true;          // Ctrl (+ Shift) + =
    if (c.ctrl && !c.shift && !c.alt &&
        (c.key == "Minus" || c.key == "0" || c.key == "KeypadAdd" ||
         c.key == "KeypadSubtract" || c.key == "Keypad0"))
        return true;
    return false;
}

// The other action already on this chord, or -1.
inline int FindShortcutConflict(const ShortcutMap& map, int action, const ShortcutChord& c) {
    if (c.empty()) return -1;
    for (int i = 0; i < kShortcutCount; ++i)
        if (i != action && map[i] == c) return i;
    return -1;
}

} // namespace core::services
