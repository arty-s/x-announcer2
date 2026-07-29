#include "plugin/ui/main_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

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

// Four sizes with a job each, and four strengths of the same ink. Hierarchy is
// carried by both together: the focus size says "this is what the tab is about",
// the ink strength says "this is a value, that is a label, that is an aside". In
// 1.x it had to be done with colour alone - one font, one size, no way to add
// either.
using ui::kAccent;
using ui::kInkDim;
using ui::kInkMute;
using ui::kMet;

void section(const char* title) { ui::sectionHeading(title); }

// A note in the note size: quieter by measure as well as by colour.
void small(const char* text) {
    if (ImFont* font = XpImguiWindow::noteFont()) {
        ImGui::PushFont(font);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kInkMute);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    if (XpImguiWindow::noteFont() != nullptr) {
        ImGui::PopFont();
    }
}

// Label on the left, control on the right at a fixed width. The label is dimmer
// than the value it names - it is read once, the value is read every time.
void label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kInkDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelColumn);
    ImGui::SetNextItemWidth(kControlWidth);
}

// A switch and the sentence that says what it does. The sentence is not
// decoration: eight short labels in two columns were unreadable precisely
// because the short label has to leave something out, and what it left out was
// the difference between "announce the lights" and "control the lights".
void switchCell(const char* title, bool* value, const char* explanation,
                bool* changed) {
    if (ui::checkBox(title, value)) {
        *changed = true;
    }
    ImGui::PushTextWrapPos(0.0f);
    small(explanation);
    ImGui::PopTextWrapPos();
}

// Rounds towards a signed zero and takes the sign off. A vertical speed of
// -0.4 fpm printed as "-0", which reads as a value whose sign someone is meant
// to act on; zero has no sign. The same guard sits in the core's formatFeet, for
// the readings that come through the condition list rather than through here.
double whole(double v) { return v > -0.5 && v < 0.5 ? 0.0 : v; }

void copyInto(char* buffer, std::size_t size, const std::string& text) {
    const std::size_t length = std::min(text.size(), size - 1);
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
}

// The checklist arrives from the core in English, and it has to: those exact
// strings are what the offline bench and 1.x's log are compared through, so
// translating them at the source would leave the differential run comparing two
// different vocabularies. The panel translates on the way to the screen instead.
//
// A label the table does not know is drawn as it came, and said once in the log.
// The alternative - dropping it - would hide a condition that is holding the
// flight up, which is the one thing this list exists to show.
std::string translated(const std::string& text) {
    static const std::map<std::string, const char*> kTable = {
        // conditions
        {"on the ground", "на земле"},
        {"engines off", "двигатели выключены"},
        {"beacon off", "маяк выключен"},
        {"battery or any light on", "батарея или любой свет включён"},
        {"beacon on or engine started", "маяк включён или запущен двигатель"},
        {"engine running", "двигатель работает"},
        {"strobes / landing lights", "стробы или посадочные фары"},
        {"airborne", "в воздухе"},
        {"3000 ft AGL", "3000 фт над землёй"},
        {"above 15 000 ft", "выше 15 000 фт"},
        {"levelling off", "выравнивание"},
        {"held 25 s", "держится 25 с"},
        {"below 11 000 ft", "ниже 11 000 фт"},
        {"descending", "снижается"},
        {"below 3000 ft AGL", "ниже 3000 фт над землёй"},
        {"below 60 kt", "медленнее 60 уз"},
        {"brake set or stopped", "стояночный тормоз или стоит"},
        {"turnaround", "оборот"},
        // Times of day, as the pack's [Night] tags name them.
        {"morning", "утро"},
        {"afternoon", "день"},
        {"evening", "вечер"},
        {"night", "ночь"},
        // The name of the phase being waited for. Stored upper case because its
        // one use is inside a stencilled heading, and upper-casing Cyrillic at
        // run time needs a locale this plugin has no business setting.
        {"Preflight", "ПОДГОТОВКА"},
        {"Boarding", "ПОСАДКА"},
        {"Doors & safety", "ДВЕРИ И БРИФИНГ"},
        {"Takeoff", "ВЗЛЁТ"},
        {"Climb", "НАБОР"},
        {"Cruise", "ЭШЕЛОН"},
        {"Descent", "СНИЖЕНИЕ"},
        {"Approach", "ЗАХОД"},
        {"After landing", "ПОСЛЕ ПОСАДКИ"},
        {"Disembarking", "ВЫСАДКА"},
    };
    const auto found = kTable.find(text);
    if (found != kTable.end()) {
        return found->second;
    }
    static std::set<std::string> reported;
    if (reported.insert(text).second) {
        log("UI: нет перевода для '%s' - показано как есть", text.c_str());
    }
    return text;
}

