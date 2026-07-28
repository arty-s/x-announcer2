// Rendering ImGui draw data inside an X-Plane window.
//
// X-Plane 12 renders with Vulkan/Metal, but plugin drawing is still OpenGL and
// only OpenGL ("Plugin compatibility guide for X-Plane 11.50"). Two consequences
// shape this file:
//
//   * We stay on fixed-function GL 1.1 client arrays. Everything used here is
//     exported by opengl32.dll on Windows, so the plugin needs no GL loader and
//     no extra dependency.
//   * GL state that X-Plane caches (blending, texturing, depth) must be changed
//     through XPLMSetGraphicsState, and textures bound through XPLMBindTexture2d,
//     or X-Plane's idea of the state drifts from reality.
#pragma once

#include "imgui.h"

namespace xa {

// Draws ImGui output into the X-Plane window whose top-left corner is at
// (windowLeft, windowTop) in global boxel coordinates.
void renderImGuiDrawData(ImDrawData* drawData, int windowLeft, int windowTop);

}  // namespace xa
