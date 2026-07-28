#include "plugin/imgui_xp/xp_imgui_window.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#if IBM
#include <windows.h>
#endif
#include <GL/gl.h>

#include "XPLMDefs.h"
#include "XPLMGraphics.h"

#include "plugin/imgui_xp/xp_imgui_render.h"
#include "plugin/ui/theme.h"
#include "plugin/xa_log.h"

namespace xa {
namespace {

// One atlas for every window: the font bitmap is the expensive part and there
// is no reason to pay for it twice.
ImFontAtlas* sharedAtlas() {
    static ImFontAtlas* atlas = new ImFontAtlas();
    return atlas;
}

GLuint g_atlasTexture = 0;
bool g_atlasBuilt = false;
ImFont* g_engravedFont = nullptr;
ImFont* g_noteFont = nullptr;
ImFont* g_focusFont = nullptr;

// A missing glyph degrades quietly - the character just becomes '?', the atlas
// builds, nothing fails. So state it in the log instead of waiting for someone
// to notice on screen.
void reportMissingGlyphs() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) {
        return;
    }
    // Only characters the interface actually uses belong here. Probing one we
    // never draw produces a warning nobody can act on - the arrows U+2190-2193
    // did exactly that: this build of Roboto-Medium carries 896 glyphs and has
    // no arrows at all (verified by walking its cmap), so the "widen the ranges"
    // advice sent the reader after a fix that does not exist.
    ImFont* font = io.Fonts->Fonts[0];
    static const ImWchar kProbe[] = {
        0x0401, 0x0451,          // Ё ё
        0x00AB, 0x00BB,          // « »
        0x2013, 0x2014,          // – —
        0x2026, 0x2116, 0x00B0,  // … № °
        0,
    };

    std::string missing;
    for (const ImWchar* cp = kProbe; *cp != 0; ++cp) {
        if (font->FindGlyphNoFallback(*cp) == nullptr) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "U+%04X ", static_cast<unsigned>(*cp));
            missing += buf;
        }
    }
    if (missing.empty()) {
        log("font: glyph probe OK");
    } else {
        log("font: MISSING GLYPHS %s- these render as '?'. Either the range is "
            "absent from loadUiFont() or the font file itself has no such glyph "
            "- check the font before widening ranges.", missing.c_str());
    }
}

// The atlas can only be uploaded from inside a draw callback - that is the one
// place X-Plane guarantees a live GL context.
void ensureAtlasUploaded() {
    if (g_atlasBuilt) {
        return;
    }
    ImFontAtlas* atlas = sharedAtlas();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    atlas->GetTexDataAsRGBA32(&pixels, &width, &height);

    glGenTextures(1, &g_atlasTexture);
    XPLMBindTexture2d(static_cast<int>(g_atlasTexture), 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    atlas->SetTexID(static_cast<ImTextureID>(g_atlasTexture));
    g_atlasBuilt = true;
    log("font atlas: %dx%d, GL texture %u", width, height, g_atlasTexture);
    reportMissingGlyphs();
}

double nowSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

ImGuiKey mapVirtualKey(char virtualKey) {
    switch (static_cast<unsigned char>(virtualKey)) {
        case XPLM_VK_BACK:   return ImGuiKey_Backspace;
        case XPLM_VK_TAB:    return ImGuiKey_Tab;
        case XPLM_VK_RETURN: return ImGuiKey_Enter;
        case XPLM_VK_ENTER:  return ImGuiKey_KeypadEnter;
        case XPLM_VK_ESCAPE: return ImGuiKey_Escape;
        case XPLM_VK_SPACE:  return ImGuiKey_Space;
        case XPLM_VK_END:    return ImGuiKey_End;
        case XPLM_VK_HOME:   return ImGuiKey_Home;
        case XPLM_VK_LEFT:   return ImGuiKey_LeftArrow;
        case XPLM_VK_UP:     return ImGuiKey_UpArrow;
        case XPLM_VK_RIGHT:  return ImGuiKey_RightArrow;
        case XPLM_VK_DOWN:   return ImGuiKey_DownArrow;
        case XPLM_VK_DELETE: return ImGuiKey_Delete;
        default:             return ImGuiKey_None;
    }
}

}  // namespace

