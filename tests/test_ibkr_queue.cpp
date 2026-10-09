#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/services/IBKRClient.h"
#include "Contract.h"
#include "Order.h"
#include "OrderState.h"
#include "Execution.h"
#include "CommissionAndFeesReport.h"

using namespace core::services;

// ---------------------------------------------------------------------------
// TestableIBKRClient
//   Subclass that exposes the protected Push() method so tests can inject
//   messages directly without a live IB connection.
//   The constructor calls IBKRClient() which creates an EClientSocket and
//   EReaderOSSignal — both are safe to construct without ever connecting.
// ---------------------------------------------------------------------------
class TestableIBKRClient : public IBKRClient {
public:
    void inject(IBMessage msg) { Push(std::move(msg)); }
};

// ── MsgConnection ─────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgConnection to onConnectionChanged", "[queue][connection]") {
    TestableIBKRClient client;

    bool   receivedConnected = false;
    bool   callbackFired     = false;
    std::string receivedInfo;

    client.onConnectionChanged = [&](bool connected, const std::string& info) {
        callbackFired     = true;
        receivedConnected = connected;
        receivedInfo      = info;
    };

    client.inject(MsgConnection{true, "Connected"});
    client.ProcessMessages();

    REQUIRE(callbackFired);
    REQUIRE(receivedConnected);
    REQUIRE(receivedInfo == "Connected");
}

TEST_CASE("ProcessMessages dispatches MsgConnection disconnect event", "[queue][connection]") {
    TestableIBKRClient client;
    bool connected = true;

    client.onConnectionChanged = [&](bool c, const std::string&) { connected = c; };
    client.inject(MsgConnection{false, "Connection closed by remote host"});
    client.ProcessMessages();

    REQUIRE_FALSE(connected);
}

// ── MsgTickPrice ──────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgTickPrice to onTickPrice", "[queue][tick]") {
    TestableIBKRClient client;

    int    receivedTicker = -1;
    int    receivedField  = -1;
    double receivedPrice  = -1.0;

    client.onTickPrice = [&](int ticker, int field, double price) {
        receivedTicker = ticker;
        receivedField  = field;
        receivedPrice  = price;
    };

    client.inject(MsgTickPrice{42, 4, 150.25});
    client.ProcessMessages();

    REQUIRE(receivedTicker == 42);
    REQUIRE(receivedField  == 4);
    REQUIRE(receivedPrice  == Catch::Approx(150.25));
}

// ── MsgTickSize ───────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgTickSize to onTickSize", "[queue][tick]") {
    TestableIBKRClient client;

    int    receivedTicker = -1;
    int    receivedField  = -1;
    double receivedSize   = -1.0;

    client.onTickSize = [&](int ticker, int field, double size) {
        receivedTicker = ticker;
        receivedField  = field;
        receivedSize   = size;
    };

    client.inject(MsgTickSize{10, 8, 500.0});
    client.ProcessMessages();

    REQUIRE(receivedTicker == 10);
    REQUIRE(receivedField  == 8);
    REQUIRE(receivedSize   == Catch::Approx(500.0));
}

// ── MsgTickString ───────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgTickString to onTickString", "[queue][tick]") {
    TestableIBKRClient client;

    int         receivedTicker = -1;
    int         receivedField  = -1;
    std::string receivedValue;

    client.onTickString = [&](int ticker, int field, const std::string& value) {
        receivedTicker = ticker;
        receivedField  = field;
        receivedValue  = value;
    };

    client.inject(MsgTickString{7, 47, "MKTCAP=1234.5;PEEXCLXOR=15.2"});
    client.ProcessMessages();

    REQUIRE(receivedTicker == 7);
    REQUIRE(receivedField  == 47);
    REQUIRE(receivedValue  == "MKTCAP=1234.5;PEEXCLXOR=15.2");
}

TEST_CASE("ProcessMessages MsgTickString null callback does not crash", "[queue][tick]") {
    TestableIBKRClient client;
    client.inject(MsgTickString{1, 47, "MKTCAP=100.0"});
    REQUIRE_NOTHROW(client.ProcessMessages());
}

