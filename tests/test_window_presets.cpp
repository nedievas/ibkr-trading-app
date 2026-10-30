// Tests for core::services::WindowPresets (user-saved window layouts).

#include "core/services/WindowPresets.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace core::services;

TEST_CASE("EscapeMultiline round-trips newlines and backslashes", "[presets]") {
    const std::string ini = "[Window][Chart AAPL G1###chart0]\r\nPos=60,60\nSize=800,600\n"
                            "Path=C:\\tmp\\n\n";
    const std::string esc = EscapeMultiline(ini);
    CHECK(esc.find('\n') == std::string::npos);
    CHECK(esc.find('\r') == std::string::npos);
    CHECK(UnescapeMultiline(esc) ==
          "[Window][Chart AAPL G1###chart0]\nPos=60,60\nSize=800,600\nPath=C:\\tmp\\n\n");
    CHECK(UnescapeMultiline(EscapeMultiline("")).empty());
}

TEST_CASE("Custom presets survive format and parse", "[presets]") {
    CustomWindowPreset a;
    a.name   = "Morning: options";
    a.layout = "[Window][Portfolio###portfolio]\nPos=10,20\nSize=300,200\n\n[Docking][Data]\n";
    a.windows = { { "chart", 0, true, 1 }, { "chart", 2, false, 3 },
                  { "dom", 1, true, 2 }, { "optionschain", 0, true, 4 },
                  { "orders", 0, false, 0 } };
    CustomWindowPreset b;
    b.name = "Evening";
    b.windows = { { "notifications", 0, true, 0 } };

    const auto parsed = ParseCustomPresets(FormatCustomPresets({ a, b }));
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0].name == "Morning: options");
    CHECK(parsed[0].layout == a.layout);
    REQUIRE(parsed[0].windows.size() == 5);
    const auto* c2 = FindPresetWindow(parsed[0], "chart", 2);
    REQUIRE(c2);
    CHECK_FALSE(c2->open);
    CHECK(c2->group == 3);
    const auto* oc = FindPresetWindow(parsed[0], "optionschain", 0);
    REQUIRE(oc);
    CHECK(oc->open);
    CHECK(oc->group == 4);
    CHECK(FindPresetWindow(parsed[0], "dom", 0) == nullptr);
    CHECK(MaxPresetIndex(parsed[0], "chart") == 2);
    CHECK(MaxPresetIndex(parsed[0], "scanner") == -1);
    CHECK(parsed[1].name == "Evening");
    CHECK(parsed[1].layout.empty());
}

TEST_CASE("Custom presets: malformed lines skipped, later duplicate wins", "[presets]") {
    const std::string text =
        "WINDOW:One\n"
        "W_chart_x:1,1\n"     // bad index
        "W_:1,1\n"            // no kind
        "W_dom_0:junk\n"      // bad value
        "W_dom_1:1,2\n"
        "WINDOW:One\n"
        "W_scanner_0:1,5\n";
    const auto parsed = ParseCustomPresets(text);
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].windows.size() == 1);
    CHECK(parsed[0].windows[0].kind == "scanner");
    CHECK(parsed[0].windows[0].group == 5);
    CHECK(ParseCustomPresets("").empty());
}

TEST_CASE("UpsertCustomPreset replaces by name, RemoveCustomPreset deletes", "[presets]") {
    std::vector<CustomWindowPreset> list;
    UpsertCustomPreset(list, { "A", "x", {} });
    UpsertCustomPreset(list, { "B", "y", {} });
    UpsertCustomPreset(list, { "A", "z", {} });
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "A");
    CHECK(list[0].layout == "z");
    CHECK(RemoveCustomPreset(list, "A"));
    CHECK_FALSE(RemoveCustomPreset(list, "missing"));
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "B");
}
