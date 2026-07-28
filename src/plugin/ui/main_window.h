#pragma once

#include "plugin/imgui_xp/xp_imgui_window.h"

namespace xa {

class Announcer;

// The panel.
//
// It is read in a cockpit, over the instruments, in the gaps between things the
// pilot is actually doing - so it is laid out like a flight-deck panel rather
// than a settings page: engraved section headings, a fixed label column with the
// control beside it, and three levels of text (value, label, note) doing the
// hierarchy instead of size. The phase list on the Flight tab is a crew
// checklist, which is where that language comes from.
class MainWindow : public XpImguiWindow {
public:
    explicit MainWindow(Announcer* announcer);

protected:
    void buildUi() override;

private:
    void drawFlightTab();
    void drawLibraryTab();
    void drawSettingsTab();
    void drawLogTab();

    // ImGui edits text in place, so the one free-text setting left in the panel
    // needs a buffer of its own. It is refilled from the settings whenever the
    // file is re-read, and written back when the field loses focus rather than
    // on every keystroke - a half-typed folder name is not a folder name.
    void syncTextBuffers();

    Announcer* announcer_ = nullptr;
    // Which scale the window size currently reflects. Applied only when it
    // changes, so resizing the window by hand is not undone on the next frame.
    double appliedScale_ = 0.0;
    bool buffersFilled_ = false;
    char libraryBuffer_[512] = {0};
};

}  // namespace xa