// ── MsgBar ────────────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgBar to onBarData", "[queue][bar]") {
    TestableIBKRClient client;

    int         receivedReqId = -1;
    core::Bar   receivedBar   = {};
    bool        receivedDone  = false;
    bool        receivedLive  = false;

    client.onBarData = [&](int reqId, const core::Bar& bar, bool done, bool live) {
        receivedReqId = reqId;
        receivedBar   = bar;
        receivedDone  = done;
        receivedLive  = live;
    };

    core::Bar bar;
    bar.timestamp = 1705329600.0;
    bar.open  = 100.0;
    bar.high  = 105.0;
    bar.low   =  98.0;
    bar.close = 103.0;
    bar.volume = 1'000'000.0;

    client.inject(MsgBar{7, bar, false, true});
    client.ProcessMessages();

    REQUIRE(receivedReqId      == 7);
    REQUIRE(receivedBar.open   == Catch::Approx(100.0));
    REQUIRE(receivedBar.high   == Catch::Approx(105.0));
    REQUIRE(receivedBar.low    == Catch::Approx( 98.0));
    REQUIRE(receivedBar.close  == Catch::Approx(103.0));
    REQUIRE(receivedBar.volume == Catch::Approx(1'000'000.0));
    REQUIRE_FALSE(receivedDone);
    REQUIRE(receivedLive);
}

TEST_CASE("ProcessMessages dispatches done-sentinel MsgBar correctly", "[queue][bar]") {
    TestableIBKRClient client;

    bool doneFired = false;
    int  doneReqId = -1;

    client.onBarData = [&](int reqId, const core::Bar&, bool done, bool) {
        if (done) { doneFired = true; doneReqId = reqId; }
    };

    client.inject(MsgBar{3, {}, true, false});
    client.ProcessMessages();

    REQUIRE(doneFired);
    REQUIRE(doneReqId == 3);
}

// ── MsgDepth ──────────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgDepth to onDepthUpdate", "[queue][depth]") {
    TestableIBKRClient client;

    int    id_out  = -1;
    bool   isBid   = false;
    int    pos_out = -1;
    int    op_out  = -1;
    double price_out = -1.0;
    double size_out  = -1.0;
    std::string exch_out;
    bool   smart_out = false;

    client.onDepthUpdate = [&](int id, bool bid, int pos, int op, double price, double size,
                                const std::string& exchange, bool isSmartDepth) {
        id_out    = id;
        isBid     = bid;
        pos_out   = pos;
        op_out    = op;
        price_out = price;
        size_out  = size;
        exch_out  = exchange;
        smart_out = isSmartDepth;
    };

    client.inject(MsgDepth{120, true, 0, 1, 149.50, 200.0, "NYSE", false});
    client.ProcessMessages();

    REQUIRE(id_out    == 120);
    REQUIRE(isBid     == true);
    REQUIRE(pos_out   == 0);
    REQUIRE(op_out    == 1);
    REQUIRE(price_out == Catch::Approx(149.50));
    REQUIRE(size_out  == Catch::Approx(200.0));
    REQUIRE(exch_out  == "NYSE");
    REQUIRE(smart_out == false);
}

// ── MsgError ─────────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgError to onError", "[queue][error]") {
    TestableIBKRClient client;

    int         receivedReqId = -1;
    int         receivedCode  = -1;
    std::string receivedMsg;

    client.onError = [&](int reqId, int code, const std::string& msg) {
        receivedReqId = reqId;
        receivedCode  = code;
        receivedMsg   = msg;
    };

    client.inject(MsgError{1, 200, "No security definition found"});
    client.ProcessMessages();

    REQUIRE(receivedReqId == 1);
    REQUIRE(receivedCode  == 200);
    REQUIRE(receivedMsg   == "No security definition found");
}

// ── MsgScanEnd ────────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgScanEnd to onScanEnd", "[queue][scanner]") {
    TestableIBKRClient client;

    int receivedReqId = -1;
    client.onScanEnd = [&](int reqId) { receivedReqId = reqId; };

    client.inject(MsgScanEnd{1000});
    client.ProcessMessages();

    REQUIRE(receivedReqId == 1000);
}

