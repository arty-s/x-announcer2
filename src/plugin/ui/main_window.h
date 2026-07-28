#pragma once

#include "plugin/imgui_xp/xp_imgui_window.h"

namespace xa {

// The skeleton window. Its only job is to prove, on a real machine, that the
// four things v2 depends on actually work: the plugin loads, it can read
// datarefs, ImGui draws, and Russian text renders.
class MainWindow : public XpImguiWindow {
public:
    MainWindow();

protected:
    void buildUi() override;

private:
    int frames_ = 0;
    float sliderValue_ = 0.5f;
    char textField_[64] = "";
};

}  // namespace xa
