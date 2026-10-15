#pragma once
// Most-recent-first symbol lists (the "v" recent-symbols menu next to a symbol
// box). Pure logic, no ImGui / IB dependency, so tests-core covers it.
//
// Each entry keeps the symbol and, when the window knows it, its security type
// ("IND" for an index), so a pick from the list can be loaded as the right
// kind of contract. Saved one per line: "SYMBOL" or "SYMBOL|TYPE".

#include <cstddef>
#include <deque>
#include <string>
#include <algorithm>

namespace core::services {

inline constexpr std::size_t kMaxRecentSymbols = 10;

struct RecentSymbol {
    std::string symbol;
    std::string secType;   // "" = unknown
    bool operator==(const RecentSymbol& o) const {
        return symbol == o.symbol && secType == o.secType;
    }
};

namespace detail {
inline std::string TrimRecent(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
} // namespace detail

// Move `symbol` to the front, dropping an earlier copy and anything past
// `maxItems`. Surrounding whitespace is trimmed; an empty symbol is ignored.
// An empty `secType` keeps the type the earlier copy had.
inline void PushRecentSymbol(std::deque<RecentSymbol>& list, const std::string& symbol,
                             const std::string& secType = {},
                             std::size_t maxItems = kMaxRecentSymbols) {
    RecentSymbol e{ detail::TrimRecent(symbol), detail::TrimRecent(secType) };
    if (e.symbol.empty()) return;
    auto it = std::find_if(list.begin(), list.end(),
                           [&](const RecentSymbol& r) { return r.symbol == e.symbol; });
    if (it != list.end()) {
        if (e.secType.empty()) e.secType = it->secType;
        list.erase(it);
    }
    list.push_front(std::move(e));
    while (list.size() > maxItems) list.pop_back();
}

// One entry per line, most recent first. Blank lines are skipped and Windows
// line endings stripped; repeats keep their first (most recent) position.
inline std::deque<RecentSymbol> ParseRecentSymbols(const std::string& text,
                                                   std::size_t maxItems = kMaxRecentSymbols) {
    std::deque<RecentSymbol> out;
    std::size_t pos = 0;
    while (pos <= text.size() && out.size() < maxItems) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const auto bar = line.find('|');
        RecentSymbol e{ detail::TrimRecent(line.substr(0, bar)),
                        bar == std::string::npos ? std::string()
                                                 : detail::TrimRecent(line.substr(bar + 1)) };
        if (e.symbol.empty()) continue;
        if (std::none_of(out.begin(), out.end(),
                         [&](const RecentSymbol& r) { return r.symbol == e.symbol; }))
            out.push_back(std::move(e));
    }
    return out;
}

inline std::string FormatRecentSymbols(const std::deque<RecentSymbol>& list) {
    std::string out;
    for (const auto& e : list) {
        out += e.symbol;
        if (!e.secType.empty()) { out += '|'; out += e.secType; }
        out += '\n';
    }
    return out;
}

} // namespace core::services
