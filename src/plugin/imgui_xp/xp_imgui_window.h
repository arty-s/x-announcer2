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

    // Loads the Cyrillic-capable UI font once, shared by every window.
    // Safe to call more than once; returns false if the file is missing.
    static bool loadUiFont(const std::string& ttfPath, float sizePixels);

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
