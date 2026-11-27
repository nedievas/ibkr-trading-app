#pragma once

#include <string>
#include <ctime>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

#include "core/models/OrderData.h"

namespace core::services {

// Portable timegm — MSVC has _mkgmtime, POSIX has timegm.
inline std::time_t Timegm(struct tm* t) {
    t->tm_isdst = 0;
#ifdef _WIN32
    return _mkgmtime(t);
#else
    return timegm(t);
#endif
}

// Maps an IB order-status string to our OrderStatus enum.
inline ::core::OrderStatus ParseStatus(const std::string& s) {
    if (s == "Filled")                                                    return ::core::OrderStatus::Filled;
    if (s == "Cancelled" || s == "ApiCancelled" || s == "Inactive")      return ::core::OrderStatus::Cancelled;
    if (s == "Submitted"  || s == "PreSubmitted" || s == "ApiPending")   return ::core::OrderStatus::Working;
    if (s == "PartiallyFilled")                                           return ::core::OrderStatus::PartialFill;
    if (s == "Pending" || s == "PendingSubmit")                           return ::core::OrderStatus::Pending;
    if (s == "PendingCancel")                                             return ::core::OrderStatus::PendingCancel;
    if (s.empty())                                                        return ::core::OrderStatus::Pending;
    return ::core::OrderStatus::Rejected;
}

// Parses an IB timestamp string to a UNIX time_t.
// Accepts three formats:
//   "YYYYMMDD"             — daily/weekly/monthly bars; mapped to noon UTC so
//                            gmtime() always returns the correct calendar date.
//   "<unix_timestamp>"     — intraday bars with formatDate=2 (all digits, > 8 chars)
//   "YYYYMMDD HH:MM:SS"   — intraday bars with formatDate=1
// A margin / commission amount from IB's OrderState. IB sends margins as
// strings and marks "not provided" with an empty string or DBL_MAX
// (1.7976931348623157E308); both come back as NaN.
inline double ParseMarginAmount(const std::string& s) {
    if (s.empty()) return std::numeric_limits<double>::quiet_NaN();
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v) || std::fabs(v) > 1e300)
        return std::numeric_limits<double>::quiet_NaN();
    return v;
}
inline double ParseMarginAmount(double v) {
    if (!std::isfinite(v) || std::fabs(v) > 1e300) return std::numeric_limits<double>::quiet_NaN();
    return v;
}

inline std::time_t ParseIBTime(const std::string& ts) {
    if (ts.empty()) return 0;

    bool allDigits = true;
    for (char c : ts)
        if (!isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }

    if (allDigits) {
        if (ts.size() == 8) {
            // "YYYYMMDD" — daily/weekly/monthly bar. IB ignores formatDate=2 for
            // these and returns a date string, so we parse it ourselves.
            // We set the time to noon UTC so that any timezone ±12h offset still
            // maps back to the correct calendar date.
            struct tm t = {};
            std::istringstream(ts) >> std::get_time(&t, "%Y%m%d");
            t.tm_hour = 12; t.tm_min = t.tm_sec = 0;
            return Timegm(&t);
        }
        // Unix timestamp string (intraday bars with formatDate=2).
        return static_cast<std::time_t>(std::stoll(ts));
    }

    // "YYYYMMDD HH:MM:SS [TZ]" — intraday bars with formatDate=1.
    struct tm t = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&t, "%Y%m%d %H:%M:%S");
    if (ss.fail()) {
        ss.clear();
        std::istringstream(ts) >> std::get_time(&t, "%Y%m%d");
    }
    t.tm_isdst = -1;
    return mktime(&t);
}

// ── Futures symbol detection ────────────────────────────────────────────────
// IB's reqMatchingSymbols only returns stocks, so the symbol-search layer
// needs a separate path for futures.  IsFuturesSymbol recognises common
// index / commodity / currency futures symbols, the "/" prefix convention
// (e.g. "/ES", "/NQ"), and contract-month-qualified forms
// ("NQ 202612", "/NQ:202612").

// Known futures roots and the exchange IB lists each on. A future built on the
// wrong exchange gets IB error 200 (YM / treasuries / grains are CBOT, gold and
// silver COMEX, crude, gas, platinum and palladium NYMEX).
inline const std::unordered_map<std::string, std::string>& FuturesExchanges() {
    static const std::unordered_map<std::string, std::string> k = {
        // equity index (and micros)
        {"ES","CME"}, {"NQ","CME"}, {"RTY","CME"}, {"MES","CME"}, {"MNQ","CME"},
        {"M2K","CME"}, {"YM","CBOT"}, {"MYM","CBOT"},
        // metals
        {"GC","COMEX"}, {"SI","COMEX"}, {"HG","COMEX"}, {"MGC","COMEX"},
        {"SIL","COMEX"}, {"PL","NYMEX"}, {"PA","NYMEX"},
        // energy
        {"CL","NYMEX"}, {"NG","NYMEX"}, {"RB","NYMEX"}, {"HO","NYMEX"},
        {"MCL","NYMEX"},
        // treasuries / rates
        {"ZN","CBOT"}, {"ZB","CBOT"}, {"ZF","CBOT"}, {"ZT","CBOT"}, {"ZQ","CBOT"},
        // grains
        {"ZC","CBOT"}, {"ZW","CBOT"}, {"ZS","CBOT"}, {"ZM","CBOT"}, {"ZL","CBOT"},
        // livestock
        {"HE","CME"}, {"LE","CME"},
        // FX
        {"6E","CME"}, {"6J","CME"}, {"6B","CME"}, {"6A","CME"}, {"6C","CME"},
        {"6S","CME"}, {"6M","CME"}, {"6N","CME"},
    };
    return k;
}

