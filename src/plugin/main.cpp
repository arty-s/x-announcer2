// X-Announcer 2 - cabin announcements for X-Plane 12, as a native plugin.
//
// This file is the whole of the plugin's contract with X-Plane. It is kept
// deliberately small and dull: in 1.x a Lua error only stopped the Lua engine,
// while here an uncaught one takes the simulator down with it. So every entry
// point X-Plane can call goes through guarded(), and a callback that keeps
// throwing gets switched off rather than allowed to keep trying.
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include "plugin/announcer.h"
#include "plugin/ui/main_window.h"
#include "plugin/xa_log.h"
#include "plugin/xa_paths.h"

namespace {

constexpr const char* kPluginName = "X-Announcer 2";
constexpr const char* kPluginSig = "ru.xannouncer.xa2";
constexpr const char* kPluginDesc = "Cabin announcements for X-Plane 12";
constexpr const char* kToggleCommand = "x_announcer2/toggle_window";

// After this many consecutive failures a callback is considered broken and is
// left alone; better a dead feature than a crashed simulator.
constexpr int kErrorFuse = 10;

std::unique_ptr<xa::Announcer> g_announcer;
std::unique_ptr<xa::MainWindow> g_window;
XPLMMenuID g_menu = nullptr;
int g_menuToggleIndex = -1;
XPLMCommandRef g_toggleCommand = nullptr;
bool g_fuseBlown = false;
// Set while X-Plane is putting the plugin away. Hiding the window then is our
// doing, not the user's, and must not be written down as "they closed it".
bool g_shuttingDown = false;

// One counter PER call site, never a shared one. 1.x learned this the hard way:
// with a single counter the 30 Hz frame callback zeroed it on every successful
// frame, so a callback that failed once a second could never reach the limit and
// the fuse never blew. The flight loop below runs every frame, so the same trap
// is right here waiting.
struct Fuse {
    const char* what;
    int errors = 0;
};

Fuse g_fuseStart{"start"};
Fuse g_fuseFrame{"flight loop"};
Fuse g_fuseMenu{"menu toggle"};
Fuse g_fuseCommand{"toggle command"};
Fuse g_fuseEnable{"enable"};
Fuse g_fuseMessage{"plane loaded"};

// Runs fn, swallowing anything that escapes it. Returns false if it did not run
// cleanly (or at all, once the fuse is blown).
template <typename Fn>
bool guarded(Fuse& fuse, Fn&& fn) {
    if (g_fuseBlown) {
        return false;
    }
    try {
        fn();
        fuse.errors = 0;
        return true;
    } catch (const std::exception& e) {
        ++fuse.errors;
        xa::log("ERROR in %s: %s (%d in a row)", fuse.what, e.what(), fuse.errors);
    } catch (...) {
        ++fuse.errors;
        xa::log("ERROR in %s: unknown exception (%d in a row)", fuse.what, fuse.errors);
    }
    if (fuse.errors >= kErrorFuse) {
        g_fuseBlown = true;
        xa::log("FUSE BLOWN after %d consecutive errors in %s - the plugin has "
                "stopped doing anything. Restart X-Plane after reporting the lines above.",
                fuse.errors, fuse.what);
    }
    return false;
}

void syncMenuMark() {
    if (g_menu == nullptr || g_menuToggleIndex < 0) {
        return;
    }
    const bool visible = g_window && g_window->isVisible();
    XPLMCheckMenuItem(g_menu, g_menuToggleIndex,
                      visible ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

// Watches what the window actually is, rather than what we last told it to be.
// The close button is the reason: X-Plane hides the window itself and the plugin
// is never told, so a panel closed that way would come back on the next load.
void rememberWindowState() {
    if (!g_window || !g_announcer || g_shuttingDown) {
        return;
    }
    const bool visible = g_window->isVisible();
    if (visible == g_announcer->settings().panelOpen) {
        return;
    }
    g_announcer->settings().panelOpen = visible;
    g_announcer->settingsChanged();
    syncMenuMark();
}

// Every frame. The whole plugin is driven from here: read the sim, let the core
// decide, carry out what it decided. It is wrapped like everything else - an
// exception escaping a flight loop takes X-Plane with it.
float flightLoopCb(float, float, int, void*) {
    guarded(g_fuseFrame, [] {
        if (g_announcer) {
            g_announcer->frame();
        }
        rememberWindowState();
    });
    return -1.0f;  // again next frame
}

void toggleWindow() {
    if (!g_window) {
        g_window = std::make_unique<xa::MainWindow>(g_announcer.get());
    }
    g_window->setVisible(!g_window->isVisible());
    syncMenuMark();
    // The setting itself is updated by rememberWindowState(), which also catches
    // the close button - X-Plane hides a decorated window on its own, without
    // telling the plugin, so anything that only listens to the menu learns about
    // half the closes. 1.x lost a release to exactly this.
}

void menuHandler(void* /*menuRef*/, void* itemRef) {
    if (itemRef == reinterpret_cast<void*>(1)) {
        guarded(g_fuseMenu, [] { toggleWindow(); });
    }
}

int toggleCommandHandler(XPLMCommandRef /*cmd*/, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase == xplm_CommandBegin) {
        guarded(g_fuseCommand, [] { toggleWindow(); });
    }
    return 0;
}

}  // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    // X-Plane guarantees 256 bytes for each of these.
    std::snprintf(outName, 256, "%s", kPluginName);
    std::snprintf(outSig, 256, "%s", kPluginSig);
    std::snprintf(outDesc, 256, "%s", kPluginDesc);

