cmake_minimum_required(VERSION 3.20)
project(x_announcer2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(XA_SDK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDK")
set(XA_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

# --- Dear ImGui (core only, no backends: we render through XPLM ourselves) ----
add_library(imgui STATIC
    "${XA_IMGUI}/imgui.cpp"
    "${XA_IMGUI}/imgui_draw.cpp"
    "${XA_IMGUI}/imgui_tables.cpp"
    "${XA_IMGUI}/imgui_widgets.cpp"
)
target_include_directories(imgui PUBLIC "${XA_IMGUI}")
if(MSVC)
    target_compile_options(imgui PRIVATE /utf-8)
endif()

# --- The plugin --------------------------------------------------------------
add_library(x_announcer2 MODULE
    src/plugin/main.cpp
    src/plugin/xa_log.cpp
    src/plugin/xa_paths.cpp
    src/plugin/imgui_xp/xp_imgui_window.cpp
    src/plugin/imgui_xp/xp_imgui_render.cpp
    src/plugin/ui/main_window.cpp
)

target_include_directories(x_announcer2 PRIVATE
    src
    "${XA_SDK}/CHeaders/XPLM"
    "${XA_SDK}/CHeaders/Widgets"
)

target_link_libraries(x_announcer2 PRIVATE imgui)

# X-Plane 12.04+ (XPLM400 is what XPLMSound lives behind).
target_compile_definitions(x_announcer2 PRIVATE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1
)

if(WIN32)
    target_compile_definitions(x_announcer2 PRIVATE IBM=1 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(x_announcer2 PRIVATE
        "${XA_SDK}/Libraries/Win/XPLM_64.lib"
        opengl32
    )
    # Plugins must use the DLL runtime: a static CRT burns TLS slots and breaks
    # neighbouring plugins (X-Plane SDK "Building and Installing Plugins").
    set_property(TARGET x_announcer2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set(XA_PLATFORM_DIR "win_x64")
elseif(APPLE)
    target_compile_definitions(x_announcer2 PRIVATE APL=1)
    target_link_libraries(x_announcer2 PRIVATE
        "-F${XA_SDK}/Libraries/Mac" "-framework XPLM" "-framework OpenGL"
    )
    set(XA_PLATFORM_DIR "mac_x64")
else()
    target_compile_definitions(x_announcer2 PRIVATE LIN=1)
    target_link_options(x_announcer2 PRIVATE -shared -rdynamic -nodefaultlibs -undefined_warning)
    target_link_libraries(x_announcer2 PRIVATE GL)
    set(XA_PLATFORM_DIR "lin_x64")
endif()

if(MSVC)
    target_compile_options(x_announcer2 PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(x_announcer2 PRIVATE -Wall -Wextra -fvisibility=hidden -fPIC)
endif()

# X-Plane wants  <plugin>/<platform>/<name>.xpl  with no "lib" prefix.
set(XA_OUT "${CMAKE_BINARY_DIR}/x_announcer2")
set_target_properties(x_announcer2 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "x_announcer2"
    SUFFIX ".xpl"
    LIBRARY_OUTPUT_DIRECTORY "${XA_OUT}/${XA_PLATFORM_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${XA_OUT}/${XA_PLATFORM_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${XA_OUT}/${XA_PLATFORM_DIR}"
)

# Assets travel next to the plugin, not inside the binary.
add_custom_command(TARGET x_announcer2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${XA_OUT}/assets"
    COMMENT "Staging assets into ${XA_OUT}/assets"
)
