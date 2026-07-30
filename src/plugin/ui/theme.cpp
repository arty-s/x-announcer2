#include "plugin/ui/theme.h"

#include <cfloat>
#include <cmath>

// Writing vertices by hand needs two things the public header only forward
// declares: the shared draw data, for the atlas coordinate of the white pixel,
// and IM_PI. This is the header ImGui itself points custom widgets at; the
// alternative is a hand-rolled radial fade with no way to say "solid colour
// here", which is the whole point of the exercise.
#include "imgui_internal.h"

#include "plugin/imgui_xp/xp_imgui_window.h"
#include "plugin/xa_log.h"

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

// The peaks were first chosen blind - the aurora had never once reached the
// screen, so nobody could say whether they were right. At four times these the
// light was called "охрененный", and at four times these the quietest ink (42%
// white) starts losing the contrast it needs over a lit blue. Half of what was
// praised keeps the gradient unmistakable and the text where it was.
const Blob kBlobs[] = {
    {0.14f, 0.06f, 0.62f, rgb(56, 189, 248), 0.44f},
    {0.88f, 0.28f, 0.56f, rgb(99, 102, 241), 0.38f},
    {0.60f, 1.04f, 0.64f, rgb(168, 85, 247), 0.30f},
};

// A soft round pool of light, faded out by interpolating alpha across the
// vertices instead of stacking rings of flat colour.
//
// Stacked rings were the first attempt and they banded visibly: every ring is a
// filled polygon with one alpha, so its rim is a hard step no matter how many
// rings there are - and the polygon edges line up into concentric circles. Here
// the fade happens between vertices, where the GPU interpolates it, so there are
// no edges to see. Four rings of vertices approximate the falloff of a blur
// closely enough that the eye stops looking for the shape.
void radialGlow(ImDrawList* draw, const ImVec2& centre, float radius, const ImVec4& colour,
                float peak) {
    constexpr int kSegments = 64;
    constexpr int kRings = 4;
    static const float kStop[kRings + 1] = {0.0f, 0.34f, 0.60f, 0.82f, 1.0f};
    static const float kFade[kRings + 1] = {1.0f, 0.60f, 0.28f, 0.09f, 0.0f};

    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    draw->PrimReserve(kRings * kSegments * 6, (kRings + 1) * kSegments);
    const unsigned int base = draw->_VtxCurrentIdx;

    for (int ring = 0; ring <= kRings; ++ring) {
        ImVec4 shade = colour;
        shade.w = peak * kFade[ring];
        const ImU32 packed = ImGui::GetColorU32(shade);
        const float r = radius * kStop[ring];
        for (int s = 0; s < kSegments; ++s) {
            const float angle = (static_cast<float>(s) / kSegments) * 2.0f * IM_PI;
            draw->PrimWriteVtx(ImVec2(centre.x + std::cos(angle) * r,
                                      centre.y + std::sin(angle) * r),
                               uv, packed);
        }
    }
    for (int ring = 0; ring < kRings; ++ring) {
        for (int s = 0; s < kSegments; ++s) {
            const int next = (s + 1) % kSegments;
            const unsigned int inner = base + ring * kSegments;
            const unsigned int outer = base + (ring + 1) * kSegments;
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(inner + s));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(inner + next));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(outer + s));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(inner + next));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(outer + next));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(outer + s));
        }
    }
}

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
    // No outline on controls. A hairline round a button is what a glass panel
    // looks like in a browser, where there is a blurred backdrop behind it to
    // catch the light; here there is none, so the line is just a wireframe drawn
    // round a shape that is barely filled - and it read as exactly that. A
    // control now says it is a control by being LIGHTER than what it sits on,
    // which is also the only cue that survives being looked at from a seat.
    style.FrameBorderSize = 0.0f;
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

    // Fields and slider tracks go the other way - darker than the panel, because
    // they receive content rather than offer an action.
    colors[ImGuiCol_FrameBg] = inset(0.30f);
    colors[ImGuiCol_FrameBgHovered] = inset(0.22f);
    colors[ImGuiCol_FrameBgActive] = inset(0.16f);

    colors[ImGuiCol_TitleBg] = kNightDeep;
    colors[ImGuiCol_TitleBgActive] = kNightDeep;
    colors[ImGuiCol_TitleBgCollapsed] = kNightDeep;

    // Buttons are filled, and the three states are far enough apart to be felt
    // rather than looked for: 5% steps are invisible on a dark surface.
    colors[ImGuiCol_Button] = glass(0.12f);
    colors[ImGuiCol_ButtonHovered] = glass(0.20f);
    colors[ImGuiCol_ButtonActive] = glass(0.28f);

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

    for (const Blob& blob : kBlobs) {
        radialGlow(draw, ImVec2(min.x + width * blob.x, min.y + height * blob.y),
                   width * blob.radius, blob.colour, blob.peak);
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
        // A lit lamp has a halo, drawn by the same graded pool the aurora uses.
        // Circles of flat colour were tried and read as rings around the lamp
        // rather than as light coming off it.
        radialGlow(draw, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), size * 2.1f, kMet, 0.34f);
        draw->AddRectFilled(a, b, ImGui::GetColorU32(kMet), 2.0f);
    } else {
        draw->AddRect(a, b, ImGui::GetColorU32(kInkMute), 2.0f, 0, 1.5f);
    }
    // Reserve the space so the caller can simply SameLine() the label.
    ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
}

