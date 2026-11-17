#pragma once
// IB order messages (rejects, warnings) can carry HTML and web links. Pure logic,
// no ImGui / IB dependency, so tests-core covers it.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace core::services {

namespace message_links_detail {

inline bool StartsWithNoCase(const std::string& s, std::size_t pos, const char* prefix) {
    for (std::size_t i = 0; prefix[i]; ++i) {
        if (pos + i >= s.size()) return false;
        if (std::tolower(static_cast<unsigned char>(s[pos + i])) != prefix[i]) return false;
    }
    return true;
}

// The end of a markup tag starting at `pos`, or npos. Only the tags IB uses, so
// a comparison such as "price<limit and stop>0" stays as text.
inline std::size_t TagEnd(const std::string& s, std::size_t pos) {
    if (s[pos] != '<') return std::string::npos;
    std::size_t i = pos + 1;
    if (i < s.size() && s[i] == '/') ++i;
    std::size_t nameEnd = i;
    while (nameEnd < s.size() && std::isalpha(static_cast<unsigned char>(s[nameEnd]))) ++nameEnd;
    std::string name = s.substr(i, nameEnd - i);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const kTags[] = { "a", "b", "br", "div", "font", "i", "p", "span" };
    if (std::none_of(std::begin(kTags), std::end(kTags),
                     [&](const char* t) { return name == t; }))
        return std::string::npos;
    return s.find('>', nameEnd);
}

inline void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (std::size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
        s.replace(p, from.size(), to);
}

} // namespace message_links_detail

// The message as plain text: tags become a space, runs of spaces collapse. A link
// written as bare text stays; one only inside a tag is left to ExtractLinks.
inline std::string CleanMessageText(const std::string& raw) {
    namespace d = message_links_detail;
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        const std::size_t end = d::TagEnd(raw, i);
        if (end != std::string::npos) {
            out += ' ';
            i = end + 1;
            continue;
        }
        const char c = raw[i++];
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (!space)                               out += c;
        else if (!out.empty() && out.back() != ' ') out += ' ';
        else continue;
    }
    // A tag's space can land next to a text space.
    d::ReplaceAll(out, "  ", " ");
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (!out.empty() && out.front() == ' ') out.erase(0, 1);
    d::ReplaceAll(out, "&amp;", "&");
    return out;
}

// The web links in a message, in order and without repeats. Only http(s) and
// www. addresses, so nothing else can be handed to the system to open.
inline std::vector<std::string> ExtractLinks(const std::string& raw) {
    namespace d = message_links_detail;
    std::vector<std::string> links;
    for (std::size_t i = 0; i < raw.size();) {
        const bool web = d::StartsWithNoCase(raw, i, "http://") || d::StartsWithNoCase(raw, i, "https://");
        const bool www = !web && d::StartsWithNoCase(raw, i, "www.") &&
                         (i == 0 || !std::isalnum(static_cast<unsigned char>(raw[i - 1])));
        if (!web && !www) { ++i; continue; }

        std::size_t end = i;
        while (end < raw.size()) {
            const char c = raw[end];
            if (std::isspace(static_cast<unsigned char>(c)) ||
                c == '"' || c == '\'' || c == '<' || c == '>')
                break;
            ++end;
        }
        std::string url = raw.substr(i, end - i);
        i = end;
        while (!url.empty() && std::string(".,;:!?)]}").find(url.back()) != std::string::npos)
            url.pop_back();
        d::ReplaceAll(url, "&amp;", "&");
        if (www) url.insert(0, "https://");
        if (std::find(links.begin(), links.end(), url) == links.end()) links.push_back(url);
    }
    return links;
}

} // namespace core::services