// ── MsgScanItem ───────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgScanItem to onScanItem", "[queue][scanner]") {
    TestableIBKRClient client;

    int             receivedReqId = -1;
    core::ScanResult receivedResult;

    client.onScanItem = [&](int reqId, const core::ScanResult& r) {
        receivedReqId  = reqId;
        receivedResult = r;
    };

    core::ScanResult item;
    item.symbol    = "AAPL";
    item.changePct = 3.5;

    client.inject(MsgScanItem{1000, item});
    client.ProcessMessages();

    REQUIRE(receivedReqId          == 1000);
    REQUIRE(receivedResult.symbol  == "AAPL");
    REQUIRE(receivedResult.changePct == Catch::Approx(3.5));
}

// ── MsgNextOrderId ────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgNextOrderId to onNextValidId", "[queue][order]") {
    TestableIBKRClient client;

    int receivedId = -1;
    client.onNextValidId = [&](int id) { receivedId = id; };

    client.inject(MsgNextOrderId{42});
    client.ProcessMessages();

    REQUIRE(receivedId == 42);
}

// ── Null-callback safety ──────────────────────────────────────────────────────

TEST_CASE("ProcessMessages does not crash when callbacks are unset", "[queue][safety]") {
    TestableIBKRClient client;
    // No callbacks assigned — all std::function members are empty.

    REQUIRE_NOTHROW([&] {
        client.inject(MsgTickPrice{1, 4, 100.0});
        client.inject(MsgBar{2, {}, false, false});
        client.inject(MsgError{3, 200, "test"});
        client.inject(MsgConnection{true, "Connected"});
        client.inject(MsgScanEnd{1000});
        client.ProcessMessages();
    }());
}

// ── Multiple messages in one call ─────────────────────────────────────────────

TEST_CASE("ProcessMessages drains all queued messages in one call", "[queue]") {
    TestableIBKRClient client;

    int tickCount = 0;
    client.onTickPrice = [&](int, int, double) { ++tickCount; };

    for (int i = 0; i < 10; ++i)
        client.inject(MsgTickPrice{i, 4, 100.0 + i});

    client.ProcessMessages();

    REQUIRE(tickCount == 10);
}

TEST_CASE("ProcessMessages preserves message ordering", "[queue]") {
    TestableIBKRClient client;

    std::vector<double> prices;
    client.onTickPrice = [&](int, int, double price) { prices.push_back(price); };

    client.inject(MsgTickPrice{1, 4, 10.0});
    client.inject(MsgTickPrice{1, 4, 20.0});
    client.inject(MsgTickPrice{1, 4, 30.0});
    client.ProcessMessages();

    REQUIRE(prices.size() == 3);
    REQUIRE(prices[0] == Catch::Approx(10.0));
    REQUIRE(prices[1] == Catch::Approx(20.0));
    REQUIRE(prices[2] == Catch::Approx(30.0));
}

// ── Queue is empty after ProcessMessages ──────────────────────────────────────

TEST_CASE("Queue is empty after ProcessMessages; second call is a no-op", "[queue]") {
    TestableIBKRClient client;

    int callCount = 0;
    client.onTickPrice = [&](int, int, double) { ++callCount; };

    client.inject(MsgTickPrice{1, 4, 50.0});
    client.ProcessMessages();
    REQUIRE(callCount == 1);

    // Second call — queue should be empty; callback should not fire again.
    client.ProcessMessages();
    REQUIRE(callCount == 1);
}

// ── MsgOrderStatus ────────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgOrderStatus to onOrderStatusChanged", "[queue][order]") {
    TestableIBKRClient client;

    int               receivedId     = -1;
    core::OrderStatus receivedStatus = core::OrderStatus::Pending;
    double            receivedFilled = -1.0;
    double            receivedAvg    = -1.0;

    client.onOrderStatusChanged = [&](int id, core::OrderStatus status, double filled, double avg) {
        receivedId     = id;
        receivedStatus = status;
        receivedFilled = filled;
        receivedAvg    = avg;
    };

    client.inject(MsgOrderStatus{99, core::OrderStatus::Filled, 100.0, 152.30});
    client.ProcessMessages();

    REQUIRE(receivedId     == 99);
    REQUIRE(receivedStatus == core::OrderStatus::Filled);
    REQUIRE(receivedFilled == Catch::Approx(100.0));
    REQUIRE(receivedAvg    == Catch::Approx(152.30));
}

// ── MsgHistoricalTick ─────────────────────────────────────────────────────────

