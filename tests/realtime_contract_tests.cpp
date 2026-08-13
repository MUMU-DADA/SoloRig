#include "test_framework.hpp"

#include "core/audio_buffer.hpp"
#include "core/audio_graph.hpp"
#include "core/realtime_contract.hpp"
#include "core/realtime_load_meter.hpp"
#include "core/rig_runtime.hpp"

#include <cmath>

TEST_CASE("Realtime contract is an explicit no-allocation process boundary") {
    constexpr auto contract = solo_rig::DefaultRealtimeContract();
    static_assert(contract.process_noexcept);
    static_assert(!contract.process_allocates);
    static_assert(!contract.process_reads_files);
    static_assert(!contract.process_parses_json);
    static_assert(!contract.process_locks);
    static_assert(contract.prepare_owns_compilation);
    static_assert(solo_rig::IsRealtimeSafe(contract));
    static_assert(noexcept(std::declval<solo_rig::AudioGraph&>().Process(
        std::declval<const solo_rig::AudioBuffer&>(), std::declval<solo_rig::AudioBuffer&>(), 0U)));
    static_assert(noexcept(std::declval<solo_rig::RigRuntime&>().Process(
        std::declval<const solo_rig::AudioBuffer&>(), std::declval<solo_rig::AudioBuffer&>(), 0U)));
    REQUIRE(contract.maximum_oversampling == 16U);
}

TEST_CASE("Realtime contract reports deterministic latency and load measurements") {
    solo_rig::RealtimeLoadMeter meter;
    meter.Configure(48000.0, 48U);
    meter.RecordProcessingNanoseconds(500'000U);
    REQUIRE(std::fabs(meter.LastLoadFraction() - 0.5) < 0.01);
    REQUIRE(meter.PeakLoadFraction() >= meter.LastLoadFraction());

    auto contract = solo_rig::DefaultRealtimeContract();
    contract.sample_rate_hz = 48000U;
    contract.max_block_size = 48U;
    contract.declared_latency_samples = 7U;
    contract.estimated_cpu_percent = meter.LastLoadFraction() * 100.0;
    REQUIRE(solo_rig::IsRealtimeSafe(contract));
    REQUIRE(contract.declared_latency_samples == 7U);
    REQUIRE(std::isfinite(contract.estimated_cpu_percent));
}
