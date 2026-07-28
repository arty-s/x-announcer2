#include "plugin/ui/theme.h"

#include <string>

#include "plugin/imgui_xp/xp_imgui_window.h"

namespace xa::ui {
namespace {

// One hue, shifted only in lightness, as a cockpit surface would be. Numbers are
// the same ones the direction was approved on.
constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

const ImVec4 kWindowBg = rgb(16, 18, 21);
const ImVec4 kPanelBg = rgb(13, 16, 19);
const ImVec4 kControl = rgb(25, 29, 34);
const ImVec4 kControlHover = rgb(31, 36, 42);
const ImVec4 kControlActive = rgb(36, 42, 49);
const ImVec4 kBorder = rgb(38, 44, 51);
const ImVec4 kAccent = rgb(63, 182, 200);
const ImVec4 kAccentDim = rgb(29, 50, 56);
const ImVec4 kText = rgb(223, 229, 234);
const ImVec4 kTextDim = rgb(125, 134, 142);

}  // namespace

const ImVec4 kMet = rgb(86, 184, 119);
const ImVec4 kWaiting = rgb(217, 161, 58);
const ImVec4 kEngraved = rgb(108, 117, 125);

void applyPanelTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounder than a terminal, squarer than a phone. Small radii read as
    // instrument bezels; the large default ones read as a web page.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.PopupRounding = 4.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;  // controls need an edge on a dark surface
    style.ChildBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    // Density: a working panel, not a brochure. Rows breathe vertically because
    // the checklist is read at a glance; horizontal padding stays tight.
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(10.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.GrabMinSize = 10.0f;
    style.ScrollbarSize = 12.0f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = kWindowBg;
    colors[ImGuiCol_ChildBg] = kPanelBg;
    colors[ImGuiCol_PopupBg] = rgb(21, 25, 30);
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDim;

    colors[ImGuiCol_FrameBg] = kControl;
    colors[ImGuiCol_FrameBgHovered] = kControlHover;
    colors[ImGuiCol_FrameBgActive] = kControlActive;

    colors[ImGuiCol_TitleBg] = kWindowBg;
    colors[ImGuiCol_TitleBgActive] = kWindowBg;
    colors[ImGuiCol_TitleBgCollapsed] = kWindowBg;

    colors[ImGuiCol_Button] = rgb(26, 31, 37);
    colors[ImGuiCol_ButtonHovered] = rgb(35, 43, 50);
    colors[ImGuiCol_ButtonActive] = rgb(43, 53, 61);

    colors[ImGuiCol_Header] = kAccentDim;
    colors[ImGuiCol_HeaderHovered] = rgb(35, 60, 67);
    colors[ImGuiCol_HeaderActive] = rgb(40, 70, 78);

    colors[ImGuiCol_Separator] = rgb(30, 36, 42);
    colors[ImGuiCol_SeparatorHovered] = kBorder;
    colors[ImGuiCol_SeparatorActive] = kAccent;

    // The accent, and the only place it appears: things the user is acting on.
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = rgb(95, 201, 216);
    colors[ImGuiCol_FrameBgActive] = kControlActive;

    colors[ImGuiCol_Tab] = rgb(19, 23, 27);
    colors[ImGuiCol_TabHovered] = rgb(30, 40, 45);
    colors[ImGuiCol_TabSelected] = rgb(26, 34, 38);
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = rgb(19, 23, 27);
    colors[ImGuiCol_TabDimmedSelected] = rgb(26, 34, 38);
    colors[ImGuiCol_TabDimmedSelectedOverline] = kBorder;

    colors[ImGuiCol_ScrollbarBg] = kPanelBg;
    colors[ImGuiCol_ScrollbarGrab] = rgb(35, 41, 48);
    colors[ImGuiCol_ScrollbarGrabHovered] = rgb(45, 53, 61);
    colors[ImGuiCol_ScrollbarGrabActive] = rgb(55, 65, 74);

    colors[ImGuiCol_NavCursor] = kAccent;
}

void statusLamp(bool met) {
    const float size = ImGui::GetFontSize() * 0.62f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float top = pos.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 a(pos.x, top);
    const ImVec2 b(pos.x + size, top + size);
    if (met) {
        draw->AddRectFilled(a, b, ImGui::GetColorU32(kMet), 2.0f);
    } else {
        draw->AddRect(a, b, ImGui::GetColorU32(kWaiting), 2.0f, 0, 1.5f);
    }
    // Reserve the space so the caller can simply SameLine() the label.
    ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
}

void sectionHeading(const char* text) {
    // Letter-spaced by hand: ImGui has no tracking. Spaces go between UTF-8
    // codepoints, never between the bytes of one - splitting a Cyrillic letter
    // in half would render two replacement glyphs.
    std::string spaced;
    for (const char* c = text; *c != '\0'; ++c) {
        const bool continuation = (static_cast<unsigned char>(*c) & 0xC0) == 0x80;
        if (!continuation && c != text) {
            spaced.push_back(' ');
        }
        spaced.push_back(*c);
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (ImFont* small = XpImguiWindow::smallFont()) {
        ImGui::PushFont(small);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kEngraved);
    ImGui::TextUnformatted(spaced.c_str());
    ImGui::PopStyleColor();
    if (XpImguiWindow::smallFont() != nullptr) {
        ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

}  // namespace xa::ui