TEST_CASE("ProcessMessages dispatches MsgHistoricalTick to onHistoricalTicks", "[queue][replay]") {
    TestableIBKRClient client;

    int  receivedReqId = -1;
    bool receivedDone  = true;
    std::vector<core::HistoricalTick> receivedTicks;

    client.onHistoricalTicks = [&](int reqId,
                                   const std::vector<core::HistoricalTick>& ticks,
                                   bool done) {
        receivedReqId = reqId;
        receivedTicks = ticks;
        receivedDone  = done;
    };

    std::vector<core::HistoricalTick> ticks;
    core::HistoricalTick t;
    t.type  = core::TickType::Trades;
    t.time  = 1713191400;
    t.price = 150.25;
    t.size  = 100.0;
    ticks.push_back(t);

    client.inject(MsgHistoricalTick{42, ticks, false});
    client.ProcessMessages();

    REQUIRE(receivedReqId == 42);
    REQUIRE(receivedTicks.size() == 1);
    REQUIRE(receivedTicks[0].type == core::TickType::Trades);
    REQUIRE(receivedTicks[0].price == Catch::Approx(150.25));
    REQUIRE(receivedTicks[0].size == Catch::Approx(100.0));
    REQUIRE(receivedDone == false);
}

TEST_CASE("MsgHistoricalTick fires with done=true on final batch", "[queue][replay]") {
    TestableIBKRClient client;

    bool receivedDone = false;
    int  callCount    = 0;

    client.onHistoricalTicks = [&](int, const std::vector<core::HistoricalTick>&, bool done) {
        receivedDone = done;
        ++callCount;
    };

    client.inject(MsgHistoricalTick{1, {}, true});
    client.ProcessMessages();

    REQUIRE(callCount == 1);
    REQUIRE(receivedDone == true);
}

TEST_CASE("MsgHistoricalTick null callback does not crash", "[queue][replay]") {
    TestableIBKRClient client;
    // onHistoricalTicks left unset

    std::vector<core::HistoricalTick> ticks;
    client.inject(MsgHistoricalTick{1, ticks, true});
    client.ProcessMessages();
    // No crash = pass
    SUCCEED();
}

// ── Options chain dispatch (Phase 18 Task B) ─────────────────────────────────
// The std::visit in ProcessMessages() is exhaustive, so an unhandled variant
// silently no-ops. One dispatch test per new message type, per testing.md.

TEST_CASE("ProcessMessages dispatches MsgSecDefOptParams", "[queue][options]") {
    TestableIBKRClient client;
    int  calls = 0;
    core::services::MsgSecDefOptParams got{};

    client.onSecDefOptParams = [&](const core::services::MsgSecDefOptParams& m) {
        got = m;
        ++calls;
    };

    core::services::MsgSecDefOptParams msg;
    msg.reqId           = 21000;
    msg.exchange        = "SMART";
    msg.underlyingConId = 265598;
    msg.tradingClass    = "AAPL";
    msg.multiplier      = "100";
    msg.expirations     = {"20260918", "20261016"};
    msg.strikes         = {190.0, 195.0, 200.0};

    client.inject(msg);
    client.ProcessMessages();

    REQUIRE(calls == 1);
    REQUIRE(got.reqId           == 21000);
    REQUIRE(got.exchange        == "SMART");
    REQUIRE(got.underlyingConId == 265598);
    REQUIRE(got.tradingClass    == "AAPL");
    REQUIRE(got.multiplier      == "100");
    REQUIRE(got.expirations.size() == 2);
    REQUIRE(got.expirations[0]  == "20260918");
    REQUIRE(got.strikes.size()  == 3);
    REQUIRE(got.strikes[2]      == 200.0);
}

TEST_CASE("ProcessMessages dispatches MsgSecDefOptParamsEnd", "[queue][options]") {
    TestableIBKRClient client;
    int calls = 0, gotReqId = 0;

    client.onSecDefOptParamsEnd = [&](int reqId) { gotReqId = reqId; ++calls; };

    client.inject(core::services::MsgSecDefOptParamsEnd{21000});
    client.ProcessMessages();

    REQUIRE(calls    == 1);
    REQUIRE(gotReqId == 21000);
}

