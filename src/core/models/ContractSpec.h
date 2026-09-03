#pragma once

#include <string>

namespace core {

// ---- A fully-qualified IB contract ------------------------------------------
//
// Shared by every feature that needs to name a contract precisely rather than
// assume a plain US stock: scanner results, market-data / historical requests,
// and (since the options work) order submission via core::Order::spec.
//
// The IB scanner returns a complete ContractDetails per row (conId, secType,
// exchange, expiry, ...). Capturing it lets us re-subscribe market data and
// historical bars with the *exact* contract instead of guessing "STK" from the
// bare symbol — which is what broke Indexes (IND) and Futures (FUT) quotes.
//
// Lives in its own header so `OrderData.h` does not have to depend on
// `ScannerData.h` just to name a contract.

struct ContractSpec {
    long        conId   = 0;
    std::string symbol;
    std::string secType;          // "STK" (stocks + ETFs), "IND", "FUT", "OPT", ...
    std::string exchange;         // routing exchange (native for IND/FUT)
    std::string primaryExchange;
    std::string currency = "USD";
    std::string lastTradeDateOrContractMonth;  // futures expiry (YYYYMM / YYYYMMDD)
    std::string multiplier;                    // futures / options multiplier
    // Options-only fields (secType == "OPT"). Defaulted so every existing
    // brace-init call site is unaffected.
    double      strike  = 0.0;    // strike price
    std::string right;            // "C" / "P"
    std::string tradingClass;     // e.g. "AAPL" vs "AAPL1" weeklies
};

}  // namespace core
