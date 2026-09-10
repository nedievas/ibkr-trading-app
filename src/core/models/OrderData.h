#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <ctime>

#include "ContractSpec.h"   // core::ContractSpec

namespace core {

// ---- Enumerations -----------------------------------------------------------

enum class OrderSide   { Buy, Sell };
enum class OrderType {
    Market, Limit, Stop, StopLimit,          // basic
    Trail, TrailLimit,                        // trailing stop
    MOC, LOC, MTL,                            // market/limit variants
    MIT, LIT,                                 // if-touched
    Midprice, Relative                        // smart / pegged-to-primary
};
enum class TimeInForce { Day, GTC, IOC, FOK, Overnight, OPG };
enum class OrderStatus { Pending, Working, PartialFill, Filled,
                         Cancelled, Rejected, PendingCancel };

// ---- Market depth -----------------------------------------------------------

struct DepthLevel {
    double      price    = 0.0;
    double      size     = 0.0;
    int         numOrders = 1;
    float       flashAge = 0.0f;  // seconds since last price change (for highlight)
    std::string exchange;         // exchange name (empty for L1 SMART depth)
};

// ---- Tick (time & sales) ----------------------------------------------------

struct Tick {
    double      price        = 0.0;
    double      size         = 0.0;
    std::time_t timestamp    = 0;
    bool        isUptick     = true;   // vs downtick; false = downtick, neutral when equal
    bool        isNeutral    = false;  // price == previous price
    std::string exchange;
    std::string specialConds;
};

// ---- Order ------------------------------------------------------------------

struct Order {
    int         orderId     = 0;
    std::string symbol;
    OrderSide   side        = OrderSide::Buy;
    OrderType   type        = OrderType::Market;
    TimeInForce tif         = TimeInForce::Day;
    double      quantity       = 0.0;
    double      limitPrice     = 0.0;
    double      stopPrice      = 0.0;
    double      auxPrice       = 0.0;   // trigger for MIT/LIT; offset for REL; trail $ for TRAIL*
    double      trailingPercent= 0.0;   // trailing % for TRAIL / TRAIL LIMIT
    double      trailStopPrice = 0.0;   // initial stop cap for TRAIL / TRAIL LIMIT (optional)
    double      lmtPriceOffset = 0.0;   // limit offset from trail stop for TRAIL LIMIT
    bool        outsideRth     = false; // allow pre/after-hours fills
    std::string account;               // IB account code (required for multi-account / FA)
    std::string exchange;              // routing exchange; empty / "SMART" = IB smart routing
    // Full contract for non-stock orders (options, futures, indexes). An empty
    // secType means "plain US stock named by `symbol`" and takes the legacy
    // MakeStockContract path in PlaceOrder — so every pre-existing order site
    // keeps its exact current behaviour.
    ContractSpec spec;
    int         parentId       = 0;    // 0 = no parent; non-zero = bracket child
    std::string ocaGroup;              // OCA group id; siblings sharing this id are linked
    int         ocaType        = 0;    // 0 = none, 1 = cancel-with-block, 2/3 = reduce variants
    bool        transmit       = true; // IB transmit flag; false = stage, true = activate
    double      filledQty      = 0.0;
    double      avgFillPrice = 0.0;
    double      commission   = 0.0;  // actual commission from fills (or estimate from OrderState)
    OrderStatus status       = OrderStatus::Pending;
    std::string rejectReason;        // IB error message when status == Rejected
    // Informational IB warning when the order is accepted but held — typically
    // pre/post-market submissions held until RTH open, or "Outside RTH attribute
    // ignored" notices. Distinct from rejectReason: order is still live.
    std::string holdReason;
    std::time_t submittedAt  = 0;
    std::time_t updatedAt    = 0;
};

// ---- Fill (execution report) ------------------------------------------------

struct Fill {
    int         orderId     = 0;
    std::string execId;       // IB execution ID — correlates execDetails ↔ commissionReport
    std::string symbol;
    OrderSide   side        = OrderSide::Buy;
    double      quantity    = 0.0;
    double      price       = 0.0;
    double      commission  = 0.0;
    double      realizedPnL = 0.0;  // populated by commissionReport callback
    std::time_t timestamp   = 0;

