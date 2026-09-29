# Copied from Totality `Plugin/CMakeLists.txt` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
# Parhelion as a VST3 and a standalone (Phase 6). After Ephemeris' Plugin/CMakeLists.txt at d047d79.
juce_add_plugin(Parhelion
    COMPANY_NAME                "Rene Weller"
    BUNDLE_ID                   "com.reneweller.parhelion"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Rnwl
    PLUGIN_CODE                 Parh
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Instrument Synth
    VERSION                     ${PROJECT_VERSION}
    PRODUCT_NAME                "Parhelion"
    ICON_BIG                    "${CMAKE_SOURCE_DIR}/Deploy/parhelion.png")
target_sources(Parhelion PRIVATE PluginProcessor.cpp PluginEditor.cpp EditorTheme.cpp EditorPerform.cpp
                             EditorStyle.cpp UpdateCheck.cpp)
target_compile_definitions(Parhelion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Parhelion
    PRIVATE
        ParhelionCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
