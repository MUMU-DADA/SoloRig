#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace solo_rig {

inline constexpr std::size_t kGenericTubeAmpMaximumStages = 8U;
inline constexpr std::size_t kGenericTubeAmpMaximumComponents = 16U;
inline constexpr std::size_t kGenericTubeAmpMaximumSupplies = 1U;
inline constexpr std::size_t kGenericTubeAmpMaximumNets = 11U;
inline constexpr std::size_t kGenericTubeAmpMaximumNetTerminals = 9U;
inline constexpr std::size_t kGenericTubeAmpMaximumComponentPins = 4U;
inline constexpr std::size_t kGenericTubeAmpTextCapacity = 48U;

enum class GenericTubeAmpStageKind : std::uint8_t {
    Input,
    Triode,
    CouplingNetwork,
    Pentode,
    BeamTetrode,
    Supply,
    Transformer,
    Speaker,
    Output,
};

enum class GenericTubeAmpPortDirection : std::uint8_t {
    Terminal,
    Input,
    Output,
};

enum class GenericTubeAmpPortDomain : std::uint8_t {
    Audio,
    Bias,
    SupplyRail,
    TransformerPrimary,
    TransformerSecondary,
    Ground,
};

enum class GenericTubeAmpComponentKind : std::uint8_t {
    Resistor,
    Capacitor,
    CoupledInductor,
    SpeakerLoad,
};

struct GenericTubeAmpPortRequest {
    std::string_view id;
    std::string_view node;
    std::string_view pin;
    GenericTubeAmpPortDirection direction{GenericTubeAmpPortDirection::Terminal};
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
};

struct GenericTubeAmpStageRequest {
    std::string_view id;
    std::string_view model_reference;
    GenericTubeAmpStageKind kind{GenericTubeAmpStageKind::Input};
    std::array<GenericTubeAmpPortRequest, kGenericTubeAmpMaximumComponentPins> ports{};
    std::size_t port_count{};
    std::array<double, 4U> parameters{};
};

struct GenericTubeAmpComponentPinRequest {
    std::string_view id;
    std::string_view node;
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
};

struct GenericTubeAmpComponentRequest {
    std::string_view id;
    std::string_view model_reference;
    GenericTubeAmpComponentKind kind{GenericTubeAmpComponentKind::Resistor};
    std::array<GenericTubeAmpComponentPinRequest, kGenericTubeAmpMaximumComponentPins> pins{};
    std::size_t pin_count{};
    std::array<double, 8U> parameters{};
};

struct GenericTubeAmpSupplyRequest {
    std::string_view id;
    std::string_view model_reference;
    std::string_view positive_node;
    std::string_view negative_node;
    std::array<double, 9U> parameters{};
};

enum class GenericTubeAmpTerminalOwner : std::uint8_t {
    Stage,
    Component,
};

struct GenericTubeAmpNetTerminalRequest {
    GenericTubeAmpTerminalOwner owner{GenericTubeAmpTerminalOwner::Stage};
    std::string_view owner_id;
    std::string_view terminal_id;
};

struct GenericTubeAmpNetRequest {
    std::string_view id;
    std::string_view node;
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
    std::array<GenericTubeAmpNetTerminalRequest, kGenericTubeAmpMaximumNetTerminals> terminals{};
    std::size_t terminal_count{};
};

struct GenericTubeAmpCompileRequest {
    std::array<GenericTubeAmpStageRequest, kGenericTubeAmpMaximumStages> stages{};
    std::size_t stage_count{};
    std::array<GenericTubeAmpComponentRequest, kGenericTubeAmpMaximumComponents> components{};
    std::size_t component_count{};
    std::array<GenericTubeAmpSupplyRequest, kGenericTubeAmpMaximumSupplies> supplies{};
    std::size_t supply_count{};
    std::array<GenericTubeAmpNetRequest, kGenericTubeAmpMaximumNets> nets{};
    std::size_t net_count{};
};

