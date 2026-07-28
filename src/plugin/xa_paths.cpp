#include "plugin/xa_paths.h"

#include <cstring>

#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "plugin/xa_log.h"

namespace xa {
namespace {

// X-Plane hands out paths with either separator depending on the platform and
// on whether native paths are enabled, and 1.x lost a release to guessing which
// one it would be. So never guess: cut on whichever comes last.
std::string stripLastComponent(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string discoverPluginDir() {
    char filePath[1024] = {0};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, filePath, nullptr, nullptr);
    if (filePath[0] == '\0') {
        log("paths: XPLMGetPluginInfo returned no path - assets will not load");
        return std::string();
    }

    // .../x_announcer2/win_x64/x_announcer2.xpl -> .../x_announcer2
    const std::string platformDir = stripLastComponent(filePath);
    const std::string root = stripLastComponent(platformDir);
    if (root.empty()) {
        log("paths: cannot walk up from '%s'", filePath);
        return std::string();
    }
    log("paths: plugin dir '%s'", root.c_str());
    return root;
}

}  // namespace

const std::string& pluginDir() {
    static const std::string dir = discoverPluginDir();
    return dir;
}

std::string assetPath(const std::string& relative) {
    const std::string& root = pluginDir();
    if (root.empty()) {
        return relative;
    }
    return root + "/assets/" + relative;
}

}  // namespace xa
