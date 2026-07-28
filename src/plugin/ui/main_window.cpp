#include "plugin/ui/main_window.h"

#include <cstdio>

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include "plugin/xa_log.h"
#include "plugin/xa_paths.h"

namespace xa {
namespace {

// Cached because looking a dataref up every frame is wasteful, and because a
// missing dataref must stay missing: a null handle is information, not an error
// to paper over. The v2 core will lean on exactly this distinction (ToLiss keeps
// its battery off while the aircraft is live).
XPLMDataRef simTimeRef() {
    static XPLMDataRef ref = XPLMFindDataRef("sim/time/total_running_time_sec");
    return ref;
}

XPLMDataRef groundSpeedRef() {
    static XPLMDataRef ref = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    return ref;
}

}  // namespace

MainWindow::MainWindow() : XpImguiWindow(560, 460, "X-Announcer 2") {}

void MainWindow::buildUi() {
    ++frames_;

    ImGui::TextUnformatted("Каркас работает.");
    // Every character here has burned someone once. If any of them shows up as
    // '?', the glyph ranges in loadUiFont() lost a block.
    ImGui::TextUnformatted("Проверка кириллицы: Ёжик, объявление, «ёлки» — 0123456789");
    ImGui::TextUnformatted("Проверка пунктуации: тире — и – , многоточие… , № 5 , 12 ° , A → B");
    ImGui::Separator();

    int xplaneVersion = 0;
    int xplmVersion = 0;
    XPLMHostApplicationID host = xplm_Host_Unknown;
    XPLMGetVersions(&xplaneVersion, &xplmVersion, &host);
    ImGui::Text("X-Plane %d, XPLM %d", xplaneVersion, xplmVersion);
    ImGui::Text("Кадров нарисовано: %d", frames_);

    if (simTimeRef() != nullptr) {
        ImGui::Text("Время симулятора: %.1f с", XPLMGetDataf(simTimeRef()));
    } else {
        ImGui::TextUnformatted("Время симулятора: датареф не найден");
    }
    if (groundSpeedRef() != nullptr) {
        ImGui::Text("Путевая скорость: %.1f м/с", XPLMGetDataf(groundSpeedRef()));
    } else {
        ImGui::TextUnformatted("Путевая скорость: датареф не найден");
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Папка плагина: %s", pluginDir().empty() ? "не определена" : pluginDir().c_str());

    ImGui::Separator();
    ImGui::TextUnformatted("Проверка ввода:");
    if (ImGui::Button("Написать строку в Log.txt")) {
        log("UI: кнопка нажата на кадре %d", frames_);
    }
    ImGui::SliderFloat("Ползунок", &sliderValue_, 0.0f, 1.0f);
    ImGui::InputText("Поле ввода (латиница)", textField_, sizeof(textField_));

    ImGui::Separator();
    ImGui::TextUnformatted("Журнал (прокрутка проверяет отсечение):");
    if (ImGui::BeginChild("log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        for (const std::string& line : logTail(200)) {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::EndChild();
}

}  // namespace xa
