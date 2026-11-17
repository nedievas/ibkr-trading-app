#pragma once
// A table cell showing a message that can be long and carry links (an IB reject
// or warning). A tooltip can't be clicked, so a click opens a popup with the
// full text, each link to open or copy, and the text to copy.

#include "imgui.h"
#include "core/services/MessageLinks.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ui {

// `shown` is what the cell displays; `raw` is the message. Draw it where the cell
// text would go. `width` 0 fills the column. Call inside a per-row PushID.
inline void DrawMessageCell(const char* id, const char* shown, const std::string& raw,
                            const ImVec4& color, float width = 0.0f) {
    char label[32];
    std::snprintf(label, sizeof(label), "###%s", id);
    ImGui::PushID(id);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float  w     = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const bool   clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_None,
                                             ImVec2(w, ImGui::GetTextLineHeight()));
    const bool   hovered = ImGui::IsItemHovered();
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(), ImGui::GetFontSize(), start, ImGui::GetColorU32(color),
        shown, nullptr, 0.0f, nullptr);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Click for the full message and any links");
    }
    if (clicked || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        ImGui::OpenPopup("##message");

    if (ImGui::BeginPopup("##message")) {
        const std::string text = core::services::CleanMessageText(raw);
        const float wrap = ImGui::GetFontSize() * 34.0f;
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();

        const std::vector<std::string> links = core::services::ExtractLinks(raw);
        if (!links.empty()) ImGui::Separator();
        for (std::size_t i = 0; i < links.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            // A long address is cut in the label; the link itself stays whole.
            std::string shortLink = links[i];
            if (shortLink.size() > 52) shortLink = shortLink.substr(0, 49) + "...";
            ImGui::TextLinkOpenURL(shortLink.c_str(), links[i].c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open: %s", links[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy link")) ImGui::SetClipboardText(links[i].c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::SmallButton("Copy message")) ImGui::SetClipboardText(text.c_str());
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

} // namespace ui
