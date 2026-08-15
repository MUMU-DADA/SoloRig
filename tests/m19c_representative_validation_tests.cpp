#include "m19c_validation_harness.hpp"
#include "test_framework.hpp"
#include "dsp/mna_solver.hpp"

#include <type_traits>

TEST_CASE("M19-C representative generic coverage reports auditable status") {
    const auto evidence = solo_rig::m19c_validation::ValidateRepresentativeCoverage();
    REQUIRE(evidence.size() == solo_rig::m19c_validation::kRepresentativeCategoryCount);
    for (const auto& item : evidence) {
        REQUIRE(item.compiled || item.status == solo_rig::m19c_validation::RepresentativeStatus::StableRefusal);
        REQUIRE(item.prepared || item.status == solo_rig::m19c_validation::RepresentativeStatus::StableRefusal);
        if (item.status == solo_rig::m19c_validation::RepresentativeStatus::RunnableGeneric) {
            REQUIRE(item.compiled);
            REQUIRE(item.prepared);
            REQUIRE(item.finite_nontrivial);
            REQUIRE(item.reset_deterministic);
            REQUIRE(item.sample_rate_and_oversampling);
            REQUIRE(item.rejection_boundary);
        } else {
            REQUIRE(!item.refusal_reason.empty());
        }
    }
}

TEST_CASE("M19-C representative generic coverage keeps remaining refusal categories explicit") {
    const auto evidence = solo_rig::m19c_validation::ValidateRepresentativeCoverage();
    REQUIRE(evidence[static_cast<std::size_t>(solo_rig::m19c_validation::RepresentativeCategory::WahPhaser)].status ==
        solo_rig::m19c_validation::RepresentativeStatus::StableRefusal);
    REQUIRE(evidence[static_cast<std::size_t>(solo_rig::m19c_validation::RepresentativeCategory::ChorusFlanger)].status ==
        solo_rig::m19c_validation::RepresentativeStatus::RunnableGeneric);
    REQUIRE(evidence[static_cast<std::size_t>(solo_rig::m19c_validation::RepresentativeCategory::OpticalHardware)].status ==
        solo_rig::m19c_validation::RepresentativeStatus::StableRefusal);
    static_assert(noexcept(solo_rig::MnaPreparedCircuit::ProcessContract()));
}
