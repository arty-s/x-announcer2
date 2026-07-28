#include "plugin/ui/main_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/settings.h"
#include "plugin/announcer.h"
#include "plugin/ui/theme.h"
#include "plugin/xa_log.h"

namespace xa {
namespace {

// The panel's rhythm, in one place. The label column is wide enough for the
// longest Russian label at this size; the control column is narrower than the
// window on purpose, so the eye has a straight edge to run down instead of
// controls stretching to wherever the window happens to end.
constexpr float kLabelColumn = 224.0f;
constexpr float kControlWidth = 208.0f;

// The window at scale 1.0. Everything else is this times the scale.
constexpr int kBaseWidth = 620;
constexpr int kBaseHeight = 520;

// Three levels of text, and now three sizes to carry them: the value leads, the
// label names it, the note explains it. In 1.x this had to be done with colour
// alone - one font, one size, no way to add either.
using ui::kEngraved;
using ui::kMet;
using ui::kWaiting;

void section(const char* title) { ui::sectionHeading(title); }

// A note in the small size: quieter by measure as well as by colour.
void small(const char* text) {
    if (ImFont* font = XpImguiWindow::smallFont()) {
        ImGui::PushFont(font);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kEngraved);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    if (XpImguiWindow::smallFont() != nullptr) {
        ImGui::PopFont();
    }
}

// Label on the left, control on the right at a fixed width.
void label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(kLabelColumn);
    ImGui::SetNextItemWidth(kControlWidth);
}

// The quietest level: why a setting exists, or what it costs.
void note(const char* text) {
    ImGui::Indent(12.0f);
    small(text);
    ImGui::Unindent(12.0f);
}

void copyInto(char* buffer, std::size_t size, const std::string& text) {
    const std::size_t length = std::min(text.size(), size - 1);
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
}

const char* phaseTitle(core::Phase phase) {
    switch (phase) {
        case core::Phase::Preflight: return "Подготовка";
        case core::Phase::Boarding:  return "Посадка";
        case core::Phase::Pushback:  return "Двери и брифинг";
        case core::Phase::Takeoff:   return "Взлёт";
        case core::Phase::Climb:     return "Набор";
        case core::Phase::Cruise:    return "Эшелон";
        case core::Phase::Descent:   return "Снижение";
        case core::Phase::Approach:  return "Заход";
        case core::Phase::TaxiIn:    return "После посадки";
        case core::Phase::Disembark: return "Высадка";
    }
    return "-";
}

}  // namespace

MainWindow::MainWindow(Announcer* announcer)
    : XpImguiWindow(kBaseWidth, kBaseHeight, "X-Announcer 2"), announcer_(announcer) {}

void MainWindow::syncTextBuffers() {
    const core::Settings& s = announcer_->settings();
    copyInto(libraryBuffer_, sizeof(libraryBuffer_), s.library);
    copyInto(languageBuffer_, sizeof(languageBuffer_), s.language);
    copyInto(seatbeltBuffer_, sizeof(seatbeltBuffer_), s.seatbeltDref);
    buffersFilled_ = true;
}

