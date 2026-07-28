// Where the plugin lives on disk.
//
// XPLMGetPluginInfo hands back the path of the .xpl itself; everything the
// plugin ships (fonts now, sound packs and config later) sits two levels up,
// next to the platform folder:
//
//     x_announcer2/win_x64/x_announcer2.xpl
//     x_announcer2/assets/fonts/Roboto-Medium.ttf
#pragma once

#include <string>

namespace xa {

// Root of the plugin folder, no trailing separator. Empty if it can't be found.
const std::string& pluginDir();

// pluginDir() + "/assets/" + relative.
std::string assetPath(const std::string& relative);

// The settings file: pluginDir() + "/config.ini". Beside the plugin rather than
// inside assets/, because assets/ is ours to overwrite on every update and this
// file is the user's.
std::string configPath();

}  // namespace xa
