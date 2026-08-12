cmake_minimum_required(VERSION 3.25)

project(SoloRig VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

function(enable_solo_rig_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
    )
endfunction()

add_library(solo_rig_core STATIC
    src/core/audio_format.cpp
    src/core/audio_buffer.cpp
    src/core/audio_graph.cpp
    src/core/effect_descriptor.cpp
    src/core/quality_policy.cpp
    src/core/realtime_load_meter.cpp
)
target_include_directories(solo_rig_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
enable_solo_rig_warnings(solo_rig_core)

if(BUILD_TESTING)
    add_executable(solo_rig_tests
        tests/test_main.cpp
        tests/audio_format_tests.cpp
        tests/audio_buffer_tests.cpp
        tests/effect_descriptor_tests.cpp
        tests/audio_graph_tests.cpp
        tests/realtime_load_meter_tests.cpp
        tests/quality_policy_tests.cpp
    )
    target_link_libraries(solo_rig_tests PRIVATE solo_rig_core)
    enable_solo_rig_warnings(solo_rig_tests)
    add_test(NAME solo_rig_tests COMMAND solo_rig_tests)
endif()