void MainWindow::buildUi() {
    if (announcer_ == nullptr) {
        ImGui::TextUnformatted("Плагин не запустился — смотрите Log.txt.");
        return;
    }
    if (!buffersFilled_) {
        syncTextBuffers();
    }
    // Text scaling is a setting because this window is read from a seat, at a
    // distance, sometimes in VR. Applied per frame so the slider moves the
    // panel while it is being dragged.
    const double scale = announcer_->settings().windowScale;
    ImGui::GetIO().FontGlobalScale = static_cast<float>(scale);
    if (appliedScale_ != scale) {
        appliedScale_ = scale;
        // The window grows with the text. Bigger letters in a window that stays
        // put just means less of the panel fits, which is the opposite of what
        // "scale" is asked for.
        resizeKeepingCorner(static_cast<int>(kBaseWidth * scale),
                            static_cast<int>(kBaseHeight * scale));
    }

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Рейс")) {
            drawFlightTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Библиотека")) {
            drawLibraryTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Настройки")) {
            drawSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Журнал")) {
            drawLogTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void MainWindow::drawFlightTab() {
    core::Engine& engine = announcer_->engine();
    const core::Snapshot& s = announcer_->lastSnapshot();

    // The phase is what this tab is for, so it is the one thing set in the large
    // size. Everything else on the tab supports it.
    if (ImFont* big = XpImguiWindow::largeFont()) {
        ImGui::PushFont(big);
    }
    ImGui::TextUnformatted(phaseTitle(engine.phase()));
    if (XpImguiWindow::largeFont() != nullptr) {
        ImGui::PopFont();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", core::phaseId(engine.phase()));

    // Said here as well as on the settings tab: this is the tab someone stares
    // at wondering why nothing is being announced.
    if (!announcer_->settings().flight.enabled) {
        ImGui::TextColored(kWaiting, "Объявления выключены — плагин молчит целиком.");
    }

    // What the machine is waiting for. This list is generated by the same code
    // the offline bench cross-checks against the state machine, so it cannot
    // quietly start lying about what is holding the flight up.
    section(("ДАЛЬШЕ — " + engine.nextPhaseLabel(s)).c_str());
    for (const core::Condition& c : engine.phaseConditions(s)) {
        ImGui::Indent(4.0f);
        ui::statusLamp(c.met);
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(c.met ? kMet : kWaiting, "%s", c.label.c_str());
        if (!c.value.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("— %s", c.value.c_str());
        }
        ImGui::Unindent(4.0f);
    }

    section("ЭФИР");
    if (announcer_->player().announcementActive()) {
        ImGui::TextColored(kMet, "%s", announcer_->player().announcementEvent().c_str());
    } else {
        ImGui::TextDisabled("тишина");
    }
    ImGui::Text("В очереди: %zu", engine.queueSize());
    if (engine.musicPlaying()) {
        ImGui::Text("Фон: %s", engine.musicEvent().c_str());
    }

    section("ЧТО ВИДИТ ПЛАГИН");
    ImGui::Text("на земле: %s   двигателей: %d   маяк: %s",
                s.onGround ? "да" : "нет", s.enginesRunning, s.beacon ? "вкл" : "выкл");
    ImGui::Text("высота: %.0f фт   над землёй: %.0f фт   верт.: %.0f фт/мин",
                s.altFt, s.aglFt, s.vsFpm);
    ImGui::Text("скорость: %.0f уз   местный час: %d   %s",
                s.gsKt, s.localHour, s.isDark() ? "темно" : "светло");
    const char* belt = s.seatbelt == core::Tri::Unknown ? "борт его не публикует"
                                                        : (s.seatbelt == core::Tri::On ? "горит" : "погашено");
    ImGui::Text("табло ремней: %s", belt);
    if (announcer_->simState().seatbeltDataref()[0] != '\0') {
        ImGui::TextDisabled("  %s", announcer_->simState().seatbeltDataref());
    }
}

void MainWindow::drawLibraryTab() {
    FileSoundLibrary& library = announcer_->library();
    ImGui::TextWrapped("Папка: %s", library.root().c_str());
    ImGui::Text("Паков: %zu, в выбранном объявлений: %d",
                library.packs().size(), library.eventCount());
    if (!library.packLanguage().empty()) {
        ImGui::TextDisabled("  язык пака: %s", library.packLanguage().c_str());
    }
    ImGui::Separator();

    // Recognising the airline and owning its sounds are shown as two separate
    // lines on purpose. Reporting "not detected" when the airline was in fact
    // recognised but unowned is the exact defect this port had to preserve a fix
    // for, and a panel that blurs the two invites it straight back.
    const core::AirlineVerdict& airline = announcer_->airline();
    const std::string name = announcer_->airlines().nameOf(airline.code);
    ImGui::Text("Авиакомпания: %s%s", airline.code.c_str(),
                name.empty() ? "" : (" — " + name).c_str());
    ImGui::TextDisabled("  как определено: %s", airline.source.c_str());
    if (airline.code != "Default" && library.pack() != airline.code) {
        ImGui::TextDisabled("  пака нет, играет «%s»", library.pack().c_str());
    }

    core::Settings& settings = announcer_->settings();
    bool automatic = settings.autoAirline();
    if (ImGui::Checkbox("Определять по ливрее", &automatic)) {
        settings.airlineMode = automatic ? "auto" : "manual";
        if (automatic) {
            announcer_->resolveAirline();
        } else {
            // Pinning starts from whatever is playing now, so switching to
            // manual never silently changes the pack under the user.
            settings.airlineManual = library.pack();
        }
        announcer_->settingsChanged();
    }
    ImGui::BeginDisabled(automatic);
    for (const std::string& packName : library.packs()) {
        const bool selected = packName == library.pack();
        if (ImGui::RadioButton(packName.c_str(), selected) && !selected) {
            library.selectPack(packName);
            settings.airlineManual = packName;
            announcer_->settingsChanged();
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Проверка звука без полёта:");
    static const char* const kProbeEvents[] = {"BoardingWelcome", "SafetyBriefing",
                                               "CrewSeatsTakeoff", "AfterLanding"};
    for (const char* event : kProbeEvents) {
        const bool available = library.has(event);
        ImGui::BeginDisabled(!available);
        if (ImGui::Button(event)) {
            announcer_->player().playAnnouncement(
                event, library.pathFor(event), static_cast<float>(settings.volume));
            log("UI: ручное воспроизведение %s", event);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (available) {
            ImGui::TextDisabled("%.1f с", library.duration(event));
        } else {
            ImGui::TextDisabled("нет в паке");
        }
    }
    if (ImGui::Button("Стоп")) {
        announcer_->player().stopAnnouncement();
    }
    ImGui::SameLine();
    if (ImGui::Button("Пересканировать")) {
        announcer_->rescanLibrary();
    }
}

void MainWindow::drawSettingsTab() {
    core::Settings& settings = announcer_->settings();

    // No file controls here, and no path. Settings apply as they are touched and
    // are written by themselves; buttons for saving and re-reading were solving
    // a problem the user does not have, and the path is in the log at start-up
    // for the one time a year somebody needs it.
    //
    // There is no Apply button either, on purpose - a cabin announcement you
    // cannot hear the effect of is a setting you cannot judge.
    section("ЗВУК");
    {
        float value = static_cast<float>(settings.volume);
        label("Громкость объявлений");
        if (ImGui::SliderFloat("##volume", &value, 0.0f, 1.0f, "%.2f")) {
            settings.volume = value;
            announcer_->settingsChanged();
        }

        value = static_cast<float>(settings.musicVolume);
        label("Громкость музыки");
        if (ImGui::SliderFloat("##music_volume", &value, 0.0f, 1.0f, "%.2f")) {
            settings.musicVolume = value;
            announcer_->settingsChanged();
        }

        value = static_cast<float>(settings.duck);
        label("Приглушение музыки");
        if (ImGui::SliderFloat("##duck", &value, 0.0f, 1.0f, "%.2f")) {
            settings.duck = value;
            announcer_->settingsChanged();
        }
        note("Во сколько раз тише становится музыка, пока говорит бортпроводник.");
        // The audio buses are NOT here. Choosing between interior, exterior, ui
        // and com1 asks the user to know how X-Plane's mixer routes sound, to
        // answer a question almost nobody has - the default is right for a cabin
        // announcement. The keys stay in config.ini for the rare case.
    }

    section("ЧТО ОБЪЯВЛЯТЬ");
    {
        const struct {
            const char* label;
            bool* value;
        } switches[] = {
            {"Объявления включены", &settings.flight.enabled},
            {"Музыка при посадке пассажиров", &settings.flight.boardingMusic},
            {"Шум салона в полёте", &settings.flight.cabinNoise},
            {"Начинать посадку самостоятельно", &settings.flight.autoBoarding},
            {"Приветствие командира", &settings.flight.pilotWelcome},
            {"Объявления про двери", &settings.flight.doorCalls},
            {"Свет в салоне ночью", &settings.flight.nightDim},
            {"Реакция салона на касание", &settings.flight.landingReaction},
        };
        for (const auto& item : switches) {
            if (ImGui::Checkbox(item.label, item.value)) {
                announcer_->settingsChanged();
            }
        }
        if (!settings.flight.enabled) {
            ImGui::TextColored(kWaiting, "  Сейчас плагин молчит целиком.");
        }

        int seconds = static_cast<int>(settings.flight.boardingRepeat);
        label("Повтор приветствия");
        if (ImGui::SliderInt("##boarding_repeat", &seconds, 30, 900, "%d с")) {
            settings.flight.boardingRepeat = seconds;
            announcer_->settingsChanged();
        }

        int loops = settings.flight.musicMaxLoops;
        label("Повторов музыки");
        if (ImGui::SliderInt("##music_max_loops", &loops, 0, 20)) {
            settings.flight.musicMaxLoops = loops;
            announcer_->settingsChanged();
        }
    }

    section("БИБЛИОТЕКА");
    {
        // Committed when the field loses focus, never per keystroke: rescanning
        // the disk after every letter of a path would be both slow and wrong.
        label("Папка со звуками");
        ImGui::InputText("##library", libraryBuffer_, sizeof(libraryBuffer_));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings.library = libraryBuffer_;
            announcer_->settingsChanged(true);
        }
        ImGui::PushTextWrapPos(0.0f);
        if (settings.library.empty()) {
            ImGui::TextDisabled("  Пусто — звуки берутся из папки рядом с плагином:");
        } else {
            ImGui::TextDisabled("  Читается:");
        }
        ImGui::TextDisabled("  %s", announcer_->libraryDir().c_str());
        ImGui::TextDisabled("  Внутри — по папке на авиакомпанию (SBI, AFL, DLH…), "
                            "как у MSFS Universal Announcer.");
        ImGui::PopTextWrapPos();
        label("Язык внутри пака");
        ImGui::InputText("##language", languageBuffer_, sizeof(languageBuffer_));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings.language = languageBuffer_;
            announcer_->settingsChanged(true);
        }
        note("Подпапка вроде en-us или ru. Если в паке она одна, берётся она "
             "независимо от этой строки.");
    }

    section("ПРОЧЕЕ");
    {
        label("Датареф табло ремней");
        ImGui::InputText("##seatbelt", seatbeltBuffer_, sizeof(seatbeltBuffer_));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings.seatbeltDref = seatbeltBuffer_;
            announcer_->settingsChanged();
        }
        // The panel has to say when what was typed is not what is being read.
        // Silently falling back looks identical to "my dataref works", and the
        // typo then lives in the file forever.
        const std::string& wanted = settings.seatbeltDref;
        const char* found = announcer_->simState().seatbeltDataref();
        if (wanted.empty()) {
            note("Пусто — плагин ищет сам.");
        } else if (wanted != found) {
            ImGui::TextColored(kWaiting, "  На этом борту такого датарефа нет — читается свой:");
        }
        ImGui::TextDisabled("  читается: %s",
                            found[0] == '\0' ? "этот борт табло не публикует" : found);

        float scale = static_cast<float>(settings.windowScale);
        label("Масштаб окна");
        if (ImGui::SliderFloat("##window_scale", &scale, 0.8f, 2.0f, "%.2f")) {
            settings.windowScale = scale;
            announcer_->settingsChanged();
        }
        // Getting back to 1.00 by dragging is fiddly, and this is the one
        // setting whose wrong value makes the panel harder to fix from inside.
        ImGui::SameLine();
        ImGui::BeginDisabled(settings.windowScale == 1.0);
        if (ImGui::Button("Сбросить")) {
            settings.windowScale = 1.0;
            announcer_->settingsChanged();
        }
        ImGui::EndDisabled();
    }

}

void MainWindow::drawLogTab() {
    if (ImGui::BeginChild("log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        // Wrapped, not clipped. Log lines carry full paths and dataref names,
        // and a line whose end is off the edge is a line that cannot be read
        // back - which is most of what this tab is for.
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& line : logTail(200)) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

}  // namespace xa
