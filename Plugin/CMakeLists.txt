# Copied from Totality `Plugin/CMakeLists.txt` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
# Parhelion as a VST3 and a standalone (Phase 6). After Ephemeris' Plugin/CMakeLists.txt at d047d79.
juce_add_plugin(Parhelion
    COMPANY_NAME                "Rene Weller"
    BUNDLE_ID                   "com.reneweller.parhelion"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE    # the composer's notes as they play (02.10.2026; PluginProcessor::emitMidi)
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Rnwl
    PLUGIN_CODE                 Parh
    FORMATS                     ${FAMILY_PLUGIN_FORMATS}   # VST3, Standalone; AU on macOS, LV2 on Linux (Family.cmake)
    LV2URI                      "https://github.com/reneweller-coding/Parhelion"
    NEEDS_CURL                  TRUE
    VST3_CATEGORIES             Instrument Synth
    VERSION                     ${PROJECT_VERSION}
    PRODUCT_NAME                "Parhelion"
    ICON_BIG                    "${CMAKE_SOURCE_DIR}/Deploy/parhelion.png")
target_sources(Parhelion PRIVATE PluginProcessor.cpp PluginEditor.cpp EditorTheme.cpp EditorPerform.cpp
                             EditorStyle.cpp UpdateCheck.cpp Frame.cpp LinkClock.cpp)
# The frame's pieces (01.10.2026): the picture behind the panel and the manual's prose for the help (F1).
juce_add_binary_data(ParhelionData
    HEADER_NAME ParhelionData.h
    NAMESPACE ParhelionData
    SOURCES Resources/backdrop.jpg ${CMAKE_SOURCE_DIR}/Tools/manual/chapters.txt)
target_compile_definitions(Parhelion PUBLIC
    JUCE_WEB_BROWSER=0
    # The update check fetches GitHub over https, which JUCE does on Linux only through libcurl (02.10.2026): there
    # NEEDS_CURL sets JUCE_USE_CURL itself, and the library is loaded when the check runs, so a machine without it
    # only goes without the check. Windows and macOS have their own https and keep curl out.
    $<$<NOT:$<PLATFORM_ID:Linux>>:JUCE_USE_CURL=0>
    JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Parhelion
    PRIVATE
        ParhelionCore
        ParhelionData
        juce::juce_audio_utils
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Into bin/<preset> after every build (cmake/Family.cmake, 01.10.2026): the standalone and the VST3 bundle.
family_bin(Parhelion_Standalone)
family_bin(Parhelion_VST3 VST3)

family_link(Parhelion)   # Ableton Link in the standalone (cmake/Family.cmake, 02.10.2026)
