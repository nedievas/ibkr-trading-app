#pragma once

#include <string>
#include <vector>
#include <ctime>

namespace core {

// ---- Option chain data models -----------------------------------------------
// POD-only per cpp-style.md: no IB API, no ImGui, no business logic. The logic
// that operates on these lives in core/services/OptionChain.h.

// Identifies one option contract. Right is 'C' or 'P'.
struct OptionContractKey {
    std::string symbol;
    std::string expiry;        // "YYYYMMDD"
    double      strike = 0.0;
    char        right  = 'C';
};

// A live quote for one option contract.
struct OptionQuote {
    OptionContractKey key;
    int    conId = 0;
    double bid = 0, ask = 0, last = 0, volume = 0, openInterest = 0;
    double impliedVol = 0, delta = 0, gamma = 0, theta = 0, vega = 0, undPrice = 0;
    int    reqId = 0;
    bool   subscribed = false;
    std::time_t lastTick = 0;   // 0 = never ticked; drives the staleness indicator
};

// The set of expirations and strikes IB lists for an underlying.
//
// Note there is deliberately no per-expiry strike list. IB's
// securityDefinitionOptionalParameter delivers a flat expirations set and a
// flat strikes set per exchange — not the pairing between them. The true
// tradable set is a subset of the cross product, and the only way to know
// which combinations exist is to ask (reqContractDetails) or watch a
// subscription come back empty. Modelling a per-expiry list here would imply
// a precision the upstream data does not have.
struct OptionChainMeta {
    std::string symbol;
    std::string tradingClass;
    std::string multiplier;
    int         underlyingConId = 0;
    std::vector<std::string> expirations;   // ascending, deduped
    std::vector<double>      strikes;       // ascending, deduped
};

// A two-leg vertical spread staged from the chain. Both legs share expiry and
// right and differ only in strike.
struct VerticalSpread {
    OptionContractKey longLeg, shortLeg;
    int    longConId = 0, shortConId = 0;
    double netPrice  = 0.0;   // >0 = net debit, <0 = net credit
};

}  // namespace core