// The value column carries its units from the core in English too. Only the
// units are touched; light names like nav and taxi stay as they are, because
// that is what is written on the switches.
std::string translatedValue(const std::string& text) {
    static const std::pair<const char*, const char*> kUnits[] = {
        {" fpm", " фт/мин"},
        {" kt", " уз"},
        {" s", " с"},
    };
    std::string out = text;
    if (out.rfind("no battery", 0) == 0) {
        out.replace(0, 10, "нет батареи");
    }
    for (const auto& unit : kUnits) {
        const std::size_t length = std::strlen(unit.first);
        if (out.size() >= length && out.compare(out.size() - length, length, unit.first) == 0) {
            out.replace(out.size() - length, length, unit.second);
            break;
        }
    }
    return out;
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

    // The light the glass is lit by, painted before anything else so every
    // surface above sits on top of it.
    ui::drawAurora(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                   ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                          ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));

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

    // The phase is what this tab is for, so it is the one thing set in the focus
    // size - and the only thing on the tab that gets it. Everything else here
    // supports it.
    if (ImFont* big = XpImguiWindow::focusFont()) {
        ImGui::PushFont(big);
    }
    ImGui::TextUnformatted(phaseTitle(engine.phase()));
    if (XpImguiWindow::focusFont() != nullptr) {
        ImGui::PopFont();
    }
    // The internal name of the phase, stencilled beside it: it is what the log
    // and the bench call this phase, so a question about either can be asked
    // without translating first.
    ImGui::SameLine(0.0f, 10.0f);
    if (ImFont* engraved = XpImguiWindow::engravedFont()) {
        ImGui::PushFont(engraved);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ui::kEngraved, "%s", core::phaseId(engine.phase()));
    if (XpImguiWindow::engravedFont() != nullptr) {
        ImGui::PopFont();
    }

    // Said here as well as on the settings tab: this is the tab someone stares
    // at wondering why nothing is being announced.
    if (!announcer_->settings().flight.enabled) {
        ImGui::TextColored(kAccent, "Объявления выключены — плагин молчит целиком.");
    }

    // What the machine is waiting for. This list is generated by the same code
    // the offline bench cross-checks against the state machine, so it cannot
    // quietly start lying about what is holding the flight up.
    section(("ДАЛЬШЕ — " + translated(engine.nextPhaseLabel(s))).c_str());
    for (const core::Condition& c : engine.phaseConditions(s)) {
        ImGui::Indent(4.0f);
        ui::statusLamp(c.met);
        ImGui::SameLine(0.0f, 10.0f);
        // A satisfied condition is stated in full ink; one still holding the
        // flight up is dimmer, with its lamp unlit. There is no third colour for
        // "waiting" - an unlit lamp beside quieter text already says it, and an
        // amber would have been a second accent competing with the first.
        ImGui::TextColored(c.met ? ui::kInk : kInkMute, "%s", translated(c.label).c_str());
        if (!c.value.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ui::kEngraved, "— %s", translatedValue(c.value).c_str());
        }
        ImGui::Unindent(4.0f);
    }

    section("ЭФИР");
    if (announcer_->player().announcementActive()) {
        ImGui::TextColored(kMet, "%s", announcer_->player().announcementEvent().c_str());
    } else {
        ImGui::TextDisabled("тишина");
    }
    // The queue is shown only when there is one. A counter reading 0 for the
    // whole flight is diagnostics left over from the port, and it invites the
    // exact question "what queue?" from someone who has no queue to think about.
    if (engine.queueSize() > 0) {
        ImGui::Text("В очереди: %zu", engine.queueSize());
    }
    if (engine.musicPlaying()) {
        ImGui::Text("Фон: %s", engine.musicEvent().c_str());
    }

    section("ЧТО ВИДИТ ПЛАГИН");
    ImGui::Text("на земле: %s   двигателей: %d   маяк: %s",
                s.onGround ? "да" : "нет", s.enginesRunning, s.beacon ? "вкл" : "выкл");
    ImGui::Text("высота: %.0f фт   над землёй: %.0f фт   верт.: %.0f фт/мин",
                whole(s.altFt), whole(s.aglFt), whole(s.vsFpm));
    ImGui::Text("скорость: %.0f уз   местный час: %d   %s",
                whole(s.gsKt), s.localHour, s.isDark() ? "темно" : "светло");
    const char* belt = s.seatbelt == core::Tri::Unknown ? "борт его не публикует"
                                                        : (s.seatbelt == core::Tri::On ? "горит" : "погашено");
    ImGui::Text("табло ремней: %s", belt);
    const char* found = announcer_->simState().seatbeltDataref();
    if (found[0] != '\0') {
        ImGui::TextColored(ui::kEngraved, "  %s", found);
    }
    // The dataref field itself now lives in config.ini, but the panel still has
    // to say when what was typed there is not what is being read. Falling back
    // silently looks identical to "my dataref works", and the typo then lives in
    // the file forever.
    const std::string& wanted = announcer_->settings().seatbeltDref;
    if (!wanted.empty() && wanted != found) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kAccent, "  в config.ini указан %s — на этом борту такого датарефа нет",
                           wanted.c_str());
        ImGui::PopTextWrapPos();
    }
}

