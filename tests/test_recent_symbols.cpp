// Tests for core::services::RecentSymbols (the "v" recent-symbols menu).

#include "core/services/RecentSymbols.h"

#include <catch2/catch_test_macros.hpp>

using namespace core::services;
using List = std::deque<RecentSymbol>;

TEST_CASE("PushRecentSymbol keeps the newest first without repeats", "[recent]") {
    List l;
    PushRecentSymbol(l, "AAPL");
    PushRecentSymbol(l, "SPY");
    PushRecentSymbol(l, "AAPL");                 // a repeat moves to the front
    REQUIRE(l == List{{"AAPL", ""}, {"SPY", ""}});

    PushRecentSymbol(l, "  TSLA \r");            // trimmed
    CHECK(l.front().symbol == "TSLA");
    PushRecentSymbol(l, "");                     // ignored
    PushRecentSymbol(l, "   ");
    CHECK(l.size() == 3);
}

TEST_CASE("PushRecentSymbol keeps the security type", "[recent]") {
    List l;
    PushRecentSymbol(l, "XSP", "IND");
    PushRecentSymbol(l, "AAPL", "STK");
    PushRecentSymbol(l, "XSP");                  // no type given: the known one stays
    REQUIRE(l.front() == RecentSymbol{"XSP", "IND"});
    PushRecentSymbol(l, "XSP", "STK");           // a new type replaces it
    CHECK(l.front() == RecentSymbol{"XSP", "STK"});
}

TEST_CASE("PushRecentSymbol drops the oldest past the limit", "[recent]") {
    List l;
    for (int i = 0; i < 12; ++i) PushRecentSymbol(l, "S" + std::to_string(i));
    REQUIRE(l.size() == kMaxRecentSymbols);
    CHECK(l.front().symbol == "S11");
    CHECK(l.back().symbol  == "S2");

    PushRecentSymbol(l, "X", "", 3);
    CHECK(l == List{{"X", ""}, {"S11", ""}, {"S10", ""}});
}

TEST_CASE("Recent symbols round-trip through Format / Parse", "[recent]") {
    const List l = {{"SPX", "IND"}, {"/ES 202612", ""}, {"AAPL", "STK"}};
    CHECK(FormatRecentSymbols(l) == "SPX|IND\n/ES 202612\nAAPL|STK\n");
    CHECK(ParseRecentSymbols(FormatRecentSymbols(l)) == l);

    // A file written on Windows by an older build (no types), with blank lines
    // and a repeat.
    CHECK(ParseRecentSymbols("SPY\r\n\r\nQQQ\r\nSPY\r\n") ==
          List{{"SPY", ""}, {"QQQ", ""}});
    CHECK(ParseRecentSymbols("A\nB\nC\n", 2) == List{{"A", ""}, {"B", ""}});
    CHECK(ParseRecentSymbols("").empty());
    CHECK(ParseRecentSymbols("|IND\n").empty());   // no symbol
}
