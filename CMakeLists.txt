cmake_minimum_required(VERSION 3.25)

project(SoloRig VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(NAM_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NeuralAmpModelerCore")
file(GLOB NAM_CORE_SOURCES CONFIGURE_DEPENDS
    "${NAM_CORE_ROOT}/NAM/*.cpp"
    "${NAM_CORE_ROOT}/NAM/*.c"
    "${NAM_CORE_ROOT}/NAM/*/*.cpp"
)
add_library(neural_amp_modeler_core STATIC ${NAM_CORE_SOURCES})
target_compile_features(neural_amp_modeler_core PUBLIC cxx_std_20)
target_compile_definitions(neural_amp_modeler_core PUBLIC
    NAM_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_include_directories(neural_amp_modeler_core SYSTEM PUBLIC
    "${NAM_CORE_ROOT}"
    "${NAM_CORE_ROOT}/Dependencies/eigen"
    "${NAM_CORE_ROOT}/Dependencies/nlohmann"
)
if(MSVC)
    # NAM factory registration uses translation-unit static initializers. Keep all
    # Core object files when a host links the static library so every model type,
    # including A2, remains registered at runtime.
    target_link_options(neural_amp_modeler_core INTERFACE
        "/WHOLEARCHIVE:$<TARGET_FILE:neural_amp_modeler_core>"
    )
endif()

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
    src/dsp/diode_clipper.cpp
    src/dsp/digital_delay.cpp
    src/dsp/impulse_response.cpp
    src/dsp/nam_processor.cpp
    src/dsp/tuner.cpp
    src/content/json.cpp
    src/content/module_package.cpp
    src/content/preset.cpp
    src/content/theme.cpp
    src/control/midi_mapping.cpp
    src/control/shortcut_registry.cpp
    src/ui/pedal_layout.cpp
)
target_include_directories(solo_rig_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(solo_rig_core PUBLIC neural_amp_modeler_core)
enable_solo_rig_warnings(solo_rig_core)

if(BUILD_TESTING)
    add_executable(solo_rig_tests
        tests/test_main.cpp
        tests/audio_format_tests.cpp
        tests/audio_buffer_tests.cpp
        tests/effect_descriptor_tests.cpp
        tests/audio_graph_tests.cpp
        tests/realtime_load_meter_tests.cpp
        tests/diode_clipper_tests.cpp
        tests/digital_delay_tests.cpp
        tests/impulse_response_tests.cpp
        tests/nam_processor_tests.cpp
        tests/quality_policy_tests.cpp
        tests/tuner_tests.cpp
        tests/json_tests.cpp
        tests/module_package_tests.cpp
        tests/preset_tests.cpp
        tests/theme_tests.cpp
        tests/midi_mapping_tests.cpp
        tests/shortcut_registry_tests.cpp
        tests/pedal_layout_tests.cpp
    )
    target_link_libraries(solo_rig_tests PRIVATE solo_rig_core)
    target_compile_definitions(solo_rig_tests PRIVATE SOLO_RIG_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    enable_solo_rig_warnings(solo_rig_tests)
    add_test(NAME solo_rig_tests COMMAND solo_rig_tests)
endif()
