// Tests for core::services::TradingShortcuts (rebindable keys).

#include "core/services/TradingShortcuts.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace core::services;

TEST_CASE("Shortcut chord text round-trips", "[shortcuts]") {
    ShortcutChord c;
    c.key = "B";
    CHECK(FormatChord(c) == "B");
    CHECK(ParseChord("B") == c);

    ShortcutChord all;
    all.ctrl = all.shift = all.alt = true;
    all.key = "F5";
    CHECK(FormatChord(all) == "Ctrl+Shift+Alt+F5");
    CHECK(ParseChord("Ctrl+Shift+Alt+F5") == all);
    CHECK(FormatChord(ShortcutChord{}).empty());
}

TEST_CASE("Malformed shortcut text gives no chord", "[shortcuts]") {
    CHECK(ParseChord("").empty());
    CHECK(ParseChord("Ctrl+").empty());
    CHECK(ParseChord("Meta+B").empty());
    CHECK(ParseChord("+").empty());
}

TEST_CASE("Default shortcuts are set and don't clash", "[shortcuts]") {
    const ShortcutMap m = DefaultShortcuts();
    for (int i = 0; i < kShortcutCount; ++i) {
        CHECK_FALSE(m[i].empty());
        CHECK_FALSE(IsReservedShortcut(m[i]));
        CHECK(FindShortcutConflict(m, i, m[i]) == -1);
    }
}

TEST_CASE("Sending an option order defaults to C", "[shortcuts]") {
    const ShortcutMap m = DefaultShortcuts();
    CHECK(FormatChord(m[static_cast<int>(ShortcutAction::SendOptionOrder)]) == "C");
    CHECK(std::string(ShortcutPrefKey(static_cast<int>(ShortcutAction::SendOptionOrder))) ==
          "SHORTCUT_SEND_OPTION_ORDER");
}

TEST_CASE("Updating an open order defaults to U", "[shortcuts]") {
    const ShortcutMap m = DefaultShortcuts();
    CHECK(FormatChord(m[static_cast<int>(ShortcutAction::UpdateOrder)]) == "U");
    CHECK(std::string(ShortcutPrefKey(static_cast<int>(ShortcutAction::UpdateOrder))) ==
          "SHORTCUT_UPDATE_ORDER");
}

TEST_CASE("Only chords that can't be typed work from a number field", "[shortcuts]") {
    for (const char* t : { "C", "B", "S", "U", "Shift+C", "Alt+U", "Ctrl+Enter", "Shift+Enter",
                           "Alt+KeypadEnter", "F5", "Ctrl+F12" })
        CHECK(ShortcutWorksInNumberField(ParseChord(t)));
    for (const char* t : { "E", "Shift+E", "5", "Keypad5", "Enter", "KeypadEnter", "Ctrl+A",
                           "Ctrl+C", "Ctrl+V", "Minus", "Tab" })
        CHECK_FALSE(ShortcutWorksInNumberField(ParseChord(t)));
}

TEST_CASE("A chord already on another action is a conflict", "[shortcuts]") {
    const ShortcutMap m = DefaultShortcuts();
    const int buy  = static_cast<int>(ShortcutAction::ArmBuy);
    const int sell = static_cast<int>(ShortcutAction::ArmSell);
    CHECK(FindShortcutConflict(m, buy, ParseChord("S")) == sell);
    CHECK(FindShortcutConflict(m, buy, ParseChord("B")) == -1);       // its own key
    CHECK(FindShortcutConflict(m, buy, ParseChord("Shift+S")) == -1); // other modifiers
    CHECK(FindShortcutConflict(m, buy, ShortcutChord{}) == -1);       // not set
}

TEST_CASE("Keys the app already uses are reserved", "[shortcuts]") {
    for (const char* t : { "Enter", "KeypadEnter", "Escape", "Space", "UpArrow", "Tab",
                           "Ctrl+Tab", "Ctrl+Equal", "Ctrl+Shift+Equal", "Ctrl+Minus",
                           "Ctrl+0", "Ctrl+KeypadAdd", "Ctrl+KeypadSubtract", "Ctrl+Keypad0",
                           "Ctrl+UpArrow", "Shift+DownArrow", "Ctrl+LeftArrow", "Shift+RightArrow" })
        CHECK(IsReservedShortcut(ParseChord(t)));
    for (const char* t : { "B", "F5", "Ctrl+B", "Shift+Enter", "Minus", "0", "Alt+Equal",
                           "Alt+UpArrow", "Ctrl+Shift+UpArrow" })
        CHECK_FALSE(IsReservedShortcut(ParseChord(t)));
}
