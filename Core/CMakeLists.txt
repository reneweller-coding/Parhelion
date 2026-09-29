add_library(ParhelionCore STATIC
    src/Params.cpp
    src/Clock.cpp
    src/Score.cpp
    src/Midi.cpp
    src/WavWriter.cpp
    src/Halfband.cpp
    src/Loudness.cpp
    src/Leveler.cpp
    src/fx/Dynamics.cpp
    src/fx/Reverb.cpp
    src/fx/Plate.cpp
    src/fx/TapeEcho.cpp
    src/synth/Kick.cpp
    src/synth/SubBass.cpp
    src/synth/Kit.cpp
    src/synth/Synth.cpp
    src/synth/Poly.cpp
    src/synth/Sfx.cpp
    src/synth/SfxBankTables.cpp
    src/synth/WaveTable.cpp
    src/synth/WaveTableFile.cpp
    src/compose/GestureEngine.cpp
    src/compose/Study.cpp
    src/compose/Style.cpp
    src/compose/Planner.cpp
    src/compose/Harmony.cpp
    src/compose/Melody.cpp
    src/compose/Composer.cpp
    src/compose/Corpus.cpp
    src/compose/CorpusTables.cpp
    src/compose/Memo.cpp
    src/compose/MemoFilter.cpp
    src/SetFile.cpp
    src/Deck.cpp
    src/Engine.cpp
    src/Cue.cpp
)
target_include_directories(ParhelionCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
option(PARH_PROFILE "Time the engine's stages (Profile.h); parh_render prints them" OFF)
if(PARH_PROFILE)
    target_compile_definitions(ParhelionCore PUBLIC PARH_PROFILE)
endif()
if(WIN32)
    target_link_libraries(ParhelionCore PUBLIC ws2_32)   # the cue sender's UDP socket (Cue.h)
endif()
target_compile_features(ParhelionCore PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(ParhelionCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(ParhelionCore PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ParhelionCore PRIVATE -Wall -Wextra)
endif()

# AVX2 with FMA for the lane-parallel paths (Vec.h). PUBLIC, so everything that links the core sees the same vector
# path the core was built with. The arm64 build (Quest) takes NEON on its own.
if(PARH_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|x64)")
    if(MSVC)
        target_compile_options(ParhelionCore PUBLIC /arch:AVX2)
    else()
        target_compile_options(ParhelionCore PUBLIC -mavx2 -mfma)
    endif()
endif()