void MainWindow::drawLibraryTab() {
    FileSoundLibrary& library = announcer_->library();
    core::Settings& settings = announcer_->settings();
    const core::AirlineVerdict& airline = announcer_->airline();
    const std::vector<std::string> packNames = library.packs();

    // The pack is picked from a list rather than a column of radio buttons: with
    // thirty packs on disk the column was most of the tab, and the one line that
    // matters - which pack is playing - was somewhere inside it.
    const std::string airlineName = announcer_->airlines().nameOf(airline.code);
    std::string preview;
    if (settings.autoAirline()) {
        preview = "Авто — " + library.pack();
        if (!airlineName.empty()) {
            preview += "  (" + airlineName + ")";
        }
    } else {
        preview = settings.airlineManual;
    }

    label("Набор звуков");
    if (ImGui::BeginCombo("##pack", preview.c_str())) {
        if (ImGui::Selectable("Авто — по ливрее", settings.autoAirline())) {
            settings.airlineMode = "auto";
            announcer_->resolveAirline();
            announcer_->settingsChanged();
        }
        for (const std::string& packName : packNames) {
            const std::string caption = announcer_->airlines().nameOf(packName).empty()
                                            ? packName
                                            : packName + "  —  " +
                                                  announcer_->airlines().nameOf(packName);
            const bool chosen = !settings.autoAirline() && settings.airlineManual == packName;
            if (ImGui::Selectable(caption.c_str(), chosen)) {
                settings.airlineMode = "manual";
                settings.airlineManual = packName;
                library.selectPack(packName);
                announcer_->settingsChanged();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Пересканировать")) {
        announcer_->rescanLibrary();
    }

    // Recognising the airline and owning its sounds stay two separate lines.
    // Reporting "not detected" when the airline WAS recognised but has no pack
    // is the exact defect this port had to preserve a fix for.
    small(("Ливрея: " + airline.code + (airlineName.empty() ? "" : " — " + airlineName) +
           ", определено по: " + airline.source)
              .c_str());
    if (airline.code != "Default" && library.pack() != airline.code) {
        small(("Пака для " + airline.code + " нет — играет «" + library.pack() + "».").c_str());
    }
    if (!library.packLanguage().empty()) {
        small(("Язык внутри пака: " + library.packLanguage()).c_str());
    }
    const core::PlayContext& context = library.playContext();
    small(("Варианты файлов выбираются под " +
           (context.aircraft.empty() ? std::string("неизвестный борт") : context.aircraft) + ", " +
           translated(context.daypart))
              .c_str());

    section("ОБЪЯВЛЕНИЯ В НАБОРЕ");
    if (packNames.empty()) {
        ImGui::TextColored(kAccent, "В папке нет ни одного набора звуков.");
        return;
    }

    const bool playing = announcer_->player().announcementActive();
    const std::string playingEvent = announcer_->player().announcementEvent();

    // A table, not a list: the question is never "does this event exist" alone
    // but "how many files, from which pack, and which one right now" - four
    // answers that only line up when they are in columns.
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("events", 4, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Объявление", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Файлов", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Откуда", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const FileSoundLibrary::Coverage& row : library.coverage()) {
            const bool available = !row.source.empty();
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(available ? ui::kInk : kInkMute, "%s", row.event.c_str());
            // Which file, and how long it is, only for the row under the
            // cursor. Measuring a file means opening it, and measuring all
            // twenty-nine the moment this tab appears is a stutter in the frame
            // - the one thing a plugin must not cost a pilot on approach.
            if (available && !row.file.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%.0f с", row.file.c_str(), library.duration(row.event));
            }

            ImGui::TableNextColumn();
            const int count = row.own > 0 ? row.own : row.fallback;
            ImGui::TextColored(available ? kInkDim : kInkMute, "%d", count);

            ImGui::TableNextColumn();
            // Green only when the sound is the airline's own; a stand-in from
            // Default is stated, not dressed up as a hit.
            ImGui::TextColored(row.own > 0 ? kMet : kInkMute, "%s",
                               available ? row.source.c_str() : "—");

            ImGui::TableNextColumn();
            ImGui::PushID(row.event.c_str());
            if (playing && playingEvent == row.event) {
                if (ImGui::SmallButton("стоп")) {
                    announcer_->player().stopAnnouncement();
                }
            } else if (available) {
                if (ImGui::SmallButton("играть")) {
                    announcer_->player().playAnnouncement(
                        row.event, library.pathFor(row.event),
                        static_cast<float>(settings.volume));
                    log("UI: ручное воспроизведение %s", row.event.c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
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

        // Neither the ducking amount nor the audio buses are here. Both ask the
        // user to answer a question they do not have - by how much exactly, and
        // through which of X-Plane's mixer buses - and both have a default that
        // is right for a cabin announcement. The keys stay in config.ini.
    }

    section("ЧТО ОБЪЯВЛЯТЬ");
    {
        // Two columns, each switch with the sentence that says what it does.
        // Short labels alone were tried and failed on their own terms: "Свет
        // ночью" reads as though the plugin dims the cabin, which it cannot do -
        // it only announces that the crew is about to.
        const struct {
            const char* title;
            bool* value;
            const char* explanation;
        } switches[] = {
            {"Объявления", &settings.flight.enabled,
             "Главный выключатель. Выключено — плагин молчит целиком."},
            {"Музыка при посадке", &settings.flight.boardingMusic,
             "Фон, пока пассажиры садятся, и после высадки."},
            {"Шум салона", &settings.flight.cabinNoise,
             "Ровный гул салона, только в воздухе."},
            {"Посадка сама", &settings.flight.autoBoarding,
             "Плагин сам решает, что посадка пассажиров началась: борт запитан, "
             "стоит, двигатели и маяк выключены."},
            {"Командир", &settings.flight.pilotWelcome,
             "Приветствие командира следом за приветствием экипажа."},
            {"Двери", &settings.flight.doorCalls,
             "Двери на автомат перед выруливанием и в ручной режим на стоянке."},
            {"Свет ночью", &settings.flight.nightDim,
             "Объявление о приглушении света на ночном взлёте и снижении. "
             "Самим светом плагин не управляет."},
            {"Реакция на посадку", &settings.flight.landingReaction,
             "Салон отзывается на мягкое или жёсткое приземление."},
        };
        bool changed = false;
        if (ImGui::BeginTable("switches", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (const auto& item : switches) {
                ImGui::TableNextColumn();
                switchCell(item.title, item.value, item.explanation, &changed);
            }
            ImGui::EndTable();
        }
        if (changed) {
            announcer_->settingsChanged();
        }
        if (!settings.flight.enabled) {
            ImGui::TextColored(kAccent, "Сейчас плагин молчит целиком.");
        }
        // The repeat interval and the music loop count are gone from here too:
        // one is a number nobody tunes, the other existed in 1.x only to bound a
        // FlyWithLua memory leak that v2 does not have. Both keys still work.
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
        // The language subfolder is not offered here. In a pack with one
        // language it is taken regardless of what the field says, which is most
        // packs - a control that usually changes nothing looks broken when it
        // finally does not. The `language` key still works.
    }

    section("ПРОЧЕЕ");
    {
        // The seatbelt dataref field has moved to config.ini as well. What it
        // was really for - seeing whether the aircraft publishes a sign at all -
        // is on the Рейс tab, which is where someone is already looking.
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
