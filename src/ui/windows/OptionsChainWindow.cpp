#include "ui/windows/OptionsChainWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/models/WindowGroup.h"
#include "core/services/state-io.h"
#include "ui/UiScale.h"

namespace ui {

using core::services::StrikeRange;

OptionsChainWindow::OptionsChainWindow() = default;

// ── Symbol ───────────────────────────────────────────────────────────────────

void OptionsChainWindow::SetSymbol(const std::string& sym) {
    if (sym == m_symbol) return;
    m_symbol = sym;
    std::snprintf(m_symbolBuf, sizeof(m_symbolBuf), "%s", sym.c_str());

    // A new underlying invalidates everything downstream.
    m_meta = core::OptionChainMeta{};
    m_meta.symbol      = sym;
    m_underlyingConId  = 0;
    m_underlyingPrice  = 0.0;
    m_expiryIdx        = 0;
    m_chainLoaded      = false;
    m_loading          = false;
    m_status.clear();
}

// ── Data in ──────────────────────────────────────────────────────────────────

void OptionsChainWindow::OnUnderlyingConId(int conId) {
    if (conId <= 0) return;
    m_underlyingConId = conId;
    // conId is the prerequisite for asking IB for the chain definition.
    if (m_loading && OnReqSecDefOptParams)
        OnReqSecDefOptParams(kSecDefReqId, m_symbol, m_underlyingConId);
}

void OptionsChainWindow::OnSecDefOptParams(int reqId, const std::string& tradingClass,
                                           const std::string& multiplier,
                                           int underlyingConId,
                                           const std::vector<std::string>& expirations,
                                           const std::vector<double>& strikes) {
    if (reqId != kSecDefReqId) return;
    // IB fires this once per listing exchange; fold them all into one meta.
    core::services::MergeChainDefinition(m_meta, tradingClass, multiplier,
                                         underlyingConId, expirations, strikes);
}

void OptionsChainWindow::OnSecDefOptParamsEnd(int reqId) {
    if (reqId != kSecDefReqId) return;
    m_loading     = false;
    m_chainLoaded = true;
    if (m_meta.expirations.empty() || m_meta.strikes.empty())
        m_status = "IB returned no option chain for this underlying.";
    else
        m_status.clear();
    if (m_expiryIdx >= (int)m_meta.expirations.size()) m_expiryIdx = 0;
}

void OptionsChainWindow::OnUnderlyingPrice(double last) {
    if (last > 0.0) m_underlyingPrice = last;
}

// ── Strike range ─────────────────────────────────────────────────────────────

StrikeRange OptionsChainWindow::VisibleStrikeRange() const {
    return core::services::StrikeRangeAroundAtm(m_meta.strikes, m_underlyingPrice,
                                                m_strikeRange);
}

// ── Rendering ────────────────────────────────────────────────────────────────

// Palette lifted from the sketch: dark terminal chrome, green calls half,
// red puts half, amber sigma bands.
namespace {
constexpr ImU32 kCallsHdrBg = IM_COL32( 24,  54,  38, 255);
constexpr ImU32 kPutsHdrBg  = IM_COL32( 60,  28,  30, 255);
constexpr ImU32 kStrikeHdrBg= IM_COL32( 32,  34,  40, 255);
constexpr ImU32 kCallItmBg  = IM_COL32( 22,  46,  33, 110);
constexpr ImU32 kPutItmBg   = IM_COL32( 54,  25,  27, 110);
constexpr ImU32 kAtmRowBg   = IM_COL32( 58,  52,  16, 150);
constexpr ImU32 kSigma1Col  = IM_COL32(200, 170,  60, 190);
constexpr ImU32 kSigma2Col  = IM_COL32(190, 150,  55, 140);
constexpr ImU32 kSpotCol    = IM_COL32(225, 228, 235, 210);

const ImVec4 kDim   = ImVec4(0.55f, 0.56f, 0.62f, 1.0f);
const ImVec4 kUp    = ImVec4(0.35f, 0.80f, 0.48f, 1.0f);
const ImVec4 kDown  = ImVec4(0.90f, 0.38f, 0.38f, 1.0f);

// Dashed horizontal rule, used for the +/-2 sigma bands.
void DashedHLine(ImDrawList* dl, float x0, float x1, float y, ImU32 col,
                 float dash = 6.0f, float gap = 4.0f, float thick = 1.0f) {
    for (float x = x0; x < x1; x += dash + gap)
        dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + dash, x1), y), col, thick);
}
}  // namespace

