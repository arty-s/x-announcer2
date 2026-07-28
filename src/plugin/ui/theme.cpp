#include "plugin/ui/theme.h"

#include <cfloat>

#include "plugin/imgui_xp/xp_imgui_window.h"

namespace xa::ui {
namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// The night surface. One hue, three depths - the panel never changes colour,
// only how much light reaches it.
const ImVec4 kNight = rgb(10, 18, 32);
const ImVec4 kNightDeep = rgb(5, 8, 15);

// Glass is white at a few percent, not a lighter grey: it takes its colour from
// the aurora behind it, which is what makes it read as glass and not as a box.
constexpr ImVec4 glass(float a) { return ImVec4(1.0f, 1.0f, 1.0f, a); }
const ImVec4 kGlass = glass(0.055f);
const ImVec4 kGlassLit = glass(0.09f);
const ImVec4 kGlassEdge = glass(0.14f);
const ImVec4 kHair = glass(0.07f);

// Controls are inset - darker than what surrounds them, because they receive
// content. A lighter fill would read as raised, which is the opposite.
constexpr ImVec4 inset(float a) { return ImVec4(0.0f, 0.0f, 0.0f, a); }

const ImVec4 kAccentSoft = rgb(56, 189, 248, 0.20f);
const ImVec4 kAccentLit = rgb(125, 211, 252);

// One pool of aurora light: a colour, where it sits as a fraction of the
// window, how wide it spreads, and how bright its middle gets.
struct Blob {
    float x;
    float y;
    float radius;
    ImVec4 colour;
    float peak;
};

const Blob kBlobs[] = {
    {0.14f, 0.06f, 0.58f, rgb(56, 189, 248), 0.20f},
    {0.88f, 0.28f, 0.52f, rgb(99, 102, 241), 0.17f},
    {0.60f, 1.04f, 0.60f, rgb(168, 85, 247), 0.13f},
};

}  // namespace

const ImVec4 kInk = rgb(242, 248, 251);
const ImVec4 kInkDim = rgb(226, 238, 246, 0.60f);
const ImVec4 kInkMute = rgb(214, 230, 240, 0.42f);
const ImVec4 kEngraved = rgb(180, 200, 214, 0.34f);

const ImVec4 kAccent = rgb(56, 189, 248);
const ImVec4 kMet = rgb(52, 224, 161);

void applyPanelTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Concentric: the window's corner is the largest, a panel inside it is
    // smaller by its padding, a control smaller again. Parent and child sharing
    // a radius is the single thing that makes glass look wrong.
    style.WindowRounding = 14.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.PopupRounding = 10.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;  // controls need an edge on a dark surface
    style.ChildBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    // Density: a working panel, not a brochure. Rows breathe vertically because
    // the checklist is read at a glance; horizontal padding stays tight.
    style.WindowPadding = ImVec2(16.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(10.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.GrabMinSize = 10.0f;
    style.ScrollbarSize = 12.0f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = kNight;
    colors[ImGuiCol_ChildBg] = kGlass;
    colors[ImGuiCol_PopupBg] = rgb(14, 27, 44);
    colors[ImGuiCol_Border] = kGlassEdge;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_Text] = kInk;
    colors[ImGuiCol_TextDisabled] = kInkMute;

    colors[ImGuiCol_FrameBg] = inset(0.24f);
    colors[ImGuiCol_FrameBgHovered] = inset(0.16f);
    colors[ImGuiCol_FrameBgActive] = inset(0.10f);

    colors[ImGuiCol_TitleBg] = kNightDeep;
    colors[ImGuiCol_TitleBgActive] = kNightDeep;
    colors[ImGuiCol_TitleBgCollapsed] = kNightDeep;

    colors[ImGuiCol_Button] = kGlass;
    colors[ImGuiCol_ButtonHovered] = kGlassLit;
    colors[ImGuiCol_ButtonActive] = glass(0.13f);

    colors[ImGuiCol_Header] = kAccentSoft;
    colors[ImGuiCol_HeaderHovered] = rgb(56, 189, 248, 0.26f);
    colors[ImGuiCol_HeaderActive] = rgb(56, 189, 248, 0.32f);

    colors[ImGuiCol_Separator] = kHair;
    colors[ImGuiCol_SeparatorHovered] = kGlassEdge;
    colors[ImGuiCol_SeparatorActive] = kAccent;

    // The accent, and the only place it appears: things the user is acting on.
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentLit;

    colors[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TabHovered] = kGlass;
    colors[ImGuiCol_TabSelected] = kGlassLit;
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TabDimmedSelected] = kGlass;
    colors[ImGuiCol_TabDimmedSelectedOverline] = kHair;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = glass(0.10f);
    colors[ImGuiCol_ScrollbarGrabHovered] = glass(0.16f);
    colors[ImGuiCol_ScrollbarGrabActive] = glass(0.22f);

    colors[ImGuiCol_NavCursor] = kAccent;
}

void drawAurora(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    // No blur exists here, so the falloff is built by hand: rings of the same
    // faint colour stacked from the outside in, so the alpha accumulates towards
    // the middle. Twenty-four steps is where the banding stops being visible at
    // these radii; fewer looks like a target, more costs triangles for nothing.
    constexpr int kSteps = 24;
    for (const Blob& blob : kBlobs) {
        const ImVec2 centre(min.x + width * blob.x, min.y + height * blob.y);
        const float radius = width * blob.radius;
        ImVec4 colour = blob.colour;
        colour.w = blob.peak / static_cast<float>(kSteps);
        const ImU32 packed = ImGui::GetColorU32(colour);
        for (int i = kSteps; i > 0; --i) {
            draw->AddCircleFilled(centre, radius * static_cast<float>(i) / kSteps, packed, 48);
        }
    }
}

void statusLamp(bool met) {
    const float size = ImGui::GetFontSize() * 0.56f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float top = pos.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 a(pos.x, top);
    const ImVec2 b(pos.x + size, top + size);
    if (met) {
        // A lit lamp has a halo. Two faint larger rounds under the lamp itself
        // are the whole trick - it is the only thing on the panel that glows,
        // which is why the eye finds the satisfied conditions first.
        ImVec4 halo = kMet;
        halo.w = 0.16f;
        const ImU32 packed = ImGui::GetColorU32(halo);
        draw->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), size * 1.5f, packed,
                              20);
        draw->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), size * 1.0f, packed,
                              20);
        draw->AddRectFilled(a, b, ImGui::GetColorU32(kMet), 2.0f);
    } else {
        draw->AddRect(a, b, ImGui::GetColorU32(kInkMute), 2.0f, 0, 1.5f);
    }
    // Reserve the space so the caller can simply SameLine() the label.
    ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
}

void sectionHeading(const char* text) {
    ImGui::Dummy(ImVec2(0.0f, 7.0f));

    ImFont* font = XpImguiWindow::engravedFont();
    if (font == nullptr) {
        font = ImGui::GetFont();
    }
    // AddText is given an explicit size, so the global scale has to be applied
    // here - it is not a style the draw list knows about.
    const float size = font->FontSize * ImGui::GetIO().FontGlobalScale;
    const float tracking = size * 0.20f;

    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colour = ImGui::GetColorU32(kEngraved);

    float x = start.x;
    for (const char* c = text; *c != '\0';) {
        const char* next = c + 1;
        while ((static_cast<unsigned char>(*next) & 0xC0) == 0x80) {
            ++next;
        }
        draw->AddText(font, size, ImVec2(x, start.y), colour, c, next);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, c, next).x + tracking;
        c = next;
    }

    ImGui::Dummy(ImVec2(x - start.x, size));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

}  // namespace xa::ui
