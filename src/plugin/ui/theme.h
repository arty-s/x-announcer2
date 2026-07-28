// How the panel looks: the Night Deck direction.
//
// A deep blue-black surface lit from behind by a slow aurora, with panels that
// read as frosted glass and exactly one accent - cyan. Green appears in one
// place only, the checklist lamp, and means "satisfied"; nothing else in the
// panel is allowed a colour of its own. The amber "waiting" of the first
// direction is gone: a third colour buys nothing here, because an unlit lamp
// already says "not yet" the way a flight deck says it.
//
// Everything here is what 1.x could not do at all: FlyWithLua exposes neither
// ImGuiStyle nor a second font, which is one of the four reasons v2 exists.
#pragma once

#include "imgui.h"

namespace xa::ui {

// The four levels of text, separated by transparency rather than by four
// different greys - one ink, four strengths, which is what keeps a dark panel
// from looking like four unrelated colours.
extern const ImVec4 kInk;       // values, the thing being read
extern const ImVec4 kInkDim;    // labels naming those values
extern const ImVec4 kInkMute;   // explanations, unlit conditions
extern const ImVec4 kEngraved;  // section lettering, the stencilling

extern const ImVec4 kAccent;  // the one accent: what the user acts on, and notices
extern const ImVec4 kMet;     // a satisfied condition. Status only, never decoration.

// Applies colours and metrics to the current ImGui context.
void applyPanelTheme();

// Paints the aurora behind everything else: three soft pools of light, drawn as
// stacks of concentric circles because ImGui has no blur to give. Call it first
// thing in the frame - it is what the glass surfaces are lit by, and without it
// they are just flat translucent boxes.
void drawAurora(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

// Draws the checklist indicator: a lit lamp when the condition is met, an empty
// outline when it is not. Drawn rather than written, because the font carries no
// tick mark - and a lamp is what a flight deck would use anyway.
void statusLamp(bool met);

// A checkbox drawn by hand, because the two things wrong with ImGui's own are
// both unreachable through the style: its tick takes its weight from the font
// size, and its frame takes its corner radius from the panel's sliders - at 16
// pixels that radius consumes the edge and the remaining stroke looks dashed.
// Returns true on the frame it was toggled.
bool checkBox(const char* label, bool* value);

// A section heading in the panel's own stencilling: small, dim, letter-spaced,
// with a hairline under it.
//
// The tracking is real - each codepoint is placed by hand through the draw list
// with the gap added to the pen position. The obvious shortcut, inserting spaces
// between the letters, cannot work: at a word boundary the word's own space
// merges with the tracking spaces and the gap doubles. `text` is UTF-8 and is
// never split inside a codepoint.
void sectionHeading(const char* text);

}  // namespace xa::ui
