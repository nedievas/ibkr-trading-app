#pragma once

#include <string>
#include <vector>

namespace core {

// ---- One leg of a combo (BAG) contract --------------------------------------
// A vertical spread is a BAG whose comboLegs list holds two of these; IB
// references each leg by conId only, so the leg's conId must be resolved
// (reqContractDetails) before the order can be placed.
struct ComboLegSpec {
    long        conId    = 0;      // resolved leg contract id (0 = unresolved)
    int         ratio    = 1;
    std::string action;            // "BUY" / "SELL"
    std::string exchange = "SMART";
};

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
    // Combo legs (secType == "BAG"). Empty for a single instrument. When set,
    // MakeContractFromSpec builds a BAG contract and ignores strike/right.
    std::vector<ComboLegSpec> comboLegs;
    // IB's human-readable combo description, populated on inbound open orders
    // (Contract::comboLegsDescrip). Display-only; not sent when placing.
    std::string comboLegsDescrip;
};

}  // namespace core
