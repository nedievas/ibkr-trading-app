#pragma once

// Black-Scholes option pricing — pure, header-only, no IB/ImGui deps.
// Used by the strategy-analysis graph's theoretical "P/L today" curve (AG-2):
// repricing each option leg at a hypothetical spot and time-to-expiry between
// now and expiration. See .claude/plans/options-chain.md section 12.

#include <algorithm>
#include <cmath>

namespace core::services {

// Standard normal CDF.
inline double NormCdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

// Black-Scholes price of a European option (no dividend, q = 0).
//   right : 'C'/'c' call, otherwise put
//   S     : underlying spot
//   K     : strike
//   t     : years to expiry (t <= 0 → intrinsic value)
//   r     : continuously-compounded risk-free rate (e.g. 0.04)
//   iv    : annualised implied volatility (e.g. 0.25); iv <= 0 → intrinsic
// At t == 0 the discount factor is 1 and the result equals the intrinsic
// value, so a theoretical curve evaluated at expiry matches the payoff curve.
inline double BlackScholesPrice(char right, double S, double K, double t,
                                double r, double iv) {
    const bool call = (right == 'C' || right == 'c');
    if (t <= 0.0 || iv <= 0.0 || S <= 0.0 || K <= 0.0)
        return call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);

    const double sqrtT = std::sqrt(t);
    const double d1 = (std::log(S / K) + (r + 0.5 * iv * iv) * t) / (iv * sqrtT);
    const double d2 = d1 - iv * sqrtT;
    const double disc = std::exp(-r * t);
    return call ? S * NormCdf(d1) - K * disc * NormCdf(d2)
                : K * disc * NormCdf(-d2) - S * NormCdf(-d1);
}

}  // namespace core::services
