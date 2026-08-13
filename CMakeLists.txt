cmake_minimum_required(VERSION 3.25)

project(SoloRig VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(SOLO_RIG_BUILD_VST3 "Build the SoloRig VST3 plug-in" ON)
option(SOLO_RIG_BUILD_STANDALONE "Build the SoloRig Windows standalone editor" ON)

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
    src/core/rig_runtime.cpp
    src/dsp/diode_clipper.cpp
    src/dsp/mna_solver.cpp
    src/dsp/digital_delay.cpp
    src/dsp/impulse_response.cpp
    src/dsp/nam_processor.cpp
    src/dsp/tuner.cpp
    src/content/json.cpp
    src/content/circuit_v2.cpp
    src/content/whitebox_compiler.cpp
    src/content/module_package.cpp
    src/content/module_runtime.cpp
    src/content/preset.cpp
    src/content/theme.cpp
    src/control/midi_mapping.cpp
    src/control/preset_controller.cpp
    src/control/shortcut_registry.cpp
    src/standalone/audio_routing.cpp
    src/ui/pedal_layout.cpp
    src/ui/rig_editor.cpp
)
if(WIN32)
    target_sources(solo_rig_core PRIVATE src/control/midi_input.cpp)
    target_link_libraries(solo_rig_core PUBLIC winmm)