int OptionsChainWindow::DaysToExpiry(int idx) const {
    if (idx < 0 || idx >= (int)m_meta.expirations.size()) return -1;
    const std::string& e = m_meta.expirations[(std::size_t)idx];
    if (e.size() != 8) return -1;
    std::tm t{};
    t.tm_year = std::atoi(e.substr(0, 4).c_str()) - 1900;
    t.tm_mon  = std::atoi(e.substr(4, 2).c_str()) - 1;
    t.tm_mday = std::atoi(e.substr(6, 2).c_str());
    t.tm_hour = 12;                       // noon avoids DST edge flapping
    const std::time_t exp = std::mktime(&t);
    if (exp == (std::time_t)-1) return -1;
    const double secs = std::difftime(exp, std::time(nullptr));
    return (int)std::floor(secs / 86400.0) + 1;
}

void OptionsChainWindow::DrawToolbar() {
    FlexRow row;

    row.item(em(28));
    core::DrawGroupPicker(m_groupId, "##optchain_grp");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Symbol sync group");

    row.item(em(130));
    ImGui::SetNextItemWidth(em(130));
    DrawSymbolInput("##optchain_sym", m_symbolBuf, sizeof(m_symbolBuf), em(130),
                    [this](const std::string& s) {
                        SetSymbol(s);
                        if (OnBroadcastSymbol) OnBroadcastSymbol(s);
                    },
                    m_symSearch);

    // Strike count — the sketch's "Strikes: 20" dropdown.
    char strikesLbl[32];
    std::snprintf(strikesLbl, sizeof(strikesLbl), "Strikes: %d", m_strikeRange);
    row.item(FlexRow::buttonW(strikesLbl));
    if (ImGui::Button(strikesLbl)) ImGui::OpenPopup("##optchain_strikes");
    if (ImGui::BeginPopup("##optchain_strikes")) {
        static const int kOpts[] = {5, 10, 15, 20, 30, 50};
        for (int n : kOpts) {
            char l[16];
            std::snprintf(l, sizeof(l), "%d", n);
            if (ImGui::Selectable(l, m_strikeRange == n)) m_strikeRange = n;
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Strikes shown each side of ATM");

    row.item(FlexRow::buttonW("Cols [+]"));
    if (ImGui::Button("Cols [+]")) ImGui::OpenPopup("##optchain_cols");
    if (ImGui::BeginPopup("##optchain_cols")) {
        ImGui::TextColored(kDim, "Per side");
        ImGui::Checkbox("Last",          &m_showLast);
        ImGui::Checkbox("Volume",        &m_showVolume);
        ImGui::Checkbox("Open Interest", &m_showOi);
        ImGui::Checkbox("IV",            &m_showIv);
        ImGui::Separator();
        ImGui::TextColored(kDim, "Greeks");
        ImGui::Checkbox("Delta", &m_showDelta);
        ImGui::Checkbox("Gamma", &m_showGamma);
        ImGui::Checkbox("Theta", &m_showTheta);
        ImGui::Checkbox("Vega",  &m_showVega);
        ImGui::EndPopup();
    }

    // Auto-refresh toggle, right-hand side of the sketch's toolbar.
    row.item(FlexRow::buttonW("Auto ON") + em(40));
    if (ImGui::Button(m_autoRefresh ? "Auto ON" : "Auto OFF"))
        m_autoRefresh = !m_autoRefresh;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-request visible quotes on an interval.\n"
                          "Live streaming lands in a later task.");
    ImGui::SameLine(0.0f, em(4));
    ImGui::SetNextItemWidth(em(38));
    ImGui::DragInt("##optchain_auto_sec", &m_autoRefreshSec, 1.0f, 1, 300, "%ds");
}

void OptionsChainWindow::DrawUnderlyingStrip() {
    FlexRow row;

    row.item(FlexRow::textW(m_symbol.c_str()) + em(60));
    ImGui::TextUnformatted(m_symbol.c_str());
    ImGui::SameLine(0.0f, em(6));
    if (m_underlyingPrice > 0.0) ImGui::Text("%.2f", m_underlyingPrice);
    else                         ImGui::TextColored(kDim, "-");

    if (m_underlyingChange != 0.0) {
        char chg[48];
        std::snprintf(chg, sizeof(chg), "%+.2f (%+.2f%%)",
                      m_underlyingChange, m_underlyingChangePct);
        row.item(FlexRow::textW(chg));
        ImGui::TextColored(m_underlyingChange >= 0 ? kUp : kDown, "%s", chg);
    }

    // IVX + expected move stay blank until option greeks arrive (Task D2) —
    // showing a fabricated number here would be worse than showing none.
    row.item(em(90));
    ImGui::TextColored(kDim, "IVX");
    ImGui::SameLine(0.0f, em(5));
    if (m_ivx > 0.0) ImGui::Text("%.1f%%", m_ivx * 100.0);
    else             ImGui::TextColored(kDim, "-");

    row.item(em(190));
    ImGui::TextColored(kDim, "Expected Move");
    ImGui::SameLine(0.0f, em(5));
    if (m_expectedMove > 0.0 && m_underlyingPrice > 0.0)
        ImGui::Text("+/-%.2f (%.2f%%)", m_expectedMove,
                    m_expectedMove / m_underlyingPrice * 100.0);
    else
        ImGui::TextColored(kDim, "-");

    const int dte = DaysToExpiry(m_expiryIdx);
    if (dte >= 0) {
        char d[48];
        std::snprintf(d, sizeof(d), "%s - %dD",
                      m_meta.expirations[(std::size_t)m_expiryIdx].c_str(), dte);
        row.item(FlexRow::textW(d));
        ImGui::TextColored(kDim, "%s", d);
    }
}

void OptionsChainWindow::DrawExpiryTabs() {
    if (m_meta.expirations.empty()) return;
    FlexRow row;
    for (int i = 0; i < (int)m_meta.expirations.size(); ++i) {
        const std::string& e = m_meta.expirations[(std::size_t)i];
        const int dte = DaysToExpiry(i);
        char lbl[48];
        if (dte >= 0) std::snprintf(lbl, sizeof(lbl), "%s - %dD##exp%d", e.c_str(), dte, i);
        else          std::snprintf(lbl, sizeof(lbl), "%s##exp%d", e.c_str(), i);

        const bool active = (i == m_expiryIdx);
        row.item(FlexRow::buttonW(lbl));
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.78f, 1.0f));
        if (ImGui::SmallButton(lbl)) m_expiryIdx = i;
        if (active) ImGui::PopStyleColor();
    }
}

