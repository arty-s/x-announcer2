// How the panel looks: the glass-cockpit direction.
//
// A near-black neutral surface with one cyan accent, which is the whole point of
// the choice - cyan does not compete with the green "done" and amber "waiting"
// the phase checklist runs on, so status colour keeps its full meaning. Amber as
// the accent would have looked more like a lit cockpit and cost exactly that.
//
// Everything here is what 1.x could not do at all: FlyWithLua exposes neither
// ImGuiStyle nor a second font, which is one of the four reasons v2 exists.
#pragma once

#include "imgui.h"

namespace xa::ui {

// Status colours, used by both the phase checklist and the settings tab. Kept
// beside the theme so a future repaint moves them together.
extern const ImVec4 kMet;       // condition satisfied
extern const ImVec4 kWaiting;   // condition still holding the flight up
extern const ImVec4 kEngraved;  // section headings, the lettering on the panel

// Applies colours and metrics to the current ImGui context.
void applyPanelTheme();

// Draws the checklist indicator: a filled lamp when the condition is met, an
// empty outline when it is not. Drawn rather than written, because the font
// carries no tick mark - and a lamp is what a flight deck would use anyway.
void statusLamp(bool met);

// A section heading in the panel's own lettering: small, dim, letter-spaced,
// with a rule under it. `text` is UTF-8; spacing is inserted between codepoints,
// never inside one.
void sectionHeading(const char* text);

}  // namespace xa::ui
