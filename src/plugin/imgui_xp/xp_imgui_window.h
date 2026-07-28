// An X-Plane floating window that hosts a Dear ImGui frame.
//
// Subclass it and implement buildUi(). Everything below the ImGui API - window
// creation, input plumbing, the font atlas, the GL bridge - lives here so the
// UI code never touches XPLM.
#pragma once

#include <string>

#include "XPLMDisplay.h"
#include "imgui.h"

namespace xa {

class XpImguiWindow {
public:
    XpImguiWindow(int width, int height, std::string title);
    virtual ~XpImguiWindow();

    XpImguiWindow(const XpImguiWindow&) = delete;
    XpImguiWindow& operator=(const XpImguiWindow&) = delete;

    void setVisible(bool visible);
    bool isVisible() const;
    XPLMWindowID id() const { return window_; }

    // Resizes to `width` x `height` boxels keeping the top-left corner where it
    // is. Scaling the text without this leaves the window the size it was, so a
    // bigger font simply means less of the panel fits - which is the opposite of
    // what someone asking for a bigger scale wants.
    void resizeKeepingCorner(int width, int height);

    // Loads the Cyrillic-capable UI font once, shared by every window, in four
    // sizes - and each size has a job, which is the part that was missing when
    // there were merely several of them:
    //
    //   focus    ~1.375x  the one thing a tab is about, once per tab
    //   body      1.0x    labels, values, the checklist
    //   note     ~0.81x   why a setting exists, what it costs
    //   engraved ~0.69x   section lettering, stencilled and tracked
    //
    // At the intended 16 px body that lands on 22 / 16 / 13 / 11. One typeface,
    // four steps of roughly 1.25 - 1.x had exactly one of each, which is why its
    // hierarchy had to be carried by colour alone.
    static bool loadUiFont(const std::string& ttfPath, float sizePixels);

    static ImFont* engravedFont();
    static ImFont* noteFont();
    static ImFont* focusFont();

protected:
    // Called inside NewFrame/Render with an ImGui window already open.
    virtual void buildUi() = 0;

    const std::string& title() const { return title_; }

private:
    void draw();
    int handleClick(int x, int y, XPLMMouseStatus status, int button);
    void handleKey(char key, XPLMKeyFlags flags, char virtualKey, int losingFocus);
    int handleWheel(int x, int y, int wheel, int clicks);
    void updateMousePos(int x, int y);

    static void drawCb(XPLMWindowID id, void* refcon);
    static int clickCb(XPLMWindowID id, int x, int y, XPLMMouseStatus status, void* refcon);
    static int rightClickCb(XPLMWindowID id, int x, int y, XPLMMouseStatus status, void* refcon);
    static void keyCb(XPLMWindowID id, char key, XPLMKeyFlags flags, char virtualKey,
                      void* refcon, int losingFocus);
    static XPLMCursorStatus cursorCb(XPLMWindowID id, int x, int y, void* refcon);
    static int wheelCb(XPLMWindowID id, int x, int y, int wheel, int clicks, void* refcon);

    XPLMWindowID window_ = nullptr;
    ImGuiContext* context_ = nullptr;
    std::string title_;
    double lastFrameTime_ = 0.0;

    // Geometry of the last drawn frame, needed to place mouse events.
    int left_ = 0;
    int top_ = 0;
};

}  // namespace xa