    // Ask for real filesystem paths before anything reads one. Without this the
    // SDK still speaks legacy Mac path syntax on some calls.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);

    int xplaneVersion = 0;
    int xplmVersion = 0;
    XPLMHostApplicationID host = xplm_Host_Unknown;
    XPLMGetVersions(&xplaneVersion, &xplmVersion, &host);
    xa::log("starting - X-Plane %d, XPLM %d", xplaneVersion, xplmVersion);

    if (!guarded(g_fuseStart, [] {
            // 13 px is the body size the scale is built around; the heading and
            // focus sizes are derived from it, not chosen separately.
            xa::XpImguiWindow::loadUiFont(xa::assetPath("fonts/Roboto-Medium.ttf"), 13.0f);

            const int menuIndex =
                XPLMAppendMenuItem(XPLMFindPluginsMenu(), kPluginName, nullptr, 0);
            g_menu = XPLMCreateMenu(kPluginName, XPLMFindPluginsMenu(), menuIndex,
                                    &menuHandler, nullptr);
            g_menuToggleIndex = XPLMAppendMenuItem(g_menu, "Control panel",
                                                   reinterpret_cast<void*>(1), 0);

            g_toggleCommand = XPLMCreateCommand(kToggleCommand, "Toggle the X-Announcer 2 window");
            XPLMRegisterCommandHandler(g_toggleCommand, &toggleCommandHandler, 1, nullptr);

            g_announcer = std::make_unique<xa::Announcer>();
            g_announcer->start();
            XPLMRegisterFlightLoopCallback(&flightLoopCb, -1.0f, nullptr);
        })) {
        xa::log("start FAILED - the plugin will load but do nothing");
    }

    xa::log("started");
    return 1;
}

PLUGIN_API void XPluginStop(void) {
    XPLMUnregisterFlightLoopCallback(&flightLoopCb, nullptr);
    g_announcer.reset();
    if (g_toggleCommand != nullptr) {
        XPLMUnregisterCommandHandler(g_toggleCommand, &toggleCommandHandler, 1, nullptr);
        g_toggleCommand = nullptr;
    }
    g_window.reset();
    if (g_menu != nullptr) {
        XPLMDestroyMenu(g_menu);
        g_menu = nullptr;
    }
    xa::log("stopped");
}

PLUGIN_API int XPluginEnable(void) {
    g_shuttingDown = false;
    guarded(g_fuseEnable, [] {
        if (!g_window) {
            g_window = std::make_unique<xa::MainWindow>(g_announcer.get());
        }
        // Only if that is how the user left it. The plugin announces itself in
        // Log.txt and in the Plugins menu; it has no business taking over the
        // screen on every load.
        const bool wanted = g_announcer && g_announcer->settings().panelOpen;
        g_window->setVisible(wanted);
        syncMenuMark();
    });
    xa::log("enabled");
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    // Hiding the window here is X-Plane putting the plugin away, not the user
    // closing the panel; without this flag the setting would be overwritten with
    // "closed" on every shutdown.
    g_shuttingDown = true;
    if (g_window) {
        g_window->setVisible(false);
    }
    syncMenuMark();
    xa::log("disabled");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*from*/, int msg, void* param) {
    switch (msg) {
        case XPLM_MSG_PLANE_LOADED: {
            // The parameter is the aircraft index, bit-cast into the pointer;
            // only 0 is the user's. Without this check every AI aeroplane the
            // sim spawns would restart the user's flight.
            const int index = static_cast<int>(reinterpret_cast<std::intptr_t>(param));
            xa::log("message: plane %d loaded", index);
            if (index != 0) {
                break;
            }
            guarded(g_fuseMessage, [] {
                if (g_announcer) {
                    g_announcer->onAircraftLoaded();
                }
            });
            break;
        }
        case XPLM_MSG_AIRPORT_LOADED:
            // "Positioned at a new airport" - the map teleport, or picking a new
            // start. 1.x got its reset here for free, because this restarts the
            // Lua engine.
            xa::log("message: airport loaded");
            guarded(g_fuseMessage, [] {
                if (g_announcer) {
                    g_announcer->onRelocated();
                }
            });
            break;
        case XPLM_MSG_LIVERY_LOADED:
            xa::log("message: livery loaded");
            break;
        case XPLM_MSG_SCENERY_LOADED:
            xa::log("message: scenery loaded");
            break;
        default:
            break;
    }
}
