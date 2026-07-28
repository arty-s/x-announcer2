#pragma once

#include "plugin/imgui_xp/xp_imgui_window.h"

namespace xa {

class Announcer;

// The panel. Its job for now is diagnosis, not decoration: it has to show what
// the plugin sees, what it is waiting for and what it is playing, because that
// is what a live acceptance run needs. Proper visual work comes with the
// interface-design pass, once the behaviour is settled.
class MainWindow : public XpImguiWindow {
public:
    explicit MainWindow(Announcer* announcer);

protected:
    void buildUi() override;

private:
    void drawFlightTab();
    void drawLibraryTab();
    void drawLogTab();

    Announcer* announcer_ = nullptr;
};

}  // namespace xa