void OptionsChainWindow::DrawLegend() {
    ImGui::TextColored(kDim,
        "calls ITM left  -  puts ITM right      1 sigma solid  -  2 sigma dashed");
}

void OptionsChainWindow::DrawEmptyState(const char* msg) {
    ImGui::Dummy(ImVec2(0, em(20)));
    const float w  = ImGui::GetContentRegionAvail().x;
    const float tw = ImGui::CalcTextSize(msg).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - tw) * 0.5f);
    ImGui::TextColored(kDim, "%s", msg);
}

void OptionsChainWindow::DrawChainTable() {
    const StrikeRange r = VisibleStrikeRange();
    if (r.lo < 0) { DrawEmptyState("No strikes."); return; }

    // Sketch's default per-side columns are delta / bid / ask; the rest are
    // opt-in through "Cols [+]".
    int sideCols = 2;                       // bid + ask
    if (m_showDelta)  ++sideCols;
    if (m_showLast)   ++sideCols;
    if (m_showVolume) ++sideCols;
    if (m_showOi)     ++sideCols;
    if (m_showIv)     ++sideCols;
    if (m_showGamma)  ++sideCols;
    if (m_showTheta)  ++sideCols;
    if (m_showVega)   ++sideCols;

    const int totalCols = sideCols * 2 + 1;

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_BordersInnerV;

    if (!ImGui::BeginTable("##optchain", totalCols, flags)) return;

    // Calls half is mirrored: greeks outermost, bid/ask nearest the strike.
    auto setupCalls = [&]() {
        if (m_showVega)   ImGui::TableSetupColumn("vega##c",  0, em(50));
        if (m_showTheta)  ImGui::TableSetupColumn("theta##c", 0, em(50));
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##c", 0, em(50));
        if (m_showIv)     ImGui::TableSetupColumn("iv##c",    0, em(48));
        if (m_showOi)     ImGui::TableSetupColumn("oi##c",    0, em(52));
        if (m_showVolume) ImGui::TableSetupColumn("vol##c",   0, em(52));
        if (m_showLast)   ImGui::TableSetupColumn("last##c",  0, em(56));
        if (m_showDelta)  ImGui::TableSetupColumn("delta##c", 0, em(52));
        ImGui::TableSetupColumn("bid##c", 0, em(60));
        ImGui::TableSetupColumn("ask##c", 0, em(60));
    };
    auto setupPuts = [&]() {
        ImGui::TableSetupColumn("bid##p", 0, em(60));
        ImGui::TableSetupColumn("ask##p", 0, em(60));
        if (m_showDelta)  ImGui::TableSetupColumn("delta##p", 0, em(52));
        if (m_showLast)   ImGui::TableSetupColumn("last##p",  0, em(56));
        if (m_showVolume) ImGui::TableSetupColumn("vol##p",   0, em(52));
        if (m_showOi)     ImGui::TableSetupColumn("oi##p",    0, em(52));
        if (m_showIv)     ImGui::TableSetupColumn("iv##p",    0, em(48));
        if (m_showGamma)  ImGui::TableSetupColumn("gamma##p", 0, em(50));
        if (m_showTheta)  ImGui::TableSetupColumn("theta##p", 0, em(50));
        if (m_showVega)   ImGui::TableSetupColumn("vega##p",  0, em(50));
    };
    setupCalls();
    ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_NoHide, em(66));
    setupPuts();
    ImGui::TableSetupScrollFreeze(0, 2);

    // ── Group band: CALLS | STRIKE | PUTS ───────────────────────────────────
    // ImGui tables have no spanning cells, so the band is a normal row whose
    // cells are individually tinted, with the label in each group's middle.
    ImGui::TableNextRow();
    const int callsMid  = sideCols / 2;
    const int putsMid   = sideCols + 1 + sideCols / 2;
    for (int c = 0; c < totalCols; ++c) {
        ImGui::TableSetColumnIndex(c);
        const bool isStrike = (c == sideCols);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               isStrike ? kStrikeHdrBg
                                        : (c < sideCols ? kCallsHdrBg : kPutsHdrBg));
        if (c == callsMid)      ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.58f, 1.0f), "CALLS");
        else if (isStrike)      ImGui::TextColored(ImVec4(0.80f, 0.82f, 0.88f, 1.0f), "STRIKE");
        else if (c == putsMid)  ImGui::TextColored(ImVec4(0.92f, 0.48f, 0.48f, 1.0f), "PUTS");
    }

    // ── Sub-header row ──────────────────────────────────────────────────────
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    int col = 0;
    auto hdr = [&](const char* label) {
        ImGui::TableSetColumnIndex(col++);
        ImGui::TableHeader(label);
    };
    if (m_showVega)   hdr("vega");
    if (m_showTheta)  hdr("theta");
    if (m_showGamma)  hdr("gamma");
    if (m_showIv)     hdr("iv");
    if (m_showOi)     hdr("oi");
    if (m_showVolume) hdr("vol");
    if (m_showLast)   hdr("last");
    if (m_showDelta)  hdr("delta");
    hdr("bid"); hdr("ask");
    hdr("price");
    hdr("bid"); hdr("ask");
    if (m_showDelta)  hdr("delta");
    if (m_showLast)   hdr("last");
    if (m_showVolume) hdr("vol");
    if (m_showOi)     hdr("oi");
    if (m_showIv)     hdr("iv");
    if (m_showGamma)  hdr("gamma");
    if (m_showTheta)  hdr("theta");
    if (m_showVega)   hdr("vega");

    const int atm = core::services::FindAtmIndex(m_meta.strikes, m_underlyingPrice);

    // Y positions of the rules we overlay after EndTable (drawing inside the
    // table would be clipped by the cell the cursor happens to be in).
    struct Rule { float y; ImU32 col; bool dashed; const char* label; };
    std::vector<Rule> rules;
    const double spot  = m_underlyingPrice;
    const double sigma = m_expectedMove;      // 0 until greeks arrive

    for (int i = r.lo; i <= r.hi; ++i) {
        const double strike = m_meta.strikes[(std::size_t)i];
        ImGui::TableNextRow();
        const float rowTop = ImGui::GetCursorScreenPos().y;

        // Boundary rules sit between the previous strike and this one.
        if (i > r.lo && spot > 0.0) {
            const double prev = m_meta.strikes[(std::size_t)(i - 1)];
            auto crosses = [&](double level) {
                return (prev < level && strike >= level) || (prev > level && strike <= level);
            };
            if (crosses(spot)) rules.push_back({rowTop, kSpotCol, false, "spot"});
            if (sigma > 0.0) {
                if (crosses(spot - sigma))       rules.push_back({rowTop, kSigma1Col, false, "-1 sigma"});
                if (crosses(spot + sigma))       rules.push_back({rowTop, kSigma1Col, false, "+1 sigma"});
                if (crosses(spot - 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "-2 sigma"});
                if (crosses(spot + 2.0 * sigma)) rules.push_back({rowTop, kSigma2Col, true,  "+2 sigma"});
            }
        }

        if (i == atm)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kAtmRowBg);

        // In-the-money shading: calls ITM below spot, puts ITM above.
        const bool callItm = spot > 0.0 && strike < spot;
        const bool putItm  = spot > 0.0 && strike > spot;

        int c = 0;
        auto emptyCells = [&](int n, bool itm, ImU32 tint) {
            for (int k = 0; k < n; ++k) {
                ImGui::TableSetColumnIndex(c);
                if (itm && i != atm)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
                // Quotes land in Task D2; an em dash beats a fabricated price.
                ImGui::TextColored(kDim, "-");
                ++c;
            }
        };
        emptyCells(sideCols, callItm, kCallItmBg);

        ImGui::TableSetColumnIndex(c++);
        ImGui::Text("%.2f", strike);

        emptyCells(sideCols, putItm, kPutItmBg);
    }

    const ImVec2 tblMin = ImGui::GetItemRectMin();
    const ImVec2 tblMax = ImGui::GetItemRectMax();
    ImGui::EndTable();

    // Overlay the spot / sigma rules across the table width.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const Rule& ru : rules) {
        if (ru.y < tblMin.y || ru.y > tblMax.y) continue;   // scrolled out of view
        if (ru.dashed) DashedHLine(dl, tblMin.x, tblMax.x, ru.y, ru.col);
        else           dl->AddLine(ImVec2(tblMin.x, ru.y), ImVec2(tblMax.x, ru.y), ru.col, 1.2f);
        dl->AddText(ImVec2(tblMin.x + em(4), ru.y - em(11)), ru.col, ru.label);
    }
}

