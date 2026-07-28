#include "plugin/imgui_xp/xp_imgui_render.h"

#include <vector>

#if IBM
#include <windows.h>
#endif
#include <GL/gl.h>

#include "XPLMGraphics.h"

#include "plugin/xa_log.h"

namespace xa {
namespace {

// Scissor rectangles have to be given to GL in physical pixels, while ImGui
// works in the window's boxels. Rather than assume a scale factor (it is wrong
// on HiDPI and wrong again on multi-monitor), project the point through the
// matrices X-Plane has actually set for this frame.
struct GlTransform {
    GLfloat modelview[16];
    GLfloat projection[16];
    GLint viewport[4];
};

GlTransform captureTransform() {
    GlTransform t{};
    glGetFloatv(GL_MODELVIEW_MATRIX, t.modelview);
    glGetFloatv(GL_PROJECTION_MATRIX, t.projection);
    glGetIntegerv(GL_VIEWPORT, t.viewport);
    return t;
}

void boxelToPixel(const GlTransform& t, float bx, float by, float* px, float* py) {
    const float in[4] = {bx, by, 0.0f, 1.0f};
    float eye[4];
    float clip[4];
    for (int i = 0; i < 4; ++i) {
        eye[i] = t.modelview[i] * in[0] + t.modelview[4 + i] * in[1] +
                 t.modelview[8 + i] * in[2] + t.modelview[12 + i] * in[3];
    }
    for (int i = 0; i < 4; ++i) {
        clip[i] = t.projection[i] * eye[0] + t.projection[4 + i] * eye[1] +
                  t.projection[8 + i] * eye[2] + t.projection[12 + i] * eye[3];
    }
    if (clip[3] == 0.0f) {
        *px = 0.0f;
        *py = 0.0f;
        return;
    }
    *px = static_cast<float>(t.viewport[0]) +
          static_cast<float>(t.viewport[2]) * (clip[0] / clip[3] * 0.5f + 0.5f);
    *py = static_cast<float>(t.viewport[1]) +
          static_cast<float>(t.viewport[3]) * (clip[1] / clip[3] * 0.5f + 0.5f);
}

// A wrong scissor rectangle looks exactly like "the window is broken", so the
// first frame states on the record what it computed. One line, once per run.
bool g_loggedGeometry = false;

}  // namespace

void renderImGuiDrawData(ImDrawData* drawData, int windowLeft, int windowTop) {
    if (drawData == nullptr || drawData->CmdListsCount == 0) {
        return;
    }

    // fog off, 1 texture unit, no lighting, alpha test on, blending on,
    // no depth test, no depth write.
    XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0);

    const GlTransform xform = captureTransform();
    if (!g_loggedGeometry) {
        g_loggedGeometry = true;
        float px = 0.0f;
        float py = 0.0f;
        boxelToPixel(xform, static_cast<float>(windowLeft), static_cast<float>(windowTop), &px, &py);
        log("render: viewport %d,%d %dx%d; window top-left boxel %d,%d -> pixel %.1f,%.1f",
            xform.viewport[0], xform.viewport[1], xform.viewport[2], xform.viewport[3],
            windowLeft, windowTop, px, py);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnable(GL_SCISSOR_TEST);

    std::vector<ImDrawVert> verts;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* list = drawData->CmdLists[listIndex];

        // ImGui works top-left-down in window-local coordinates; X-Plane works
        // bottom-left-up in global boxels. Translate once per list.
        verts.assign(list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
        for (ImDrawVert& v : verts) {
            v.pos.x += static_cast<float>(windowLeft);
            v.pos.y = static_cast<float>(windowTop) - v.pos.y;
        }

        for (int cmdIndex = 0; cmdIndex < list->CmdBuffer.Size; ++cmdIndex) {
            const ImDrawCmd& cmd = list->CmdBuffer[cmdIndex];
            if (cmd.UserCallback != nullptr) {
                cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) {
                continue;
            }

            const ImDrawVert* base = verts.data() + cmd.VtxOffset;
            glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), &base->pos);
            glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), &base->uv);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), &base->col);

            float leftPx = 0.0f;
            float topPx = 0.0f;
            float rightPx = 0.0f;
            float bottomPx = 0.0f;
            boxelToPixel(xform,
                         static_cast<float>(windowLeft) + cmd.ClipRect.x,
                         static_cast<float>(windowTop) - cmd.ClipRect.y,
                         &leftPx, &topPx);
            boxelToPixel(xform,
                         static_cast<float>(windowLeft) + cmd.ClipRect.z,
                         static_cast<float>(windowTop) - cmd.ClipRect.w,
                         &rightPx, &bottomPx);

            const int scissorX = static_cast<int>(leftPx);
            const int scissorY = static_cast<int>(bottomPx);
            const int scissorW = static_cast<int>(rightPx - leftPx);
            const int scissorH = static_cast<int>(topPx - bottomPx);
            if (scissorW <= 0 || scissorH <= 0) {
                continue;
            }
            glScissor(scissorX, scissorY, scissorW, scissorH);

            // ImTextureID is an integer handle in this ImGui version; we store
            // the GL texture name in it directly.
            XPLMBindTexture2d(static_cast<int>(static_cast<GLuint>(cmd.GetTexID())), 0);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(cmd.ElemCount),
                           sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                           list->IdxBuffer.Data + cmd.IdxOffset);
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

}  // namespace xa
