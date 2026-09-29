# Parhelion -- a generator for trance, tracks and DJ sets (docs/PLAN.md).
#
# Layout:
#   Core/         framework-free engine (static library ParhelionCore)
#   Tools/render  parh_render: offline render, MIDI export, stems
#   Tests/        parh_selftest and the vector-path variants of parh_vectest
#   Plugin/       JUCE VST3 + Standalone (Phase 6)
#   Quest/        native OpenXR app (Phase 7)
#
# Copied from Totality's root CMakeLists.txt at 4d3c0d2 (29.09.2026), which was built after Ephemeris's; the rules are theirs.
cmake_minimum_required(VERSION 3.22)
project(Parhelion VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One source of truth for the version: this project() line.
add_compile_definitions(PARH_VERSION="${PROJECT_VERSION}")

option(PARH_BUILD_TOOLS "Build parh_render and the tests" ON)
# The plugin is a desktop artefact (JUCE cannot cross-compile its helper for Android).
if(ANDROID)
    option(PARH_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone)" OFF)
else()
    option(PARH_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone); from Phase 6" OFF)
endif()
set(PARH_JUCE_TAG "9.0.1" CACHE STRING "JUCE git tag to fetch when no local checkout is found")
option(PARH_AVX2 "Build the x86-64 core with AVX2 + FMA" ON)
option(PARH_STATIC_RUNTIME "Link the MSVC runtime statically (for distribution)" OFF)

if(MSVC)
    add_compile_options(/MP)
endif()
if(PARH_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# No fast-math and no floating-point contraction: the offline render is the determinism oracle and the lane paths
# (Vec.h) are tested bit for bit against the scalar path. Clang contracts a*b+c by default; Intel's icx does even
# under /fp:precise (measured in Phosphene, 23.09.2026).
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    add_compile_options(/fp:precise /Qfma- /clang:-Wno-overriding-option)
endif()
if(ANDROID)
    set(PARH_AVX2 OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(Core)
if(PARH_BUILD_TOOLS)
    enable_testing()
    add_subdirectory(Tools/render)
    add_subdirectory(Tests)
endif()

# The plugin (Phase 5). JUCE from ThirdParty/JUCE, else from the sibling Phosphene's checkout, else fetched. Nothing
# under Core/ includes JUCE.
if(PARH_BUILD_PLUGIN)
    include(FetchContent)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/CMakeLists.txt)
        set(FETCHCONTENT_SOURCE_DIR_JUCE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE CACHE PATH "" FORCE)
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../PsytranceGenerator/ThirdParty/JUCE/CMakeLists.txt)
        set(FETCHCONTENT_SOURCE_DIR_JUCE ${CMAKE_CURRENT_SOURCE_DIR}/../PsytranceGenerator/ThirdParty/JUCE CACHE PATH "" FORCE)
    endif()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${PARH_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(Plugin)
    # The built VST3 loaded the way a DAW loads it (Tests/vst3test.cpp). Here and not in Tests/, because the plugin
    # target has to exist first.
    if(PARH_BUILD_TOOLS)
        add_executable(parh_vst3test Tests/vst3test.cpp)
        target_link_libraries(parh_vst3test PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
        target_compile_definitions(parh_vst3test PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_PLUGINHOST_VST3=1
                                   JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                                   JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
        add_dependencies(parh_vst3test Parhelion_VST3)
        if(MSVC)
            target_compile_options(parh_vst3test PRIVATE /W4 /permissive- /utf-8)
            target_link_options(parh_vst3test PRIVATE "LINKER:/STACK:16777216")
        endif()
        add_test(NAME vst3test COMMAND parh_vst3test "$<TARGET_FILE:Parhelion_VST3>")
        set_tests_properties(vst3test PROPERTIES LABELS "audio;slow" PROCESSORS 3 ENVIRONMENT_MODIFICATION "PARH_MUTE=unset:")
        # Tracktion's pluginval (v1.0.4, unpacked into ThirdParty/pluginval or the sibling Ephemeris's, not in the
        # repository) at its highest strictness, where it is there. Muted: it plays the plugin through its own tests only.
        set(PARH_PLUGINVAL "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/pluginval/pluginval.exe")
        if(NOT EXISTS "${PARH_PLUGINVAL}")
            set(PARH_PLUGINVAL "${CMAKE_CURRENT_SOURCE_DIR}/../BerlinSchoolGenerator/ThirdParty/pluginval/pluginval.exe")
        endif()
        if(EXISTS "${PARH_PLUGINVAL}")
            add_test(NAME pluginval COMMAND "${PARH_PLUGINVAL}" --strictness-level 10 --timeout-ms 900000
                     --validate "$<TARGET_FILE_DIR:Parhelion_VST3>/../..")
            set_tests_properties(pluginval PROPERTIES LABELS "audio;slow" PROCESSORS 3 ENVIRONMENT "PARH_MUTE=1")
        endif()
    endif()
endif()
