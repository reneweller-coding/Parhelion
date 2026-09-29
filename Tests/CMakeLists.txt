# On Android the executables are cross-compiled for the headset; ctest must not run them here.
function(parh_add_test name)
    if(NOT ANDROID)
        add_test(NAME ${name} COMMAND ${ARGN})
    endif()
endfunction()

add_executable(parh_selftest selftest.cpp)
target_link_libraries(parh_selftest PRIVATE ParhelionCore)
# One ctest test per self-test section, `selftest.<name>`, read from the registration table in selftest.cpp
# ({ "testName", testName }) when CMake configures (after Ephemeris).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp PARH_SECTION_LINES REGEX "^    \{ \"test[A-Za-z0-9]+\", test[A-Za-z0-9]+ \},")
foreach(line ${PARH_SECTION_LINES})
    string(REGEX MATCH "\"(test[A-Za-z0-9]+)\"" _ "${line}")
    parh_add_test(selftest.${CMAKE_MATCH_1} parh_selftest --only ${CMAKE_MATCH_1})
endforeach()

# The lane paths against the scalar reference, bit for bit, once per vector path. The main variant is built as the
# core is (AVX2 on x86-64, NEON on the Quest); on x86 the NEON path also runs through neonshim/arm_neon.h, and the
# scalar path is forced. The variants compile what they test themselves: linking ParhelionCore would bring its AVX2 flags.
add_executable(parh_vectest vectest.cpp)
target_link_libraries(parh_vectest PRIVATE ParhelionCore)
parh_add_test(vectest parh_vectest)
set(PARH_TEST_TARGETS parh_selftest parh_vectest)

if(NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|x64)")
    if(PARH_AVX2)
        target_compile_definitions(parh_vectest PRIVATE PARH_EXPECT_PATH="avx2")
    endif()
    foreach(variant neon scalar)
        add_executable(parh_vectest_${variant} vectest.cpp ${CMAKE_SOURCE_DIR}/Core/src/Halfband.cpp
                       ${CMAKE_SOURCE_DIR}/Core/src/synth/Kit.cpp ${CMAKE_SOURCE_DIR}/Core/src/Params.cpp
                       ${CMAKE_SOURCE_DIR}/Core/src/synth/Poly.cpp ${CMAKE_SOURCE_DIR}/Core/src/synth/WaveTable.cpp
                       ${CMAKE_SOURCE_DIR}/Core/src/synth/WaveTableFile.cpp)
        target_include_directories(parh_vectest_${variant} PRIVATE ${CMAKE_SOURCE_DIR}/Core/include)
        target_compile_features(parh_vectest_${variant} PRIVATE cxx_std_20)
        parh_add_test(vectest_${variant} parh_vectest_${variant})
        list(APPEND PARH_TEST_TARGETS parh_vectest_${variant})
    endforeach()
    target_include_directories(parh_vectest_neon BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/neonshim)
    target_compile_definitions(parh_vectest_neon PRIVATE PARH_NEON_SHIM=1 PARH_EXPECT_PATH="neon-shim")
    target_compile_definitions(parh_vectest_scalar PRIVATE PARH_FORCE_SCALAR=1 PARH_EXPECT_PATH="scalar")
elseif(ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    target_compile_definitions(parh_vectest PRIVATE PARH_EXPECT_PATH="neon")
endif()

foreach(t ${PARH_TEST_TARGETS})
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()
