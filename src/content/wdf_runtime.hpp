#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace solo_rig {

enum class WdfElementKind {
    Resistor,
    Capacitor,
    Inductor,
    Transformer,
};

enum class WdfError {
    None,
    InvalidValue,
    NonFinite,
    NotPrepared,
    InvalidTopology,
};

struct WdfPort {
    double resistance{};
    double incident{};
    double reflected{};

    [[nodiscard]] double Voltage() const noexcept { return (incident + reflected) * 0.5; }
    [[nodiscard]] double Current() const noexcept;
};

struct WdfOnePortPrepareResult;

class WdfOnePort final {
public:
    WdfOnePort() = default;

    [[nodiscard]] WdfError Process(double incident, double& reflected) noexcept;
    [[nodiscard]] double ReflectedWave() const noexcept;
    [[nodiscard]] bool CanSetSampleRate(double sample_rate_hz) const noexcept;
    [[nodiscard]] bool SetSampleRate(double sample_rate_hz) noexcept;
    // Used only at the non-real-time Prepare boundary to transfer a validated
    // immutable port into preallocated runtime storage without relying on a
    // container move during audio processing.
    void CopyPreparedState(const WdfOnePort& source) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double PortResistance() const noexcept { return port_.resistance; }
    [[nodiscard]] WdfPort Port() const noexcept { return port_; }

private:
    friend struct WdfOnePortPrepareResult;
    friend WdfOnePortPrepareResult PrepareWdfOnePort(WdfElementKind kind,
                                                     double value,
                                                     double sample_rate_hz,
                                                     double initial,
                                                     double turns_ratio);

    WdfElementKind kind_{WdfElementKind::Resistor};
    double value_{};
    double initial_{};
    double turns_ratio_{1.0};
    double sample_rate_hz_{};
    double state_{};
    WdfPort port_{};
    bool prepared_{};
};

struct WdfOnePortPrepareResult {
    std::optional<WdfOnePort> port;
    std::optional<WdfError> error;
};

[[nodiscard]] WdfOnePortPrepareResult PrepareWdfOnePort(WdfElementKind kind,
                                                        double value,
                                                        double sample_rate_hz,
                                                        double initial = 0.0,
                                                        double turns_ratio = 1.0);

enum class WdfAdaptorKind {
    Series,
    Parallel,
    RType,
};

struct WdfAdaptorPrepareResult;

class WdfAdaptor final {
public:
    WdfAdaptor() = default;

    // Scattering writes exactly reflected.size() values and never allocates.
    [[nodiscard]] WdfError Scatter(std::span<const double> incident,
                                   std::span<double> reflected) const noexcept;
    [[nodiscard]] bool SetResistances(std::span<const double> resistances) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] WdfAdaptorKind Kind() const noexcept { return kind_; }

private:
    friend struct WdfAdaptorPrepareResult;
    friend WdfAdaptorPrepareResult PrepareWdfAdaptor(WdfAdaptorKind kind,
                                                     std::span<const double> resistances,
                                                     double turns_ratio);

    WdfAdaptorKind kind_{WdfAdaptorKind::Series};
    std::vector<double> resistances_;
    double turns_ratio_{1.0};
    bool prepared_{};
};

struct WdfAdaptorPrepareResult {
    std::optional<WdfAdaptor> adaptor;
    std::optional<WdfError> error;
};

[[nodiscard]] WdfAdaptorPrepareResult PrepareWdfAdaptor(WdfAdaptorKind kind,
                                                        std::span<const double> resistances,
                                                        double turns_ratio = 1.0);

} // namespace solo_rig
