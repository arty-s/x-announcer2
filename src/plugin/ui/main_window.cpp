#include "plugin/ui/main_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/settings.h"
#include "plugin/announcer.h"
#include "plugin/xa_log.h"
#include "plugin/xa_paths.h"

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

// Three levels of text, and only three: the value speaks, the label names it,
// the note explains it. Sizes are equal - this font has one size - so the
// hierarchy is carried by colour alone, which is why the steps are wide.
const ImVec4 kEngraved(0.52f, 0.58f, 0.64f, 1.0f);
const ImVec4 kMet(0.45f, 0.80f, 0.45f, 1.0f);
const ImVec4 kWaiting(0.90f, 0.75f, 0.35f, 1.0f);

// A section heading, engraved rather than printed: spaced out, dimmed, with a
// rule under it. Reads like the lettering on a cockpit panel and keeps the tab
// from becoming one long column of controls.
void section(const char* title) {
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kEngraved);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
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
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::Unindent(12.0f);
}

// Bus names as the settings file spells them, with the label a Russian speaker
// would actually use. Index is the index into core::audioBusNames().
const char* const kBusLabels[] = {"салон", "снаружи", "интерфейс", "COM1", "COM2", "земля"};

int busIndex(const std::string& name) {
    const std::vector<std::string>& names = core::audioBusNames();
    const auto it = std::find(names.begin(), names.end(), name);
    return it == names.end() ? 0 : static_cast<int>(it - names.begin());
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

    ImGui::Text("Фаза: %s", phaseTitle(engine.phase()));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", core::phaseId(engine.phase()));

    // Said here as well as on the settings tab: this is the tab someone stares
    // at wondering why nothing is being announced.
    if (!announcer_->settings().flight.enabled) {
        ImGui::TextColored(kWaiting, "Объявления выключены — плагин молчит целиком.");
    }

    // What the machine is waiting for. This list is generated by the same code
    // the offline bench cross-checks against the state machine, so it cannot
    // quietly start lying about what is holding the flight up.
    ImGui::Spacing();
    ImGui::Text("Дальше — «%s», ждём:", engine.nextPhaseLabel(s).c_str());
    for (const core::Condition& c : engine.phaseConditions(s)) {
        if (c.met) {
            ImGui::TextColored(kMet, "  [x] %s", c.label.c_str());
        } else {
            ImGui::TextColored(kWaiting, "  [ ] %s", c.label.c_str());
        }
        if (!c.value.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("— %s", c.value.c_str());
        }
    }

    ImGui::Separator();
    if (announcer_->player().announcementActive()) {
        ImGui::Text("Играет: %s", announcer_->player().announcementEvent().c_str());
    } else {
        ImGui::TextDisabled("Играет: тишина");
    }
    ImGui::Text("В очереди: %zu", engine.queueSize());
    if (engine.musicPlaying()) {
        ImGui::Text("Фон: %s", engine.musicEvent().c_str());
    }

    ImGui::Separator();
    ImGui::TextDisabled("Что видит плагин");
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

    // The file lives at the TOP, not buried under the last section. It holds the
    // settings this build has no controls for yet, and it is where a mistyped
    // value gets explained - and a button nobody can find is a button that does
    // not exist, which is exactly what the first acceptance run said about it.
    if (ImGui::Button("Перечитать файл")) {
        announcer_->loadSettings();
        announcer_->applySettings();
        announcer_->rescanLibrary();
        syncTextBuffers();
    }
    ImGui::SameLine();
    if (ImGui::Button("Сохранить сейчас")) {
        announcer_->saveSettings();
    }
    // Wrapped, both of them: the path is long, the window is narrow, and a line
    // running off the edge is the same defect the log tab just had fixed.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Правки сохраняются сами через пару секунд.");
    ImGui::TextDisabled("%s", configPath().c_str());
    ImGui::PopTextWrapPos();

    // Everything below writes straight into the live settings and takes effect
    // at once; the file follows a couple of seconds later. There is no Apply
    // button on purpose - a cabin announcement you cannot hear the effect of is
    // a setting you cannot judge.
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

        int bus = busIndex(settings.announceBus);
        label("Шина объявлений");
        if (ImGui::Combo("##announce_bus", &bus, kBusLabels, IM_ARRAYSIZE(kBusLabels))) {
            settings.announceBus = core::audioBusNames()[static_cast<std::size_t>(bus)];
            announcer_->settingsChanged();
        }

        bus = busIndex(settings.musicBus);
        label("Шина музыки");
        if (ImGui::Combo("##music_bus", &bus, kBusLabels, IM_ARRAYSIZE(kBusLabels))) {
            settings.musicBus = core::audioBusNames()[static_cast<std::size_t>(bus)];
            announcer_->settingsChanged();
        }
        note("Шина решает, как X-Plane обработает звук: «салон» глохнет снаружи, "
             "«интерфейс» слышен всегда. Новая шина применится к следующему объявлению.");
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

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextDisabled("Жалобы на непонятые строки файла — во вкладке «Журнал».");
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