bool XpImguiWindow::loadUiFont(const std::string& ttfPath, float sizePixels) {
    ImFontAtlas* atlas = sharedAtlas();

    // Cyrillic alone is not enough: Russian prose leans on the em dash, and a
    // codepoint outside the ranges silently renders as '?'. Nothing warns you -
    // this exact hole shipped in the first build and only a human eye caught it.
    // No arrows: this Roboto-Medium is a 896-glyph subset without U+2190-2193,
    // so asking for them only made the glyph probe cry wolf every start-up.
    static const ImWchar kPunctuation[] = {
        0x2010, 0x2027,  // dashes, quotation marks, ellipsis
        0x2030, 0x203A,  // per mille, single guillemets
        0x2116, 0x2116,  // numero sign
        0,
    };

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(atlas->GetGlyphRangesCyrillic());
    builder.AddRanges(kPunctuation);
    builder.AddChar(0x00B0);  // degree sign, used all over the flight readouts
    static ImVector<ImWchar> ranges;
    ranges.clear();
    builder.BuildRanges(&ranges);

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    ImFont* font = atlas->AddFontFromFileTTF(ttfPath.c_str(), sizePixels, &config, ranges.Data);
    if (font == nullptr) {
        log("font: FAILED to load '%s' - falling back to the built-in font "
            "(no Cyrillic)", ttfPath.c_str());
        atlas->AddFontDefault();
        return false;
    }
    // Same file, three more sizes. The atlas pays for each, which is why there
    // are four roles and not a scale for every occasion. Rounded to whole
    // pixels: this font is hinted and half a pixel of body text is visibly
    // softer than the line above it.
    const float engraved = std::floor(sizePixels * 0.6875f + 0.5f);
    const float note = std::floor(sizePixels * 0.8125f + 0.5f);
    const float focus = std::floor(sizePixels * 1.375f + 0.5f);
    g_engravedFont = atlas->AddFontFromFileTTF(ttfPath.c_str(), engraved, &config, ranges.Data);
    g_noteFont = atlas->AddFontFromFileTTF(ttfPath.c_str(), note, &config, ranges.Data);
    g_focusFont = atlas->AddFontFromFileTTF(ttfPath.c_str(), focus, &config, ranges.Data);
    log("font: loaded '%s' at %.0f / %.0f / %.0f / %.0f px (engraved/note/body/focus)",
        ttfPath.c_str(), engraved, note, sizePixels, focus);
    return true;
}

ImFont* XpImguiWindow::engravedFont() { return g_engravedFont; }
ImFont* XpImguiWindow::noteFont() { return g_noteFont; }
ImFont* XpImguiWindow::focusFont() { return g_focusFont; }

XpImguiWindow::XpImguiWindow(int width, int height, std::string title)
    : title_(std::move(title)) {
    context_ = ImGui::CreateContext(sharedAtlas());
    ImGui::SetCurrentContext(context_);

    ImGuiIO& io = ImGui::GetIO();
    // X-Plane's folder is not ours to litter in.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ui::applyPanelTheme();

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    const int left = screenLeft + 100;
    const int top = screenTop - 100;

    XPLMCreateWindow_t params;
    params.structSize = sizeof(params);
    params.left = left;
    params.top = top;
    params.right = left + width;
    params.bottom = top - height;
    params.visible = 0;
    params.refcon = this;
    params.drawWindowFunc = &XpImguiWindow::drawCb;
    params.handleMouseClickFunc = &XpImguiWindow::clickCb;
    params.handleRightClickFunc = &XpImguiWindow::rightClickCb;
    params.handleMouseWheelFunc = &XpImguiWindow::wheelCb;
    params.handleKeyFunc = &XpImguiWindow::keyCb;
    params.handleCursorFunc = &XpImguiWindow::cursorCb;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;

    window_ = XPLMCreateWindowEx(&params);
    if (window_ == nullptr) {
        log("window: XPLMCreateWindowEx FAILED");
        return;
    }
    XPLMSetWindowTitle(window_, title_.c_str());
    XPLMSetWindowResizingLimits(window_, 420, 320, 2000, 1600);
    XPLMSetWindowPositioningMode(window_, xplm_WindowPositionFree, -1);
    log("window: created '%s' %dx%d at %d,%d", title_.c_str(), width, height, left, top);
}

