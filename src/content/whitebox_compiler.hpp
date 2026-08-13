#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class WhiteboxPortDirection {
    Input,
    Output,
};

enum class WhiteboxTopologyClass {
    Unknown,
    Series,
    Parallel,
    Spqr,
    Rigid,
};

enum class WhiteboxBackend {
    Mna,
    Wdf,
};

enum class WhiteboxCompileError {
    InvalidJson,
    UnsupportedVersion,
    InvalidSchema,
    MissingField,
    InvalidField,
    DuplicateId,
    InvalidNode,
    InvalidPin,
    InvalidPort,
    InvalidSupply,
    InvalidSubcircuit,
    InvalidModelReference,
    MissingModelReference,
    MissingModelAuthorization,
    UnsafeModelPath,
    InvalidBackend,
    InvalidTopology,
    UnknownTopology,
    UnsupportedTopology,
    NonFiniteValue,
};

enum class WhiteboxProcessError {
    None,
    NotPrepared,
    NonFiniteInput,
    UnsupportedRuntime,
};

struct WhiteboxNode {
    std::string id;
};

struct WhiteboxPort {
    std::string id;
    WhiteboxPortDirection direction{WhiteboxPortDirection::Input};
    std::string node;
};

struct WhiteboxSupply {
    std::string id;
    std::string positive;
    std::string negative;
    double voltage{};
};

struct WhiteboxComponent {
    std::string id;
    std::string kind;
    std::vector<std::string> pins;
    std::optional<double> value;
    std::optional<double> initial;
    std::optional<std::string> nonlinear_group;
    std::optional<std::string> model_reference;
};

struct WhiteboxSubcircuit {
    std::string id;
    std::vector<std::string> ports;
    std::optional<std::string> model_reference;
};

struct WhiteboxModelReference {
    std::string id;
    std::string path;
    std::string status;
};

struct WhiteboxCompileResult;

class WhiteboxPreparedGraph final {
public:
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] WhiteboxProcessError Process(double input, double& output) noexcept;

private:
    friend class WhiteboxCompiledGraph;
    bool prepared_{};
};

struct WhiteboxPrepareResult {
    std::optional<WhiteboxPreparedGraph> graph;
    std::optional<WhiteboxCompileError> error;
};

class WhiteboxCompiledGraph final {
public:
    [[nodiscard]] const std::vector<WhiteboxNode>& Nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<WhiteboxPort>& Ports() const noexcept { return ports_; }
    [[nodiscard]] const std::vector<WhiteboxSupply>& Supplies() const noexcept { return supplies_; }
    [[nodiscard]] const std::vector<WhiteboxComponent>& Components() const noexcept { return components_; }
    [[nodiscard]] const std::vector<WhiteboxSubcircuit>& Subcircuits() const noexcept { return subcircuits_; }
    [[nodiscard]] const std::vector<WhiteboxModelReference>& ModelReferences() const noexcept {
        return model_references_;
    }
    [[nodiscard]] WhiteboxTopologyClass Classification() const noexcept { return classification_; }
    [[nodiscard]] WhiteboxBackend Backend() const noexcept { return backend_; }
    [[nodiscard]] std::string_view ReferenceGround() const noexcept { return reference_ground_; }
    // Prepare is a non-real-time ownership boundary. It creates a token from
    // validated immutable topology state; no JSON or file access is permitted
    // after this point.
    [[nodiscard]] WhiteboxPrepareResult Prepare() const;

private:
    friend struct WhiteboxCompileResult;
    friend WhiteboxCompileResult CompileWhiteboxGraphV2Json(std::string_view source);

    std::vector<WhiteboxNode> nodes_;
    std::vector<WhiteboxPort> ports_;
    std::vector<WhiteboxSupply> supplies_;
    std::vector<WhiteboxComponent> components_;
    std::vector<WhiteboxSubcircuit> subcircuits_;
    std::vector<WhiteboxModelReference> model_references_;
    std::string reference_ground_;
    WhiteboxTopologyClass classification_{WhiteboxTopologyClass::Unknown};
    WhiteboxBackend backend_{WhiteboxBackend::Mna};
};

struct WhiteboxCompileResult {
    std::optional<WhiteboxCompiledGraph> graph;
    std::optional<WhiteboxCompileError> error;
    WhiteboxTopologyClass classification{WhiteboxTopologyClass::Unknown};
};

[[nodiscard]] WhiteboxCompileResult CompileWhiteboxGraphV2Json(std::string_view source);

} // namespace solo_rig