endif()
target_include_directories(solo_rig_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(solo_rig_core PUBLIC neural_amp_modeler_core)
enable_solo_rig_warnings(solo_rig_core)

if(WIN32)
    add_library(solo_rig_backend SHARED
        src/interop/solorig_backend.cpp
    )
    target_include_directories(solo_rig_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(solo_rig_backend PRIVATE SOLO_RIG_BACKEND_BUILD UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(solo_rig_backend PRIVATE solo_rig_core)
    set_target_properties(solo_rig_backend PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        OUTPUT_NAME "SoloRigBackend"
    )
    enable_solo_rig_warnings(solo_rig_backend)
endif()

if(WIN32 AND SOLO_RIG_BUILD_STANDALONE)
    add_executable(SoloRigStandalone WIN32
        src/standalone/main.cpp
        src/standalone/windows_rig_window.cpp
        src/standalone/midi_control_session.cpp
    )
    target_include_directories(SoloRigStandalone PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(SoloRigStandalone PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SoloRigStandalone PRIVATE solo_rig_core d2d1 dwrite windowscodecs user32 gdi32 comdlg32)
    set_target_properties(SoloRigStandalone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        OUTPUT_NAME "SoloRigStandalone"
    )
    add_custom_command(TARGET SoloRigStandalone POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:SoloRigStandalone>/resources"
    )

    if(BUILD_TESTING)
        add_executable(solo_rig_windows_ui_tests
            tests/test_main.cpp
            tests/windows_ui_smoke_tests.cpp
            src/standalone/windows_rig_window.cpp
            src/standalone/midi_control_session.cpp
        )
        target_include_directories(solo_rig_windows_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(solo_rig_windows_ui_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(solo_rig_windows_ui_tests PRIVATE solo_rig_core d2d1 dwrite windowscodecs user32 gdi32 comdlg32)
        add_test(NAME solo_rig_windows_ui_tests COMMAND solo_rig_windows_ui_tests)
    endif()
endif()

if(SOLO_RIG_BUILD_VST3)
    set(SOLO_RIG_VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst3sdk")
    if(NOT EXISTS "${SOLO_RIG_VST3_SDK_ROOT}/public.sdk/source/vst/vstaudioeffect.cpp")
        message(FATAL_ERROR "The vendored VST3 SDK is incomplete. Initialize third_party/vst3sdk first.")
    endif()

    # Build a self-contained VST3 package inside the build tree. Deployment is an
    # explicit user action; CMake must never alter the system plug-in folders.
    list(APPEND CMAKE_MODULE_PATH "${SOLO_RIG_VST3_SDK_ROOT}/cmake/modules")
    set(SDK_ROOT "${SOLO_RIG_VST3_SDK_ROOT}")
    set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Create a link in the system VST3 folder" FORCE)
    set(SMTG_CREATE_BUNDLE_FOR_WINDOWS ON CACHE BOOL "Create Windows VST3 package folders" FORCE)
    set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "Create VST3 moduleinfo.json" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "Run VST3 validator after every plug-in build" FORCE)

    include(SMTG_VST3_SDK)
    smtg_setup_symbol_visibility()
    smtg_create_lib_base_target()
    smtg_create_pluginterfaces_target()
    smtg_create_public_sdk_common_target()
    smtg_create_public_sdk_target()
    smtg_create_public_sdk_hosting_target()

    # The official validator remains an ordinary CTest case so failures are
    # visible with the rest of SoloRig's automated test suite.
    add_subdirectory(
        "${SDK_ROOT}/public.sdk/samples/vst-hosting/validator"
        "${CMAKE_CURRENT_BINARY_DIR}/vst3_validator"
    )
    # SDK 3.8.1's validator uses filesystem::u8string() as std::string on
    # Windows. Compile that unmodified third-party validator as C++17 while
    # SoloRig itself remains C++20.
    set_target_properties(validator PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    smtg_add_vst3plugin(solo_rig_vst3
        PACKAGE_NAME "SoloRig"
        SOURCES_LIST
            src/vst3/solo_rig_processor.cpp
            src/vst3/solo_rig_controller.cpp
            src/vst3/factory.cpp
    )
    target_include_directories(solo_rig_vst3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(solo_rig_vst3 PRIVATE sdk solo_rig_core)
    # VST3's public SDK headers are compiled into this boundary target. Keep
    # SoloRig's stricter /W4 /WX policy on its own core and tests; applying it
    # here would turn warnings in unmodified SDK templates into local failures.

    if(BUILD_TESTING)
        add_executable(solo_rig_vst3_tests
            tests/test_main.cpp
            tests/vst3_processor_tests.cpp
            src/vst3/solo_rig_processor.cpp
            "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
        )
        target_include_directories(solo_rig_vst3_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${SDK_ROOT}"
            "${SDK_ROOT}/public.sdk"
        )
        target_link_libraries(solo_rig_vst3_tests PRIVATE sdk sdk_hosting solo_rig_core)
        add_test(NAME solo_rig_vst3_tests COMMAND solo_rig_vst3_tests)
        add_test(
            NAME solo_rig_vst3_validator
            COMMAND $<TARGET_FILE:validator>
                "${CMAKE_CURRENT_BINARY_DIR}/VST3/$<CONFIG>/SoloRig.vst3"
        )
    endif()
endif()

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
        tests/circuit_v2_tests.cpp
        tests/whitebox_contract_tests.cpp
        tests/whitebox_compiler_tests.cpp
        tests/module_package_tests.cpp
        tests/module_runtime_tests.cpp
        tests/rig_runtime_tests.cpp
        tests/preset_tests.cpp
        tests/preset_controller_tests.cpp
        tests/theme_tests.cpp
        tests/midi_mapping_tests.cpp
        tests/shortcut_registry_tests.cpp
        tests/audio_routing_tests.cpp
        tests/pedal_layout_tests.cpp
        tests/rig_editor_tests.cpp
        tests/midi_control_session_tests.cpp
    )
    target_link_libraries(solo_rig_tests PRIVATE solo_rig_core)
    if(WIN32)
        target_sources(solo_rig_tests PRIVATE src/standalone/midi_control_session.cpp)
    endif()
    target_compile_definitions(solo_rig_tests PRIVATE SOLO_RIG_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    enable_solo_rig_warnings(solo_rig_tests)
    add_test(NAME solo_rig_tests COMMAND solo_rig_tests)

    if(WIN32)
        add_executable(solo_rig_backend_abi_tests
            tests/test_main.cpp
            tests/backend_abi_tests.cpp
        )
        target_include_directories(solo_rig_backend_abi_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(solo_rig_backend_abi_tests PRIVATE solo_rig_backend)
        target_compile_definitions(solo_rig_backend_abi_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        enable_solo_rig_warnings(solo_rig_backend_abi_tests)
        add_test(NAME solo_rig_backend_abi_tests COMMAND solo_rig_backend_abi_tests)
    endif()
endif()