XpImguiWindow::~XpImguiWindow() {
    if (window_ != nullptr) {
        XPLMDestroyWindow(window_);
        window_ = nullptr;
    }
    if (context_ != nullptr) {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
}

void XpImguiWindow::setVisible(bool visible) {
    if (window_ != nullptr) {
        XPLMSetWindowIsVisible(window_, visible ? 1 : 0);
    }
}

bool XpImguiWindow::isVisible() const {
    return window_ != nullptr && XPLMGetWindowIsVisible(window_) != 0;
}

void XpImguiWindow::resizeKeepingCorner(int width, int height) {
    if (window_ == nullptr) {
        return;
    }
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    if (right - left == width && top - bottom == height) {
        return;
    }
    XPLMSetWindowGeometry(window_, left, top, left + width, top - height);
}

void XpImguiWindow::draw() {
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(window_, &left_, &top_, &right, &bottom);
    const int width = right - left_;
    const int height = top_ - bottom;
    if (width <= 0 || height <= 0) {
        return;
    }

    ImGui::SetCurrentContext(context_);
    ensureAtlasUploaded();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    const double now = nowSeconds();
    io.DeltaTime = lastFrameTime_ > 0.0 ? static_cast<float>(now - lastFrameTime_) : (1.0f / 60.0f);
    if (io.DeltaTime <= 0.0f) {
        io.DeltaTime = 1.0f / 60.0f;
    }
    lastFrameTime_ = now;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(title_.c_str(), nullptr, flags)) {
        buildUi();
    }
    ImGui::End();
    ImGui::Render();

    renderImGuiDrawData(ImGui::GetDrawData(), left_, top_);
}

void XpImguiWindow::updateMousePos(int x, int y) {
    ImGui::SetCurrentContext(context_);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x - left_), static_cast<float>(top_ - y));
}

int XpImguiWindow::handleClick(int x, int y, XPLMMouseStatus status, int button) {
    updateMousePos(x, y);
    ImGuiIO& io = ImGui::GetIO();
    if (status == xplm_MouseDown) {
        io.AddMouseButtonEvent(button, true);
        XPLMTakeKeyboardFocus(window_);
    } else if (status == xplm_MouseUp) {
        io.AddMouseButtonEvent(button, false);
    }
    return 1;
}

int XpImguiWindow::handleWheel(int x, int y, int wheel, int clicks) {
    updateMousePos(x, y);
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    if (wheel == 0) {
        io.AddMouseWheelEvent(0.0f, static_cast<float>(clicks));
    } else {
        io.AddMouseWheelEvent(static_cast<float>(clicks), 0.0f);
    }
    return 1;
}

void XpImguiWindow::handleKey(char key, XPLMKeyFlags flags, char virtualKey, int losingFocus) {
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    if (losingFocus != 0) {
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        return;
    }

    const bool down = (flags & xplm_DownFlag) != 0;
    io.AddKeyEvent(ImGuiMod_Ctrl, (flags & xplm_ControlFlag) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (flags & xplm_ShiftFlag) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (flags & xplm_OptionAltFlag) != 0);

    const ImGuiKey mapped = mapVirtualKey(virtualKey);
    if (mapped != ImGuiKey_None) {
        io.AddKeyEvent(mapped, down);
    }
    // X-Plane only ever hands us one byte, so typing is ASCII-only. Russian text
    // renders fine; entering Russian text does not. Nothing here needs it.
    if (down && static_cast<unsigned char>(key) >= 32 && static_cast<unsigned char>(key) < 127) {
        io.AddInputCharacter(static_cast<unsigned int>(key));
    }
}

void XpImguiWindow::drawCb(XPLMWindowID, void* refcon) {
    static_cast<XpImguiWindow*>(refcon)->draw();
}

int XpImguiWindow::clickCb(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    return static_cast<XpImguiWindow*>(refcon)->handleClick(x, y, status, 0);
}

int XpImguiWindow::rightClickCb(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    return static_cast<XpImguiWindow*>(refcon)->handleClick(x, y, status, 1);
}

void XpImguiWindow::keyCb(XPLMWindowID, char key, XPLMKeyFlags flags, char virtualKey,
                          void* refcon, int losingFocus) {
    static_cast<XpImguiWindow*>(refcon)->handleKey(key, flags, virtualKey, losingFocus);
}

XPLMCursorStatus XpImguiWindow::cursorCb(XPLMWindowID, int x, int y, void* refcon) {
    static_cast<XpImguiWindow*>(refcon)->updateMousePos(x, y);
    return xplm_CursorDefault;
}

int XpImguiWindow::wheelCb(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon) {
    return static_cast<XpImguiWindow*>(refcon)->handleWheel(x, y, wheel, clicks);
}

}  // namespace xa