    // Option descriptor — empty secType means a stock/other fill (unchanged).
    std::string secType;      // "OPT" for an option leg
    double      strike      = 0.0;
    std::string right;        // "C" / "P"
    std::string expiry;       // YYYYMMDD
};

// "TSLA 16OCT26 310P" for an option leg; the bare symbol for anything else.
// Shared by Position and Fill so the portfolio, orders, and history views all
// label an option the same way instead of showing the bare underlying symbol.
inline std::string OptionDisplayLabel(const std::string& symbol,
                                      const std::string& expiry,   // YYYYMMDD
                                      double strike,
                                      const std::string& right) {  // "C" / "P"
    if (right.empty() || expiry.size() < 8 || strike <= 0.0) return symbol;
    static const char* kMon[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC"};
    const int mo = (expiry[4] - '0') * 10 + (expiry[5] - '0');
    const char* mon = (mo >= 1 && mo <= 12) ? kMon[mo - 1] : "???";
    char strk[16];
    if (strike == std::floor(strike)) std::snprintf(strk, sizeof(strk), "%.0f", strike);
    else                              std::snprintf(strk, sizeof(strk), "%.1f", strike);
    return symbol + " " + expiry.substr(6, 2) + mon + expiry.substr(2, 2)
         + " " + strk + right.substr(0, 1);
}

// Friendly option label parsed from an OSI local symbol, e.g.
// "TSLA  261016P00320000" -> "TSLA 16OCT26 320P". Fallback for when a position
// feed carries the OSI local symbol but not the discrete strike/right/expiry
// fields (IB does not populate all of them on every position callback). Returns
// "" when the string is not a parseable OSI symbol. Parsed from the right so the
// root's space padding is irrelevant.
inline std::string OptionLabelFromLocalSymbol(const std::string& localSymbol) {
    std::string s;
    for (char c : localSymbol) if (c != ' ') s += c;   // drop OSI root padding
    // Need root(>=1) + YYMMDD(6) + right(1) + strike(8).
    if (s.size() < 1 + 6 + 1 + 8) return {};
    const std::size_t strikeAt = s.size() - 8;
    const std::size_t rightAt  = strikeAt - 1;
    const std::size_t ymdAt    = rightAt - 6;
    const char right = s[rightAt];
    if (right != 'C' && right != 'P') return {};
    for (std::size_t i = ymdAt; i < s.size(); ++i)
        if (i != rightAt && (s[i] < '0' || s[i] > '9')) return {};
    const std::string root   = s.substr(0, ymdAt);
    const std::string expiry = "20" + s.substr(ymdAt, 6);   // YYMMDD -> YYYYMMDD
    const double strike = std::stol(s.substr(strikeAt)) / 1000.0;
    if (root.empty()) return {};
    return OptionDisplayLabel(root, expiry, strike, std::string(1, right));
}

// ---- String helpers ---------------------------------------------------------

inline const char* OrderSideStr(OrderSide s) {
    return s == OrderSide::Buy ? "BUY" : "SELL";
}
inline const char* OrderTypeStr(OrderType t) {
    switch (t) {
        case OrderType::Market:    return "MKT";
        case OrderType::Limit:     return "LMT";
        case OrderType::Stop:      return "STP";
        case OrderType::StopLimit: return "STP LMT";
        case OrderType::Trail:     return "TRAIL";
        case OrderType::TrailLimit:return "TRAIL LIMIT";
        case OrderType::MOC:       return "MOC";
        case OrderType::LOC:       return "LOC";
        case OrderType::MTL:       return "MTL";
        case OrderType::MIT:       return "MIT";
        case OrderType::LIT:       return "LIT";
        case OrderType::Midprice:  return "MIDPRICE";
        case OrderType::Relative:  return "REL";
        default:                   return "?";
    }
}
inline const char* TIFStr(TimeInForce t) {
    switch (t) {
        case TimeInForce::Day: return "DAY";
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK:       return "FOK";
        case TimeInForce::Overnight: return "OVERNIGHT";
        case TimeInForce::OPG:       return "OPG";
        default:                     return "?";
    }
}
inline const char* OrderStatusStr(OrderStatus s) {
    switch (s) {
        case OrderStatus::Pending:       return "PENDING";
        case OrderStatus::Working:       return "WORKING";
        case OrderStatus::PartialFill:   return "PARTIAL";
        case OrderStatus::Filled:        return "FILLED";
        case OrderStatus::Cancelled:     return "CANCELLED";
        case OrderStatus::Rejected:      return "REJECTED";
        case OrderStatus::PendingCancel: return "CANCELLING";
        default:                         return "?";
    }
}

// ============================================================================
// PendingBracketStop — bracket stop-loss / take-profit parameters staged at
// entry-fill time.  Moved here from ChartWindow.h so it can be unit-tested.
// ============================================================================
struct PendingBracketStop {
    std::string  symbol;
    OrderSide    stopSide    = OrderSide::Buy;
    double       qty         = 0.0;
    double       stopPrice   = 0.0;
    double       tpPrice     = 0.0;   // take-profit limit; 0 = no TP leg
    bool         outsideRth  = false; // allow fill outside RTH once stop triggers
    bool         useStopLmt  = false; // STP LMT instead of STP (safer outside RTH)
};

}  // namespace core