// A root the app shows differently from IB: micro silver is "SIL" to traders
// (local symbols SILZ6, SILH7) but IB lists it under symbol SI, trading class
// SIL. Asking IB for symbol SIL gets error 200.
struct FuturesIbName { std::string symbol, tradingClass; };
inline FuturesIbName FuturesIbNameFor(const std::string& root) {
    if (root == "SIL") return {"SI", "SIL"};
    return {root, ""};
}

inline bool IsFuturesBaseSymbol(const std::string& base) {
    return FuturesExchanges().count(base) > 0;
}

// The exchange for a futures root; CME for a root this table doesn't know.
inline std::string FuturesExchange(const std::string& base) {
    auto it = FuturesExchanges().find(base);
    return it != FuturesExchanges().end() ? it->second : std::string("CME");
}

// Futures month code letter -> month (1..12), 0 if not a month code.
inline int FuturesMonthFromCode(char c) {
    static const char kCodes[] = "FGHJKMNQUVXZ";
    for (int i = 0; i < 12; ++i)
        if (kCodes[i] == c) return i + 1;
    return 0;
}

// A local symbol such as "SILH7", "ESZ6", "6EZ26" or "SIL H7": a known root,
// a month code and a one- or two-digit year. A one-digit year is the first
// year from (nowYear, nowMonth) on that ends in it, so "H6" read in November
// 2026 is March 2036 (IB reuses the digits every decade). Gives the root and
// "YYYYMM"; false when the text isn't one.
inline bool ParseFuturesLocalSymbol(const std::string& text, int nowYear, int nowMonth,
                                    std::string& root, std::string& yyyymm) {
    std::string s;
    for (char c : text)
        if (c != ' ' && c != '/') s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::size_t d = s.size();
    while (d > 0 && std::isdigit(static_cast<unsigned char>(s[d - 1]))) --d;
    const std::size_t digits = s.size() - d;
    if (digits < 1 || digits > 2 || d < 2) return false;
    const int month = FuturesMonthFromCode(s[d - 1]);
    const std::string base = s.substr(0, d - 1);
    if (month == 0 || !IsFuturesBaseSymbol(base)) return false;
    const int yy = std::atoi(s.c_str() + d);
    int year;
    if (digits == 2) {
        year = 2000 + yy;
    } else {
        year = nowYear - nowYear % 10 + yy;
        if (year < nowYear || (year == nowYear && month < nowMonth)) year += 10;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d%02d", year, month);
    root = base;
    yyyymm = buf;
    return true;
}

// Splits a futures symbol string into (base, contractMonth).
//   "/ES"          → base="ES",  contractMonth=""        (default month)
//   "/NQ 202612"   → base="NQ",  contractMonth="202612"
//   "NQ:202612"    → base="NQ",  contractMonth="202612"
//   "SILH7", "/SIL H7" → base="SIL", contractMonth="202703" (local symbol)
//   "GC"           → base="GC",  contractMonth=""
inline void ParseFuturesSymbol(const std::string& sym,
                               std::string& base, std::string& contractMonth) {
    // Strip leading "/" if present.
    std::string s = (!sym.empty() && sym[0] == '/') ? sym.substr(1) : sym;
    // Split on trailing " YYYYMM" or ":YYYYMM".
    auto pos = s.find_last_of(" :");
    if (pos != std::string::npos && pos > 0 && pos + 7 == s.size()) {
        std::string suffix = s.substr(pos + 1);
        bool allDigits = suffix.size() == 6;
        for (char c : suffix)
            if (!std::isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }
        if (allDigits) {
            base          = s.substr(0, pos);
            contractMonth = suffix;
            return;
        }
    }
    if (!IsFuturesBaseSymbol(s)) {
        const std::time_t now = std::time(nullptr);
        if (const std::tm* t = std::gmtime(&now);
            t && ParseFuturesLocalSymbol(s, t->tm_year + 1900, t->tm_mon + 1, base, contractMonth))
            return;
    }
    base = s;
    contractMonth.clear();
}

// The app's own spelling of a future: "/ROOT" or "/ROOT YYYYMM".
inline std::string FormatFuturesSymbol(const std::string& base, const std::string& month) {
    return month.empty() ? "/" + base : "/" + base + " " + month;
}

inline bool IsFuturesSymbol(const std::string& sym) {
    if (sym.empty()) return false;
    // "/" prefix signals futures intent — require at least 2 chars so
    // a lone "/" doesn't match before the user has typed a symbol letter.
    if (sym[0] == '/' && sym.size() >= 2) return true;
    // A month makes it one: "NQ 202612" or a local symbol like "SILH7". A bare
    // root isn't - it is also a stock ticker (SIL is an ETF, ES Eversource).
    std::string base, contractMonth;
    ParseFuturesSymbol(sym, base, contractMonth);
    return !contractMonth.empty() && IsFuturesBaseSymbol(base);
}

// ── Cash-settled index symbols ───────────────────────────────────────────────
// Well-known indexes and their listing exchange. A bare symbol resolves as a
// stock (STK/SMART), which IB answers with error 200 for an index, so every
// bare-symbol request (chart, order book, watchlist, …) builds an IND contract
// on this exchange instead.
inline const std::unordered_map<std::string, std::string>& KnownIndexExchanges() {
    static const std::unordered_map<std::string, std::string> k = {
        {"SPX","CBOE"}, {"SPXW","CBOE"}, {"XSP","CBOE"}, {"VIX","CBOE"},
        {"VXN","CBOE"}, {"OEX","CBOE"},  {"XEO","CBOE"}, {"DJX","CBOE"},
        {"RUT","CBOE"}, {"NDX","NASDAQ"}, {"NQX","NASDAQ"},
    };
    return k;
}
inline bool IsKnownIndexSymbol(const std::string& sym) {
    return KnownIndexExchanges().count(sym) > 0;
}

// What to do once IB says the connection is ready. IB's account list can come
// after that signal (TWS does this, Gateway sends it first), so with no account
// known yet the caller has to wait for it.
enum class ConnectStep { WaitForAccounts, PickAccount, Finish };
inline ConnectStep NextConnectStep(bool accountSelected, std::size_t managedAccounts) {
    if (accountSelected)      return ConnectStep::Finish;
    if (managedAccounts == 0) return ConnectStep::WaitForAccounts;
    return managedAccounts > 1 ? ConnectStep::PickAccount : ConnectStep::Finish;
}

// updateNewsBulletin's msgType: 1 regular, 2 exchange no longer available, 3
// exchange available again.
inline const char* NewsBulletinTitle(int msgType) {
    switch (msgType) {
        case 2:  return "Exchange unavailable";
        case 3:  return "Exchange available";
        default: return "IB bulletin";
    }
}

// Contract month ("YYYYMM") of a standard quarterly future (Mar / Jun / Sep /
// Dec) on the given date: ahead = 0 is the front month, 1 the one after it.
// A month counts as expired from its 22nd (the third Friday is at most the
// 21st), so it never returns a contract IB has already expired.
inline std::string QuarterlyContractMonth(int year, int month, int day, int ahead = 0) {
    int qm = ((month + 2) / 3) * 3;               // this quarter's contract month
    if (month == qm && day >= 22) qm += 3;        // already expired
    qm += 3 * (ahead < 0 ? 0 : ahead);
    year += (qm - 1) / 12;
    qm = (qm - 1) % 12 + 1;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d%02d", year, qm);
    return buf;
}

// QuarterlyContractMonth for today (UTC).
inline std::string FuturesFrontMonth(int ahead = 0) {
    auto now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    return QuarterlyContractMonth(utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, ahead);
}

// What an IB connectivity error code (1100-series, 1300) requires of the app.
// 1100: IB-server link lost, API socket stays up. 1102: link restored, data
// maintained. 1101: restored but data lost (re-request subscriptions).
// 1300: socket port changed (reconnect).
enum class ConnectivityAction { None, UpstreamLost, UpstreamRestored, Resubscribe, Reconnect };

inline ConnectivityAction ConnectivityActionFor(int code) {
    switch (code) {
        case 1100: return ConnectivityAction::UpstreamLost;
        case 1102: return ConnectivityAction::UpstreamRestored;
        case 1101: return ConnectivityAction::Resubscribe;
        case 1300: return ConnectivityAction::Reconnect;
        default:   return ConnectivityAction::None;
    }
}

// Next id from a rotating pool [first, last], skipping any id `inUse` says a
// live subscription still holds. `next` is the pool's cursor. After the pool
// wraps, an id handed out again while its old stream is alive would mix two
// symbols' data on one id. If every id is in use the cursor's id is returned.
template <typename InUse>
inline int NextRotatingId(int& next, int first, int last, InUse&& inUse) {
    if (next < first || next > last) next = first;
    const int count = last - first + 1;
    for (int i = 0; i < count; ++i) {
        const int id = next;
        next = (id >= last) ? first : id + 1;
        if (!inUse(id)) return id;
    }
    const int id = next;
    next = (id >= last) ? first : id + 1;
    return id;
}

} // namespace core::services
