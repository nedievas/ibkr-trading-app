#pragma once
// Rebindable trading shortcuts. Pure logic, no ImGui / IB dependency, so
// tests-core covers it. A key is held by its ImGui name ("B", "F5", "Keypad1"),
// and a chord is saved as text: "B", "Ctrl+Shift+F5".

#include <array>
#include <string>

namespace core::services {

enum class ShortcutAction : int { ArmBuy = 0, ArmSell, SendOptionOrder, UpdateOrder, Count };
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
    m[static_cast<int>(ShortcutAction::SendOptionOrder)].key = "C";
    m[static_cast<int>(ShortcutAction::UpdateOrder)].key     = "U";
    return m;
}

inline const char* ShortcutActionLabel(int action) {
    switch (static_cast<ShortcutAction>(action)) {
        case ShortcutAction::ArmBuy:  return "Arm Buy on chart";
        case ShortcutAction::ArmSell: return "Arm Sell on chart";
        case ShortcutAction::SendOptionOrder: return "Send option order";
        default:                      return "Update open order";
    }
}

// Key in app-prefs.cfg.
inline const char* ShortcutPrefKey(int action) {
    switch (static_cast<ShortcutAction>(action)) {
        case ShortcutAction::ArmBuy:  return "SHORTCUT_ARM_BUY";
        case ShortcutAction::ArmSell: return "SHORTCUT_ARM_SELL";
        case ShortcutAction::SendOptionOrder: return "SHORTCUT_SEND_OPTION_ORDER";
        default:                      return "SHORTCUT_UPDATE_ORDER";
    }
}

// A chord that can't be typed into a number field, so it may fire from one (type
// a size or price, then press the key). Letters can't be typed there, except E
// in a price (scientific notation). Ctrl + letter (select all, copy, paste) and
// digits stay with the field, as does plain Enter, which leaves it.
inline bool ShortcutWorksInNumberField(const ShortcutChord& c) {
    if (c.key == "Enter" || c.key == "KeypadEnter") return c.ctrl || c.shift || c.alt;
    if (c.key.size() >= 2 && c.key[0] == 'F' && c.key[1] >= '0' && c.key[1] <= '9') return true;
    return c.key.size() == 1 && c.key[0] >= 'A' && c.key[0] <= 'Z' && c.key != "E" && !c.ctrl;
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
// Space, arrows), window switching (Tab), the text-size shortcuts, and Ctrl or
// Shift + arrow, which step the size and price (fixed, see ui/TradingShortcuts.h).
inline bool IsReservedShortcut(const ShortcutChord& c) {
    if (c.key == "Tab") return true;
    if (!c.ctrl && !c.shift && !c.alt &&
        (c.key == "Enter" || c.key == "KeypadEnter" || c.key == "Escape" || c.key == "Space" ||
         c.key == "LeftArrow" || c.key == "RightArrow" || c.key == "UpArrow" || c.key == "DownArrow"))
        return true;
    if (c.ctrl != c.shift && !c.alt &&
        (c.key == "LeftArrow" || c.key == "RightArrow" || c.key == "UpArrow" || c.key == "DownArrow"))
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