bool checkBox(const char* label, bool* value) {
    ImGuiStyle& style = ImGui::GetStyle();
    const float line = ImGui::GetTextLineHeight();
    const float box = std::floor(line * 0.98f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();

    // The row is a text line tall and the box slightly less, so a switch sits on
    // the same baseline grid as everything else on the tab.
    const bool pressed = ImGui::InvisibleButton(
        label, ImVec2(box + style.ItemInnerSpacing.x + textSize.x, line));
    if (pressed) {
        *value = !*value;
    }
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float top = start.y + (line - box) * 0.5f;
    const ImVec2 a(start.x, top);
    const ImVec2 b(start.x + box, top + box);

    // Four pixels, not the panel's six: a radius meant for a slider eats most of
    // a 16 px edge, and what is left of the stroke breaks up into dashes.
    constexpr float kRadius = 4.0f;
    if (*value) {
        draw->AddRectFilled(a, b, ImGui::GetColorU32(kAccent), kRadius);
    } else {
        draw->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.24f)), kRadius);
        // The edge is carried at full strength rather than the glass 14%: on a
        // shape this small a hairline that faint is indistinguishable from a
        // dotted one once anti-aliasing has had its say.
        draw->AddRect(a, b, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, hovered ? 0.42f : 0.30f)),
                      kRadius, 0, 1.4f);
    }

    if (*value) {
        // Drawn rather than typed: this Roboto subset has no tick glyph, and
        // ImGui's own tick takes its weight from the font size with no say in
        // the matter. A cabin panel wants a mark that reads at a glance.
        const float thickness = box * 0.17f > 2.0f ? box * 0.17f : 2.0f;
        const ImU32 ink = ImGui::GetColorU32(ImVec4(0.02f, 0.11f, 0.19f, 1.0f));
        draw->PathLineTo(ImVec2(a.x + box * 0.24f, a.y + box * 0.52f));
        draw->PathLineTo(ImVec2(a.x + box * 0.43f, a.y + box * 0.72f));
        draw->PathLineTo(ImVec2(a.x + box * 0.78f, a.y + box * 0.28f));
        draw->PathStroke(ink, 0, thickness);
    }

    const ImVec2 textAt(b.x + style.ItemInnerSpacing.x, start.y);
    draw->AddText(textAt, ImGui::GetColorU32(hovered ? kInk : kInkDim), label);
    return pressed;
}

void sectionHeading(const char* text) {
    ImGui::Dummy(ImVec2(0.0f, 9.0f));
    if (ImFont* font = XpImguiWindow::headingFont()) {
        ImGui::PushFont(font);
    }
    ImGui::TextUnformatted(text);
    if (XpImguiWindow::headingFont() != nullptr) {
        ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

void stencilText(const char* text) {
    ImFont* font = ImGui::GetFont();
    // AddText is given an explicit size, so the global scale has to be applied
    // here - it is not a style the draw list knows about.
    const float size = font->FontSize * ImGui::GetIO().FontGlobalScale;
    const float tracking = size * 0.18f;

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
    ImGui::Dummy(ImVec2(x - start.x, ImGui::GetTextLineHeight()));
}

}  // namespace xa::ui
