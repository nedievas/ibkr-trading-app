#pragma once

// Black-Scholes option pricing — pure, header-only, no IB/ImGui deps.
// Used by the strategy-analysis graph's theoretical "P/L today" curve (AG-2):
// repricing each option leg at a hypothetical spot and time-to-expiry between
// now and expiration. See .claude/plans/options-chain.md section 12.

#include <algorithm>
#include <cmath>

namespace core::services {

// Assumed risk-free rate. There is no rate feed; a fixed constant is close
// enough for P&L-shape visualisation and for backing an IV out of a premium.
inline constexpr double kAssumedRiskFreeRate = 0.04;

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

// Implied volatility that reprices `price` (per-share premium) under
// BlackScholesPrice, found by bisection (the price is increasing in iv).
// Returns 0 when no vol in (0.0001, 5.0) fits — degenerate input, a premium at
// or below intrinsic (deep ITM / stale mark), or above the 500%-vol price — so
// callers fall back to intrinsic-only curves rather than a made-up vol.
inline double ImpliedVolFromPrice(char right, double price, double S, double K,
                                  double t, double r) {
    if (price <= 0.0 || S <= 0.0 || K <= 0.0 || t <= 0.0) return 0.0;
    double lo = 1e-4, hi = 5.0;
    if (price <= BlackScholesPrice(right, S, K, t, r, lo) ||
        price >= BlackScholesPrice(right, S, K, t, r, hi)) return 0.0;
    for (int i = 0; i < 100 && hi - lo > 1e-7; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (BlackScholesPrice(right, S, K, t, r, mid) < price) lo = mid;
        else                                                    hi = mid;
    }
    return 0.5 * (lo + hi);
}

// Per-share Black-Scholes delta and theta (theta per calendar day), the same
// units as IB's model greeks. At t <= 0 / iv <= 0: intrinsic delta, no theta.
struct BsGreeks { double delta = 0.0; double theta = 0.0; };
inline BsGreeks BlackScholesGreeks(char right, double S, double K, double t,
                                   double r, double iv) {
    const bool call = (right == 'C' || right == 'c');
    BsGreeks g;
    if (t <= 0.0 || iv <= 0.0 || S <= 0.0 || K <= 0.0) {
        g.delta = call ? (S > K ? 1.0 : 0.0) : (S < K ? -1.0 : 0.0);
        return g;
    }
    const double sqrtT = std::sqrt(t);
    const double d1 = (std::log(S / K) + (r + 0.5 * iv * iv) * t) / (iv * sqrtT);
    const double d2 = d1 - iv * sqrtT;
    const double pdf = std::exp(-0.5 * d1 * d1) / std::sqrt(2.0 * 3.14159265358979323846);
    const double disc = std::exp(-r * t);
    const double decay = -S * pdf * iv / (2.0 * sqrtT);
    g.delta = call ? NormCdf(d1) : NormCdf(d1) - 1.0;
    g.theta = (call ? decay - r * K * disc * NormCdf(d2)
                    : decay + r * K * disc * NormCdf(-d2)) / 365.0;
    return g;
}

}  // namespace core::services
