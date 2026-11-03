// Tests for core::services::PriceAlerts (chart price alerts).

#include "core/services/PriceAlerts.h"

#include <catch2/catch_test_macros.hpp>

using namespace core::services;

TEST_CASE("AlertIsAbove takes the side from the market price", "[alerts]") {
    CHECK(AlertIsAbove(105.0, 100.0));
    CHECK_FALSE(AlertIsAbove(95.0, 100.0));
    CHECK(AlertIsAbove(100.0, 100.0));    // at the market counts as above
    CHECK(AlertIsAbove(95.0, 0.0));       // no market price yet
}

TEST_CASE("AlertTriggered fires when the price reaches the level", "[alerts]") {
    PriceAlert up{1, "AAPL", 105.0, true};
    CHECK_FALSE(AlertTriggered(up, 104.99));
    CHECK(AlertTriggered(up, 105.0));
    CHECK(AlertTriggered(up, 106.0));     // a gap through the level fires too

    PriceAlert down{2, "AAPL", 95.0, false};
    CHECK_FALSE(AlertTriggered(down, 95.01));
    CHECK(AlertTriggered(down, 95.0));
    CHECK(AlertTriggered(down, 90.0));

    CHECK_FALSE(AlertTriggered(up, 0.0));          // no price
    CHECK_FALSE(AlertTriggered(PriceAlert{3, "X", 0.0, true}, 10.0));
}

TEST_CASE("Price alerts round-trip through the file text", "[alerts]") {
    std::vector<PriceAlert> in = {
        {7, "AAPL", 187.42, true},
        {9, "SPX", 6500.25, false},
        {11, "", 10.0, true},              // no symbol: not written
        {12, "MSFT", 0.0, true},           // no price: not written
    };
    std::string text = FormatPriceAlerts(in);
    CHECK(text == "AAPL|187.42|A\nSPX|6500.25|B\n");

    auto out = ParsePriceAlerts(text);
    REQUIRE(out.size() == 2);
    CHECK(out[0].id == 1);
    CHECK(out[0].symbol == "AAPL");
    CHECK(out[0].price == 187.42);
    CHECK(out[0].above);
    CHECK(out[1].id == 2);
    CHECK(out[1].symbol == "SPX");
    CHECK_FALSE(out[1].above);
}

TEST_CASE("ParsePriceAlerts skips bad lines", "[alerts]") {
    auto out = ParsePriceAlerts(
        "AAPL|187.42|A\r\n"   // Windows line ending
        "\n"
        "|10|A\n"              // no symbol
        "MSFT|abc|A\n"         // bad price
        "MSFT|-5|B\n"          // negative price
        "MSFT|10|X\n"          // bad side
        "MSFT|10\n"            // no side
        "TSLA|250|B\n");
    REQUIRE(out.size() == 2);
    CHECK(out[0].symbol == "AAPL");
    CHECK(out[1].symbol == "TSLA");
    CHECK(out[1].price == 250.0);
}