enum class GenericTubeAmpCompileError : std::uint8_t {
    None,
    InvalidPlan,
    UnsupportedTopology,
    IncompatibleConnection,
    MissingStage,
    DuplicateStage,
    InvalidModelReference,
    CapacityExceeded,
    NonFinite,
    InvalidPort,
    DuplicatePort,
    MissingPort,
    UnknownStage,
    UnknownPort,
    MissingComponent,
    DuplicateComponent,
    UnknownComponent,
    InvalidComponentPin,
    MissingNet,
    DuplicateNet,
    UnknownTerminal,
    DuplicateTerminal,
    InvalidNetMembership,
    ShortCircuit,
    InvalidSupply,
    MissingGroundReference,
    InvalidOutputMeasurement,
};

enum class GenericTubeAmpPlanStatus : std::uint8_t {
    Empty,
    Ready,
};

template <std::size_t Capacity>
struct GenericTubeAmpFixedText final {
    std::array<char, Capacity> value{};
    std::uint8_t size{};

    [[nodiscard]] std::string_view View() const noexcept {
        return {value.data(), size};
    }
};

struct GenericTubeAmpPortPlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> node;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> pin;
    GenericTubeAmpPortDirection direction{GenericTubeAmpPortDirection::Terminal};
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
};

struct GenericTubeAmpStagePlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> model_reference;
    GenericTubeAmpStageKind kind{GenericTubeAmpStageKind::Input};
    std::array<GenericTubeAmpPortPlan, kGenericTubeAmpMaximumComponentPins> ports{};
    std::size_t port_count{};
    std::array<double, 4U> parameters{};
};

struct GenericTubeAmpComponentPinPlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> node;
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
};

struct GenericTubeAmpComponentPlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> model_reference;
    GenericTubeAmpComponentKind kind{GenericTubeAmpComponentKind::Resistor};
    std::array<GenericTubeAmpComponentPinPlan, kGenericTubeAmpMaximumComponentPins> pins{};
    std::size_t pin_count{};
    std::array<double, 8U> parameters{};
};

struct GenericTubeAmpSupplyPlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> model_reference;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> positive_node;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> negative_node;
    std::array<double, 9U> parameters{};
};

struct GenericTubeAmpNetTerminalPlan {
    GenericTubeAmpTerminalOwner owner{GenericTubeAmpTerminalOwner::Stage};
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> owner_id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> terminal_id;
};

struct GenericTubeAmpNetPlan {
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> id;
    GenericTubeAmpFixedText<kGenericTubeAmpTextCapacity> node;
    GenericTubeAmpPortDomain domain{GenericTubeAmpPortDomain::Audio};
    std::array<GenericTubeAmpNetTerminalPlan, kGenericTubeAmpMaximumNetTerminals> terminals{};
    std::size_t terminal_count{};
};

struct GenericTubeAmpCompilePlan {
    GenericTubeAmpPlanStatus status{GenericTubeAmpPlanStatus::Empty};
    std::array<GenericTubeAmpStagePlan, kGenericTubeAmpMaximumStages> stages{};
    std::size_t stage_count{};
    std::array<GenericTubeAmpComponentPlan, kGenericTubeAmpMaximumComponents> components{};
    std::size_t component_count{};
    std::array<GenericTubeAmpSupplyPlan, kGenericTubeAmpMaximumSupplies> supplies{};
    std::size_t supply_count{};
    std::array<GenericTubeAmpNetPlan, kGenericTubeAmpMaximumNets> nets{};
    std::size_t net_count{};
};

struct GenericTubeAmpCompileResult {
    std::optional<GenericTubeAmpCompilePlan> plan;
    GenericTubeAmpCompileError error{GenericTubeAmpCompileError::None};
};

// Compile-only typed contract. It does not parse files, allocate a runtime, or enter MNA.
[[nodiscard]] GenericTubeAmpCompileResult CompileGenericTubeAmp(
    const GenericTubeAmpCompileRequest& request) noexcept;

} // namespace solo_rig