bool OptionsChainWindow::Render() {
    if (!m_open) return false;

    char title[96];
    std::snprintf(title, sizeof(title), "Options Chain%s%s G%d###optionschain",
                  m_symbol.empty() ? "" : " ", m_symbol.c_str(), m_groupId);

    ImGui::SetNextWindowSize(ImVec2(em(900), em(520)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &m_open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return m_open;
    }

    DrawToolbar();
    ImGui::Separator();

    if (!m_symbol.empty()) {
        DrawUnderlyingStrip();
        ImGui::Separator();
    }
    if (m_chainLoaded && !m_meta.expirations.empty()) {
        DrawExpiryTabs();
        ImGui::Separator();
    }

    if (!m_status.empty())
        ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f), "%s", m_status.c_str());

    if (m_symbol.empty())            DrawEmptyState("Enter an underlying symbol, then Load Chain.");
    else if (m_loading)              DrawEmptyState("Loading chain…");
    else if (!m_chainLoaded)         DrawEmptyState("Press Load Chain.");
    else if (m_meta.strikes.empty()) DrawEmptyState("No strikes returned for this underlying.");
    else                           { DrawChainTable(); DrawLegend(); }

    ImGui::End();
    return m_open;
}

// ── State persistence ────────────────────────────────────────────────────────

