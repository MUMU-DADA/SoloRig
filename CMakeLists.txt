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
    src/core/named_audio_graph.cpp
    src/core/effect_descriptor.cpp
    src/core/quality_policy.cpp
    src/core/realtime_load_meter.cpp
    src/core/rig_runtime.cpp
    src/dsp/diode_clipper.cpp
    src/dsp/dynamic_power_supply.cpp
    src/dsp/dynamic_speaker_load.cpp
    src/dsp/dynamic_relay.cpp
    src/dsp/dynamic_relay_routing.cpp
    src/dsp/generic_bbd.cpp
    src/dsp/generic_bbd_clocked.cpp
    src/dsp/generic_bbd_feedback.cpp
    src/dsp/generic_vco.cpp
    src/dsp/generic_vca.cpp
    src/dsp/generic_envelope.cpp
    src/dsp/generic_lfo.cpp
    src/dsp/generic_optocoupler.cpp
    src/dsp/generic_optocoupler_mna.cpp
    src/dsp/generic_spring.cpp
    src/dsp/coupled_inductor.cpp
    src/dsp/nonlinear_transformer_core.cpp
    src/dsp/mna_solver.cpp
    src/dsp/nonlinear_amplifier.cpp
    src/dsp/nonlinear_opamp_dynamic.cpp
    src/dsp/nonlinear_beam_tetrode.cpp
    src/dsp/nonlinear_bjt.cpp
    src/dsp/nonlinear_bjt_dynamic.cpp
    src/dsp/nonlinear_diode.cpp
    src/dsp/nonlinear_diode_dynamic.cpp
    src/dsp/nonlinear_jfet.cpp
    src/dsp/nonlinear_jfet_dynamic.cpp
    src/dsp/nonlinear_mosfet.cpp
    src/dsp/nonlinear_mosfet_dynamic.cpp
    src/dsp/nonlinear_pentode.cpp
    src/dsp/nonlinear_triode.cpp
    src/dsp/nonlinear_vari_mu.cpp
    src/dsp/digital_delay.cpp
    src/dsp/digital_filter.cpp
    src/dsp/digital_dynamics.cpp
    src/dsp/digital_pitch_shift.cpp
    src/dsp/impulse_response.cpp
    src/dsp/nam_processor.cpp
    src/dsp/tuner.cpp
    src/content/json.cpp
    src/content/circuit_v2.cpp
    src/content/whitebox_compiler.cpp
    src/content/whitebox_subcircuit.cpp
    src/content/whitebox_runtime.cpp
    src/content/model_database.cpp
    src/content/nonlinear_diode_binding.cpp
    src/content/nonlinear_bjt_binding.cpp
    src/content/nonlinear_jfet_binding.cpp
    src/content/nonlinear_mosfet_binding.cpp
    src/content/nonlinear_opamp_binding.cpp
    src/content/dynamic_speaker_load_binding.cpp
    src/content/topology_decomposition.cpp
    src/content/behavior_lowering.cpp
    src/content/behavior_graph.cpp
    src/content/behavior_graph_json.cpp
    src/content/whitebox_mna_json.cpp
    src/content/behavior_clocked.cpp
    src/content/behavior_clocked_runtime.cpp
    src/content/behavior_mna_modulation.cpp
    src/content/behavior_mna_composite.cpp
    src/content/behavior_feedback.cpp
    src/content/behavior_feedback_runtime.cpp
    src/content/behavior_runtime.cpp
    src/content/behavior_ports.cpp
    src/content/behavior_typed_runtime.cpp
    src/content/behavior_audio_processor.cpp
    src/content/behavior_envelope_runtime.cpp
    src/content/behavior_optocoupler_runtime.cpp
    src/content/behavior_composite.cpp
    src/content/behavior_composite_runtime.cpp
    src/content/generic_tube_amp_compile.cpp
    src/content/generic_tube_amp_mna_lowering.cpp
    src/content/generic_tube_amp_mna_runtime.cpp
    src/content/generic_bbd_binding.cpp
    src/content/generic_vco_binding.cpp
    src/content/generic_vca_binding.cpp
    src/content/generic_envelope_binding.cpp
    src/content/generic_lfo_binding.cpp
    src/content/generic_optocoupler_binding.cpp
    src/content/generic_spring_binding.cpp
    src/content/evidence_chain.cpp
    src/content/wdf_runtime.cpp
    src/content/module_package.cpp
    src/content/module_runtime.cpp
    src/content/preset.cpp
    src/content/theme.cpp
    src/control/midi_mapping.cpp
    src/control/preset_controller.cpp
    src/control/shortcut_registry.cpp
    src/standalone/audio_routing.cpp
    src/standalone/audio_resampler.cpp
    src/ui/pedal_layout.cpp
    src/ui/rig_editor.cpp
)
if(WIN32)
    target_sources(solo_rig_core PRIVATE
        src/control/midi_input.cpp
        src/standalone/asio_host.cpp
        src/standalone/asio_sample_conversion.cpp
    )
    target_link_libraries(solo_rig_core PUBLIC winmm ole32 advapi32)
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
        src/standalone/standalone_audio_engine.cpp
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

    add_executable(solo_rig_asio_probe
        tools/asio_probe.cpp
        src/standalone/standalone_audio_engine.cpp
    )
    target_include_directories(solo_rig_asio_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(solo_rig_asio_probe PRIVATE solo_rig_core user32)
    set_target_properties(solo_rig_asio_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        OUTPUT_NAME "SoloRigAsioProbe"
    )
    enable_solo_rig_warnings(solo_rig_asio_probe)

    if(BUILD_TESTING)
        add_executable(solo_rig_windows_ui_tests
            tests/test_main.cpp
            tests/windows_ui_smoke_tests.cpp
            src/standalone/windows_rig_window.cpp
            src/standalone/midi_control_session.cpp
            src/standalone/standalone_audio_engine.cpp
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
        tests/named_audio_graph_tests.cpp
        tests/realtime_load_meter_tests.cpp
        tests/realtime_contract_tests.cpp
        tests/wdf_runtime_tests.cpp
        tests/diode_clipper_tests.cpp
        tests/dynamic_power_supply_tests.cpp
        tests/dynamic_power_supply_mna_tests.cpp
        tests/dynamic_power_supply_runtime_tests.cpp
        tests/dynamic_speaker_load_tests.cpp
        tests/dynamic_speaker_load_mna_tests.cpp
        tests/dynamic_speaker_load_runtime_tests.cpp
        tests/dynamic_speaker_load_binding_tests.cpp
        tests/dynamic_relay_tests.cpp
        tests/dynamic_relay_mna_tests.cpp
        tests/dynamic_relay_runtime_tests.cpp
        tests/dynamic_relay_routing_tests.cpp
        tests/dynamic_relay_routing_mna_tests.cpp
        tests/dynamic_relay_routing_runtime_tests.cpp
        tests/generic_bbd_tests.cpp
        tests/generic_bbd_binding_tests.cpp
        tests/generic_bbd_runtime_tests.cpp
        tests/generic_bbd_clocked_tests.cpp
        tests/generic_bbd_feedback_tests.cpp
        tests/generic_vco_tests.cpp
        tests/generic_vco_binding_tests.cpp
        tests/generic_vco_runtime_tests.cpp
        tests/generic_vca_tests.cpp
        tests/generic_vca_binding_tests.cpp
        tests/generic_vca_runtime_tests.cpp
        tests/generic_envelope_tests.cpp
        tests/generic_envelope_binding_tests.cpp
        tests/generic_envelope_runtime_tests.cpp
        tests/generic_lfo_tests.cpp
        tests/generic_lfo_binding_tests.cpp
        tests/generic_lfo_runtime_tests.cpp
        tests/generic_optocoupler_tests.cpp
        tests/generic_optocoupler_binding_tests.cpp
        tests/generic_optocoupler_runtime_tests.cpp
        tests/generic_optocoupler_mna_tests.cpp
        tests/optocoupler_circuit_tests.cpp
        tests/generic_spring_tests.cpp
        tests/generic_spring_binding_tests.cpp
        tests/generic_spring_runtime_tests.cpp
        tests/behavior_graph_tests.cpp
        tests/behavior_graph_json_tests.cpp
        tests/whitebox_mna_json_tests.cpp
        tests/behavior_clocked_tests.cpp
        tests/behavior_mna_modulation_tests.cpp
        tests/behavior_mna_composite_tests.cpp
        tests/behavior_feedback_tests.cpp
        tests/behavior_runtime_tests.cpp
        tests/behavior_ports_tests.cpp
        tests/behavior_typed_runtime_tests.cpp
        tests/behavior_envelope_runtime_tests.cpp
        tests/behavior_optocoupler_runtime_tests.cpp
        tests/behavior_composite_tests.cpp
        tests/behavior_composite_runtime_tests.cpp
        fixtures/whitebox/m19a1_behavior_chain_fixtures.cpp
        fixtures/whitebox/m19a2_feedback_fixtures.cpp
        fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.cpp
        fixtures/whitebox/m19c_representative_pedal_fixtures.cpp
        tests/m19a1_validation_harness.cpp
        tests/m19a1_behavior_validation_tests.cpp
        tests/m19a2_feedback_validation_tests.cpp
        tests/m19c_validation_harness.cpp
        tests/m19c_representative_validation_tests.cpp
        tests/generic_tube_amp_compile_tests.cpp
        tests/generic_tube_amp_mna_lowering_tests.cpp
        tests/coupled_inductor_tests.cpp
        tests/nonlinear_transformer_core_tests.cpp
        tests/nonlinear_amplifier_tests.cpp
        tests/nonlinear_opamp_dynamic_tests.cpp
        tests/nonlinear_opamp_dynamic_mna_tests.cpp
        tests/nonlinear_opamp_dynamic_runtime_tests.cpp
        tests/nonlinear_opamp_dynamic_binding_tests.cpp
        tests/nonlinear_beam_tetrode_tests.cpp
        tests/nonlinear_bjt_tests.cpp
        tests/nonlinear_bjt_dynamic_tests.cpp
        tests/nonlinear_bjt_dynamic_mna_tests.cpp
        tests/nonlinear_bjt_dynamic_runtime_tests.cpp
        tests/nonlinear_jfet_dynamic_tests.cpp
        tests/nonlinear_jfet_dynamic_mna_tests.cpp
        tests/nonlinear_jfet_dynamic_runtime_tests.cpp
        tests/nonlinear_diode_tests.cpp
        tests/nonlinear_diode_dynamic_tests.cpp
        tests/nonlinear_diode_dynamic_mna_tests.cpp
        tests/nonlinear_diode_dynamic_runtime_tests.cpp
        tests/nonlinear_jfet_tests.cpp
        tests/nonlinear_mosfet_tests.cpp
        tests/nonlinear_mosfet_dynamic_tests.cpp
        tests/nonlinear_mosfet_dynamic_mna_tests.cpp
        tests/nonlinear_mosfet_dynamic_runtime_tests.cpp
        tests/nonlinear_mosfet_dynamic_binding_tests.cpp
        tests/nonlinear_pentode_tests.cpp
        tests/nonlinear_triode_tests.cpp
        tests/nonlinear_vari_mu_tests.cpp
        tests/digital_delay_tests.cpp
        tests/digital_filter_tests.cpp
        tests/digital_dynamics_tests.cpp
        tests/digital_pitch_shift_tests.cpp
        tests/impulse_response_tests.cpp
        tests/nam_processor_tests.cpp
        tests/quality_policy_tests.cpp
        tests/tuner_tests.cpp
        tests/json_tests.cpp
        tests/circuit_v2_tests.cpp
        tests/circuit_parameter_binding_parser_tests.cpp
        tests/circuit_parameter_binding_mna_tests.cpp
        tests/circuit_parameter_binding_runtime_tests.cpp
        tests/whitebox_contract_tests.cpp
        tests/whitebox_compiler_tests.cpp
        tests/whitebox_subcircuit_parser_tests.cpp
        tests/whitebox_subcircuit_flattening_tests.cpp
        tests/whitebox_subcircuit_runtime_tests.cpp
        tests/whitebox_runtime_tests.cpp
        tests/model_database_tests.cpp
        tests/m15_planning_tests.cpp
    tests/evidence_chain_tests.cpp
    tests/m23c_reference_evidence_tests.cpp
    tests/m23c_diode_reference_tests.cpp
    tests/m23c_frequency_reference_tests.cpp
    tests/m23c_dynamic_diode_reference_tests.cpp
    tests/m23c_bjt_reference_tests.cpp
    tests/m23c_jfet_reference_tests.cpp
    tests/m23c_mosfet_reference_tests.cpp
    tests/m23c_triode_reference_tests.cpp
    tests/m23c_pentode_reference_tests.cpp
    tests/m23c_tube_power_load_reference_tests.cpp
    tests/m23c_tube_supply_core_reference_tests.cpp
        tests/module_package_tests.cpp
        tests/module_runtime_tests.cpp
        tests/rig_runtime_tests.cpp
        tests/preset_tests.cpp
        tests/preset_controller_tests.cpp
        tests/theme_tests.cpp
        tests/midi_mapping_tests.cpp
        tests/shortcut_registry_tests.cpp
        tests/audio_routing_tests.cpp
        tests/audio_resampler_tests.cpp
        tests/pedal_layout_tests.cpp
        tests/rig_editor_tests.cpp
        tests/midi_control_session_tests.cpp
    )
    target_link_libraries(solo_rig_tests PRIVATE solo_rig_core)
    if(WIN32)
        target_sources(solo_rig_tests PRIVATE
            src/standalone/midi_control_session.cpp
            tests/asio_host_tests.cpp
            tests/asio_sample_conversion_tests.cpp
        )
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
