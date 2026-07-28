cmake_minimum_required(VERSION 3.20)
project(x_announcer2 LANGUAGES C CXX)

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

# --- Audio decoding ----------------------------------------------------------
# stb_vorbis is C, and stays C: compiling it as C++ works by accident rather
# than by design. Its warnings are not ours to fix, so they are silenced here
# instead of being left to hide our own.
add_library(xa_decoders STATIC
    third_party/decoders/stb_vorbis.c
    src/audio/decoders_impl.cpp
)
target_include_directories(xa_decoders PUBLIC third_party/decoders)
target_compile_definitions(xa_decoders PUBLIC STB_VORBIS_NO_STDIO=1 STB_VORBIS_NO_PUSHDATA_API=1)
if(MSVC)
    target_compile_options(xa_decoders PRIVATE /W0)
else()
    target_compile_options(xa_decoders PRIVATE -w -fPIC)
endif()

add_library(xa_audio STATIC src/audio/pcm.cpp)
target_include_directories(xa_audio PUBLIC src)
target_link_libraries(xa_audio PUBLIC xa_decoders)
if(MSVC)
    target_compile_options(xa_audio PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(xa_audio PRIVATE -Wall -Wextra -fPIC)
endif()

# --- The core ----------------------------------------------------------------
# Not one line of XPLM, FMOD, filesystem or networking lives in here. That is
# what lets the bench fly complete flights in milliseconds without X-Plane, and
# what lets the same logic be compared against 1.x scenario by scenario.
add_library(xa_core STATIC
    src/core/engine.cpp
    src/core/airline.cpp
)
target_include_directories(xa_core PUBLIC src)
if(MSVC)
    target_compile_options(xa_core PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(xa_core PRIVATE -Wall -Wextra -fPIC)
endif()

# --- The offline bench -------------------------------------------------------
add_executable(xa_test
    tests/main.cpp
    tests/scenario.cpp
    tests/audio_test.cpp
    tests/airline_test.cpp
)
target_link_libraries(xa_test PRIVATE xa_core xa_audio)
target_include_directories(xa_test PRIVATE tests)
target_compile_definitions(xa_test PRIVATE
    XA_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios"
    XA_AIRLINES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/assets/airlines.txt"
    XA_LIVERY_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/liveries.txt"
)
if(MSVC)
    target_compile_options(xa_test PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET xa_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set_property(TARGET xa_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
else()
    target_compile_options(xa_test PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME scenarios COMMAND xa_test)

# --- The plugin --------------------------------------------------------------
add_library(x_announcer2 MODULE
    src/plugin/main.cpp
    src/plugin/announcer.cpp
    src/plugin/sim_state.cpp
    src/plugin/file_library.cpp
    src/plugin/audio_player.cpp
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

target_link_libraries(x_announcer2 PRIVATE imgui xa_core xa_audio)

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