void OptionsChainWindow::SerializeSettings(core::services::StateBlock& b) const {
    using namespace core::services;
    SetBool  (b, "OPT_OPEN",        m_open);
    SetInt   (b, "OPT_GROUP",       m_groupId);
    SetString(b, "OPT_SYMBOL",      m_symbol);
    SetInt   (b, "OPT_EXPIRY_IDX",  m_expiryIdx);
    SetInt   (b, "OPT_STRIKE_RANGE",m_strikeRange);
    SetBool  (b, "OPT_AUTO",        m_autoRefresh);
    SetInt   (b, "OPT_AUTO_SEC",    m_autoRefreshSec);
    SetBool  (b, "OPT_COL_LAST",    m_showLast);
    SetBool  (b, "OPT_COL_VOLUME",  m_showVolume);
    SetBool  (b, "OPT_COL_OI",      m_showOi);
    SetBool  (b, "OPT_COL_IV",      m_showIv);
    SetBool  (b, "OPT_COL_DELTA",   m_showDelta);
    SetBool  (b, "OPT_COL_GAMMA",   m_showGamma);
    SetBool  (b, "OPT_COL_THETA",   m_showTheta);
    SetBool  (b, "OPT_COL_VEGA",    m_showVega);
}

void OptionsChainWindow::ApplySettings(const core::services::StateBlock& b) {
    using namespace core::services;
    m_open        = GetBool(b, "OPT_OPEN", m_open);
    m_groupId     = GetInt (b, "OPT_GROUP", m_groupId, 1, core::kNumGroups);
    m_strikeRange = GetInt (b, "OPT_STRIKE_RANGE", m_strikeRange, 1, 200);
    m_expiryIdx   = GetInt (b, "OPT_EXPIRY_IDX", 0, 0, 1000);

    const std::string sym = GetString(b, "OPT_SYMBOL", "");
    if (!sym.empty()) {
        // Restore the symbol only — no IB traffic on apply. The user presses
        // Load Chain, exactly as the Scanner restores filters without scanning.
        m_symbol = sym;
        m_meta.symbol = sym;
        std::snprintf(m_symbolBuf, sizeof(m_symbolBuf), "%s", sym.c_str());
    }

    m_autoRefresh    = GetBool(b, "OPT_AUTO", m_autoRefresh);
    m_autoRefreshSec = GetInt (b, "OPT_AUTO_SEC", m_autoRefreshSec, 1, 300);
    m_showLast   = GetBool(b, "OPT_COL_LAST",   m_showLast);
    m_showVolume = GetBool(b, "OPT_COL_VOLUME", m_showVolume);
    m_showOi     = GetBool(b, "OPT_COL_OI",     m_showOi);
    m_showIv     = GetBool(b, "OPT_COL_IV",     m_showIv);
    m_showDelta  = GetBool(b, "OPT_COL_DELTA",  m_showDelta);
    m_showGamma  = GetBool(b, "OPT_COL_GAMMA",  m_showGamma);
    m_showTheta  = GetBool(b, "OPT_COL_THETA",  m_showTheta);
    m_showVega   = GetBool(b, "OPT_COL_VEGA",   m_showVega);
}

}  // namespace ui
