#include "dsp/tuner.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <numbers>
#include <vector>

TEST_CASE("TunerAnalyzer detects concert A from a clean input signal") {
    constexpr double sample_rate_hz = 48000.0;
    constexpr double frequency_hz = 440.0;
    std::vector<float> samples(4096);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * frequency_hz *
                                                             static_cast<double>(index) / sample_rate_hz));
    }

    const solo_rig::TunerAnalyzer analyzer;
    const auto result = analyzer.Analyze(samples, sample_rate_hz);

    REQUIRE(result.has_value());
    REQUIRE(std::fabs(result->frequency_hz - frequency_hz) < 3.0);
    REQUIRE(result->midi_note == 69);
    REQUIRE(std::fabs(result->cents_from_nearest_note) < 15.0);
}

TEST_CASE("TunerAnalyzer suppresses silent and invalid input") {
    const std::vector<float> silence(1024, 0.0F);
    const solo_rig::TunerAnalyzer analyzer;

    REQUIRE(!analyzer.Analyze(silence, 48000.0).has_value());
    REQUIRE(!analyzer.Analyze(silence, 0.0).has_value());
}