TEST_CASE("ProcessMessages dispatches MsgTickOptionComputation", "[queue][options]") {
    TestableIBKRClient client;
    int calls = 0;
    core::services::MsgTickOptionComputation got{};

    client.onTickOptionComputation =
        [&](const core::services::MsgTickOptionComputation& m) { got = m; ++calls; };

    core::services::MsgTickOptionComputation msg{};
    msg.reqId      = 22001;
    msg.tickType   = 13;      // model computation
    msg.tickAttrib = 0;
    msg.impliedVol = 0.2841;
    msg.delta      = 0.5312;
    msg.optPrice   = 7.25;
    msg.gamma      = 0.0184;
    msg.vega       = 0.1122;
    msg.theta      = -0.0431;
    msg.undPrice   = 197.40;

    client.inject(msg);
    client.ProcessMessages();

    REQUIRE(calls == 1);
    REQUIRE(got.reqId    == 22001);
    REQUIRE(got.tickType == 13);
    REQUIRE(got.impliedVol == Catch::Approx(0.2841));
    REQUIRE(got.delta      == Catch::Approx(0.5312));
    REQUIRE(got.gamma      == Catch::Approx(0.0184));
    REQUIRE(got.theta      == Catch::Approx(-0.0431));
    REQUIRE(got.undPrice   == Catch::Approx(197.40));
}

TEST_CASE("ProcessMessages dispatches MsgTickGeneric", "[queue][options]") {
    TestableIBKRClient client;
    int calls = 0, gotReqId = 0, gotType = 0;
    double gotValue = 0.0;

    client.onTickGeneric = [&](int reqId, int tickType, double value) {
        gotReqId = reqId; gotType = tickType; gotValue = value; ++calls;
    };

    client.inject(core::services::MsgTickGeneric{22001, 101, 4312.0});  // put OI
    client.ProcessMessages();

    REQUIRE(calls    == 1);
    REQUIRE(gotReqId == 22001);
    REQUIRE(gotType  == 101);
    REQUIRE(gotValue == Catch::Approx(4312.0));
}

TEST_CASE("Options-chain messages with null callbacks do not crash", "[queue][options]") {
    TestableIBKRClient client;
    // All four callbacks left unset.
    client.inject(core::services::MsgSecDefOptParams{});
    client.inject(core::services::MsgSecDefOptParamsEnd{1});
    client.inject(core::services::MsgTickOptionComputation{});
    client.inject(core::services::MsgTickGeneric{1, 101, 0.0});
    client.ProcessMessages();
    SUCCEED();
}

TEST_CASE("ProcessMessages dispatches MsgWhatIf", "[queue][whatif]") {
    TestableIBKRClient client;
    int calls = 0;
    core::WhatIfResult got;
    client.onWhatIf = [&](const core::WhatIfResult& w) { got = w; ++calls; };

    core::WhatIfResult w;
    w.orderId = 42; w.initChange = 1250.5; w.warning = "margin";
    client.inject(core::services::MsgWhatIf{w});
    client.inject(core::services::MsgWhatIf{w});   // null-safe path below
    client.onWhatIf = nullptr;
    client.ProcessMessages();
    REQUIRE(calls == 0);   // callback cleared before draining

    client.onWhatIf = [&](const core::WhatIfResult& r) { got = r; ++calls; };
    client.inject(core::services::MsgWhatIf{w});
    client.ProcessMessages();
    REQUIRE(calls == 1);
    REQUIRE(got.orderId == 42);
    REQUIRE(got.initChange == Catch::Approx(1250.5));
    REQUIRE(got.warning == "margin");
}

// ── Orders placed outside this client ────────────────────────────────────────

