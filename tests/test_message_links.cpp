// Tests for core::services::MessageLinks (links in IB order messages).

#include "core/services/MessageLinks.h"

#include <catch2/catch_test_macros.hpp>

using namespace core::services;

using Links = std::vector<std::string>;

// A real reject, as saved in orders-history.cfg.
static const char* const kPtpReject =
    "[201] Order rejected - reason:Not allowed to open a position:<br>This security is a "
    "Publicly Traded Partnership (PTP). To request the ability to trade this <br>or other "
    "PTP securities, please click here <br>"
    "https://ibkr.com/sso/Login?action=AccountSettings&config=PTPTrading";

TEST_CASE("The link in an IB reject is found whole", "[links]") {
    CHECK(ExtractLinks(kPtpReject) ==
          Links{"https://ibkr.com/sso/Login?action=AccountSettings&config=PTPTrading"});
}

TEST_CASE("Links in markup and text are cleaned up", "[links]") {
    CHECK(ExtractLinks("see <a href='https://a.com/x?p=1&amp;q=2'>here</a>") ==
          Links{"https://a.com/x?p=1&q=2"});
    CHECK(ExtractLinks("go to https://a.com/x. Then (http://b.com), done") ==
          Links{"https://a.com/x", "http://b.com"});
    CHECK(ExtractLinks("https://a.com and https://a.com") == Links{"https://a.com"});
}

TEST_CASE("Only web addresses count as links", "[links]") {
    CHECK(ExtractLinks("see www.ibkr.com now") == Links{"https://www.ibkr.com"});
    CHECK(ExtractLinks("https://www.ibkr.com") == Links{"https://www.ibkr.com"});
    CHECK(ExtractLinks("awww.nope and file:///etc/passwd javascript:alert(1)").empty());
    CHECK(ExtractLinks("").empty());
    CHECK(ExtractLinks("no links here").empty());
}

TEST_CASE("Message text loses its tags", "[links]") {
    CHECK(CleanMessageText("one<br>two <br> three<br/>") == "one two three");
    CHECK(CleanMessageText("<a href='https://a.com'>here</a> now") == "here now");
    CHECK(CleanMessageText("a &amp; b") == "a & b");
    CHECK(CleanMessageText("").empty());
}

TEST_CASE("Text that only looks like a tag stays", "[links]") {
    CHECK(CleanMessageText("price<limit and stop>0") == "price<limit and stop>0");
    CHECK(CleanMessageText("qty <5 or >10") == "qty <5 or >10");
    CHECK(CleanMessageText("open <br") == "open <br");
}