TEST_CASE("Orders placed in TWS arrive as separate read-only orders", "[queue][external]") {
    TestableIBKRClient client;
    EWrapper& ib = client;   // the callbacks IB invokes

    std::vector<core::Order> opened;
    std::vector<int> statusIds;
    client.onOpenOrder = [&](const core::Order& o) { opened.push_back(o); };
    client.onOrderStatusChanged = [&](int id, core::OrderStatus, double, double) {
        statusIds.push_back(id);
    };

    Contract c;
    c.symbol = "SPCX"; c.secType = "OPT";
    OrderState st;
    st.status = "Submitted";
    auto order = [](long long permId) {
        ::Order o;
        o.action = "SELL"; o.orderType = "LMT"; o.tif = "GTC";
        o.totalQuantity = DecimalFunctions::doubleToDecimal(1);
        o.permId = permId;
        return o;
    };

    // A TWS bracket: three orders, all with order id 0.
    ib.openOrder(0, c, order(111), st);
    ib.openOrder(0, c, order(222), st);
    ib.openOrder(0, c, order(333), st);
    ib.openOrder(0, c, order(111), st);                 // re-read: same order again
    ib.openOrder(37300, c, order(444), st);             // this client's own order
    ib.orderStatus(0, "Submitted", DecimalFunctions::doubleToDecimal(0),
                   DecimalFunctions::doubleToDecimal(1), 0.0, 222, 0, 0.0, 0, "", 0.0);
    client.ProcessMessages();

    REQUIRE(opened.size() == 5);
    REQUIRE(opened[0].orderId < 0);
    REQUIRE(opened[1].orderId < 0);
    REQUIRE(opened[2].orderId < 0);
    REQUIRE(opened[0].orderId != opened[1].orderId);
    REQUIRE(opened[1].orderId != opened[2].orderId);
    REQUIRE(opened[0].orderId != opened[2].orderId);
    REQUIRE(opened[0].external);
    REQUIRE(opened[3].orderId == opened[0].orderId);    // stable across re-reads
    REQUIRE(opened[4].orderId == 37300);
    REQUIRE_FALSE(opened[4].external);
    REQUIRE(statusIds.size() == 1);
    REQUIRE(statusIds[0] == opened[1].orderId);         // status follows the permId
}

// ── Account summary ──────────────────────────────────────────────────────────

TEST_CASE("Account summary rows name their account", "[queue][account]") {
    TestableIBKRClient client;
    EWrapper& ib = client;

    // The summary request covers every managed account; the receiver needs the
    // account to keep one account's net liquidation off another's Portfolio.
    std::vector<std::pair<std::string, std::string>> rows;   // account, value
    client.onAccountSummary = [&](const std::string& tag, const std::string& value,
                                  const std::string&, const std::string& account) {
        if (tag == "NetLiquidation") rows.emplace_back(account, value);
    };

    ib.accountSummary(900, "U1111111", "NetLiquidation", "14310.00", "EUR");
    ib.accountSummary(900, "U2222222", "NetLiquidation", "1536.02", "USD");
    client.ProcessMessages();

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].first == "U1111111");
    REQUIRE(rows[0].second == "14310.00");
    REQUIRE(rows[1].first == "U2222222");
}

TEST_CASE("Positions name their account", "[queue][account]") {
    TestableIBKRClient client;
    EWrapper& ib = client;

    // reqPositions returns every managed account's positions in one feed.
    std::vector<core::Position> got;
    int ends = 0;
    client.onPositionData = [&](const core::Position& p, bool done) {
        if (done) ++ends; else got.push_back(p);
    };

    Contract c;
    c.symbol = "GME"; c.secType = "STK"; c.conId = 36285627;
    ib.position("U1111111", c, DecimalFunctions::doubleToDecimal(200), 27.22);
    ib.position("U2222222", c, DecimalFunctions::doubleToDecimal(20), 1.95);
    ib.positionEnd();
    client.ProcessMessages();

    REQUIRE(got.size() == 2);
    REQUIRE(got[0].account == "U1111111");
    REQUIRE(got[0].quantity == Catch::Approx(200.0));
    REQUIRE(got[1].account == "U2222222");
    REQUIRE(ends == 1);
}

TEST_CASE("Fills name their account", "[queue][account]") {
    TestableIBKRClient client;
    EWrapper& ib = client;

    std::vector<core::Fill> fills;
    client.onFillReceived = [&](const core::Fill& f) { fills.push_back(f); };

    Contract c;
    c.symbol = "GME"; c.secType = "STK";
    Execution e;
    e.execId = "0001"; e.acctNumber = "U2222222"; e.side = "BOT";
    e.shares = DecimalFunctions::doubleToDecimal(20); e.price = 25.92; e.orderId = 41;
    ib.execDetails(-1, c, e);
    CommissionAndFeesReport r;
    r.execId = "0001"; r.commissionAndFees = 1.0; r.realizedPNL = 0.0;
    ib.commissionAndFeesReport(r);
    client.ProcessMessages();

    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].account == "U2222222");
    REQUIRE(fills[0].quantity == Catch::Approx(20.0));
}
