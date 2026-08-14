#include "content/model_database.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringValue(const JsonValue* value) {
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) {
        return std::nullopt;
    }
    return *value->AsString();
}

std::optional<double> NumberValue(const JsonValue* value) {
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

std::optional<ModelKind> ParseKind(std::string_view value) {
    if (value == "diode") return ModelKind::Diode;
    if (value == "zener") return ModelKind::Zener;
    if (value == "bjt") return ModelKind::Bjt;
    if (value == "jfet") return ModelKind::Jfet;
    if (value == "mosfet") return ModelKind::Mosfet;
    if (value == "opamp") return ModelKind::Opamp;
    if (value == "ota") return ModelKind::Ota;
    if (value == "triode") return ModelKind::Triode;
    if (value == "pentode") return ModelKind::Pentode;
    if (value == "beam-tetrode") return ModelKind::BeamTetrode;
    if (value == "vari-mu") return ModelKind::VariMu;
    if (value == "transformer-core") return ModelKind::TransformerCore;
    if (value == "test-mathematical") return ModelKind::TestMathematical;
    return std::nullopt;
}

std::optional<ModelAvailability> ParseAvailability(std::string_view value) {
    if (value == "approved") return ModelAvailability::Approved;
    if (value == "planned") return ModelAvailability::Planned;
    if (value == "blocked") return ModelAvailability::Blocked;
    return std::nullopt;
}

std::optional<ModelLicenseReviewStatus> ParseReviewStatus(std::string_view value) {
    if (value == "approved") return ModelLicenseReviewStatus::Approved;
    if (value == "pending") return ModelLicenseReviewStatus::Pending;
    if (value == "blocked") return ModelLicenseReviewStatus::Blocked;
    if (value == "rejected") return ModelLicenseReviewStatus::Rejected;
    return std::nullopt;
}

bool IsApprovedKind(ModelKind kind) {
    return kind == ModelKind::Diode || kind == ModelKind::Zener ||
           kind == ModelKind::Bjt || kind == ModelKind::Jfet || kind == ModelKind::Mosfet ||
           kind == ModelKind::Opamp || kind == ModelKind::Ota ||
           kind == ModelKind::Triode || kind == ModelKind::Pentode || kind == ModelKind::BeamTetrode ||
           kind == ModelKind::TestMathematical;
}

bool HasBjtParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Bjt) {
        return true;
    }
    struct RequiredParameter {
        std::string_view id;
        std::string_view unit;
        double minimum;
        double maximum;
    };
    constexpr RequiredParameter required[] = {
        {"saturation-current", "A", 1.0e-30, 1.0e-3},
        {"forward-beta", "1", 0.01, 1.0e6},
        {"reverse-beta", "1", 0.01, 1.0e6},
        {"thermal-voltage", "V", 1.0e-6, 1.0},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) {
        return false;
    }
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                   std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool HasJfetParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Jfet) {
        return true;
    }
    struct RequiredParameter {
        std::string_view id;
        std::string_view unit;
        double minimum;
        double maximum;
    };
    constexpr RequiredParameter required[] = {
        {"pinch-off-voltage", "V", 1.0e-6, 100.0},
        {"transconductance-parameter", "A/V^2", 1.0e-12, 1.0e6},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) {
        return false;
    }
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                   std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool HasMosfetParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Mosfet) {
        return true;
    }
    struct RequiredParameter {
        std::string_view id;
        std::string_view unit;
        double minimum;
        double maximum;
    };
    constexpr RequiredParameter required[] = {
        {"threshold-voltage", "V", 1.0e-6, 100.0},
        {"transconductance-parameter", "A/V^2", 1.0e-12, 1.0e6},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) {
        return false;
    }
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                   std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool HasAmplifierParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Opamp && model.kind != ModelKind::Ota) {
        return true;
    }
    struct RequiredParameter {
        std::string_view id;
        std::string_view unit;
        double minimum;
        double maximum;
    };
    const bool is_opamp = model.kind == ModelKind::Opamp;
    const RequiredParameter required[] = {
        {is_opamp ? "open-loop-gain" : "transconductance", is_opamp ? "1" : "A/V",
         is_opamp ? 1.0 : 1.0e-12, is_opamp ? 1.0e12 : 1.0e6},
        {"minimum-output", is_opamp ? "V" : "A", -1.0e6, 1.0e6},
        {"maximum-output", is_opamp ? "V" : "A", -1.0e6, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) {
        return false;
    }
    const bool parameters_valid = std::all_of(
        std::begin(required), std::end(required), [&model](const auto& expected) {
            return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
                return parameter.id == expected.id && parameter.unit == expected.unit &&
                       std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                       parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                       std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                       parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                       parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
            }) == 1;
        });
    if (!parameters_valid) {
        return false;
    }
    const auto minimum = std::find_if(model.parameters.begin(), model.parameters.end(), [](const auto& parameter) {
        return parameter.id == "minimum-output";
    });
    const auto maximum = std::find_if(model.parameters.begin(), model.parameters.end(), [](const auto& parameter) {
        return parameter.id == "maximum-output";
    });
    return minimum != model.parameters.end() && maximum != model.parameters.end() &&
           minimum->default_value < maximum->default_value;
}

bool HasTriodeParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Triode) {
        return true;
    }
    struct RequiredParameter {
        std::string_view id;
        std::string_view unit;
        double minimum;
        double maximum;
    };
    constexpr RequiredParameter required[] = {
        {"amplification-factor", "1", 1.0, 1.0e6},
        {"perveance", "A/V^1.5", 1.0e-12, 1.0e6},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) {
        return false;
    }
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                   std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool HasPentodeParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Pentode) return true;
    struct Required { std::string_view id; std::string_view unit; double minimum; double maximum; };
    constexpr Required required[] = {
        {"screen-amplification-factor", "1", 1.0, 1.0e6},
        {"perveance", "A/V^1.5", 1.0e-12, 1.0e6},
        {"knee-voltage", "V", 1.0e-6, 1.0e6},
        {"screen-current-fraction", "1", 0.0, 0.5},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) return false;
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool HasBeamTetrodeParameterContract(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::BeamTetrode) return true;
    struct Required { std::string_view id; std::string_view unit; double minimum; double maximum; };
    constexpr Required required[] = {
        {"screen-amplification-factor", "1", 1.0, 1.0e6},
        {"perveance", "A/V^1.5", 1.0e-12, 1.0e6},
        {"knee-voltage", "V", 1.0e-6, 1.0e6},
        {"screen-current-fraction", "1", 0.0, 0.5},
        {"max-current", "A", 1.0e-12, 1.0e6},
        {"max-step", "V", 1.0e-20, 10.0}};
    if (model.parameters.size() != std::size(required)) return false;
    return std::all_of(std::begin(required), std::end(required), [&model](const auto& expected) {
        return std::count_if(model.parameters.begin(), model.parameters.end(), [&expected](const auto& parameter) {
            return parameter.id == expected.id && parameter.unit == expected.unit &&
                   std::isfinite(parameter.default_value) && parameter.default_value >= expected.minimum &&
                   parameter.default_value <= expected.maximum && std::isfinite(parameter.minimum) &&
                   std::isfinite(parameter.maximum) && parameter.minimum >= expected.minimum &&
                   parameter.maximum <= expected.maximum && parameter.minimum < parameter.maximum &&
                   parameter.default_value >= parameter.minimum && parameter.default_value <= parameter.maximum;
        }) == 1;
    });
}

bool IsAuthorizedCleanRoomDevice(const ModelAuditRecord& model) {
    if (model.kind != ModelKind::Diode && model.kind != ModelKind::Zener && model.kind != ModelKind::Bjt &&
        model.kind != ModelKind::Jfet && model.kind != ModelKind::Mosfet &&
        model.kind != ModelKind::Opamp && model.kind != ModelKind::Ota &&
        model.kind != ModelKind::Triode && model.kind != ModelKind::Pentode &&
        model.kind != ModelKind::BeamTetrode) {
        return true;
    }
    if (model.kind == ModelKind::Opamp || model.kind == ModelKind::Ota) {
        const auto expected_part = model.kind == ModelKind::Opamp
            ? "self-authored-bounded-opamp-no-commercial-part"
            : "self-authored-bounded-ota-no-commercial-part";
        return model.source.origin.starts_with("SoloRig self-authored clean-room ") &&
               model.license.permission_evidence == "self-authored-clean-room" &&
               model.part_number == expected_part;
    }
    if (model.kind == ModelKind::Triode) {
        return model.source.origin.starts_with("SoloRig self-authored clean-room ") &&
               model.license.permission_evidence == "self-authored-clean-room" &&
               model.part_number == "self-authored-space-charge-triode-no-commercial-part";
    }
    if (model.kind == ModelKind::Pentode) {
        return model.source.origin.starts_with("SoloRig self-authored clean-room ") &&
               model.license.permission_evidence == "self-authored-clean-room" &&
               model.part_number == "self-authored-space-charge-pentode-no-commercial-part";
    }
    if (model.kind == ModelKind::BeamTetrode) {
        return model.source.origin.starts_with("SoloRig self-authored clean-room ") &&
               model.license.permission_evidence == "self-authored-clean-room" &&
               model.part_number == "self-authored-beam-tetrode-capture-no-commercial-part";
    }
    if (model.kind == ModelKind::Bjt || model.kind == ModelKind::Jfet || model.kind == ModelKind::Mosfet) {
        const bool polarity_allowed = model.kind == ModelKind::Bjt
            ? model.polarity.has_value() && (*model.polarity == "npn" || *model.polarity == "pnp")
            : model.polarity.has_value() && (*model.polarity == "n-channel" || *model.polarity == "p-channel");
        if (!polarity_allowed) {
            return false;
        }
        return model.source.origin.starts_with("SoloRig self-authored clean-room ") &&
               model.license.permission_evidence == "self-authored-clean-room" &&
               model.part_number == "self-authored-" + *model.polarity + "-no-commercial-part";
    }
    return model.part_number.find("no-commercial-part") != std::string::npos &&
           model.source.origin.find("self-authored clean-room") != std::string::npos &&
           model.license.permission_evidence == "self-authored-clean-room";
}

} // namespace

const char* ModelKindName(ModelKind kind) noexcept {
    switch (kind) {
    case ModelKind::Diode: return "diode";
    case ModelKind::Zener: return "zener";
    case ModelKind::Bjt: return "bjt";
    case ModelKind::Jfet: return "jfet";
    case ModelKind::Mosfet: return "mosfet";
    case ModelKind::Opamp: return "opamp";
    case ModelKind::Ota: return "ota";
    case ModelKind::Triode: return "triode";
    case ModelKind::Pentode: return "pentode";
    case ModelKind::BeamTetrode: return "beam-tetrode";
    case ModelKind::VariMu: return "vari-mu";
    case ModelKind::TransformerCore: return "transformer-core";
    case ModelKind::TestMathematical: return "test-mathematical";
    }
    return "unknown";
}

std::vector<ModelKindContract> ModelKindContracts() {
    return {
        {ModelKind::Diode, ModelAvailability::Approved},
        {ModelKind::Zener, ModelAvailability::Approved},
        {ModelKind::Bjt, ModelAvailability::Approved},
        {ModelKind::Jfet, ModelAvailability::Approved},
        {ModelKind::Mosfet, ModelAvailability::Approved},
        {ModelKind::Opamp, ModelAvailability::Approved},
        {ModelKind::Ota, ModelAvailability::Approved},
        {ModelKind::Triode, ModelAvailability::Approved},
        {ModelKind::Pentode, ModelAvailability::Approved},
        {ModelKind::BeamTetrode, ModelAvailability::Approved},
        {ModelKind::VariMu, ModelAvailability::Blocked},
        {ModelKind::TransformerCore, ModelAvailability::Blocked},
        {ModelKind::TestMathematical, ModelAvailability::Approved},
    };
}

ModelAuditParseResult ParseModelAuditJson(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidJson};
    }
    const auto& object = *parsed.value->AsObject();
    const auto version = NumberValue(Find(object, "version"));
    if (!version.has_value() || *version != 1.0) {
        return {.model = std::nullopt, .error = ModelAuditError::UnsupportedVersion};
    }
    const auto schema = StringValue(Find(object, "schema"));
    if (!schema.has_value() || *schema != "solorig-model-audit") {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidSchema};
    }
    const auto model_id = StringValue(Find(object, "modelId"));
    const auto family = StringValue(Find(object, "deviceFamily"));
    const auto part = StringValue(Find(object, "partNumber"));
    const auto kind_string = StringValue(Find(object, "modelKind"));
    const auto status_string = StringValue(Find(object, "runtimeStatus"));
    if (!model_id.has_value() || !family.has_value() || !part.has_value() ||
        !kind_string.has_value() || !status_string.has_value()) {
        return {.model = std::nullopt, .error = ModelAuditError::MissingField};
    }
    const auto kind = ParseKind(*kind_string);
    const auto availability = ParseAvailability(*status_string);
    if (!kind.has_value()) {
        return {.model = std::nullopt, .error = ModelAuditError::UnknownModelKind};
    }
    if (!availability.has_value()) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidField};
    }

    ModelAuditRecord record{.model_id = *model_id,
                            .device_family = *family,
                            .part_number = *part,
                            .kind = *kind,
                            .availability = *availability};
    if (*kind == ModelKind::Bjt || *kind == ModelKind::Jfet || *kind == ModelKind::Mosfet) {
        const auto polarity = StringValue(Find(object, "polarity"));
        const bool polarity_allowed = *kind == ModelKind::Bjt
            ? polarity.has_value() && (*polarity == "npn" || *polarity == "pnp")
            : polarity.has_value() && (*polarity == "n-channel" || *polarity == "p-channel");
        if (!polarity_allowed) {
            return {.model = std::nullopt, .error = ModelAuditError::InvalidField};
        }
        record.polarity = *polarity;
    }

    const auto* parameters_value = Find(object, "parameters");
    const auto* parameters = parameters_value == nullptr ? nullptr : parameters_value->AsArray();
    if (parameters == nullptr || parameters->empty()) {
        return {.model = std::nullopt, .error = ModelAuditError::MissingField};
    }
    std::unordered_set<std::string> parameter_ids;
    for (const auto& parameter_value : *parameters) {
        const auto* parameter = parameter_value.AsObject();
        const auto id = parameter == nullptr ? std::nullopt : StringValue(Find(*parameter, "id"));
        const auto unit = parameter == nullptr ? std::nullopt : StringValue(Find(*parameter, "unit"));
        const auto* default_value = parameter == nullptr ? nullptr : Find(*parameter, "default");
        const auto* minimum_value = parameter == nullptr ? nullptr : Find(*parameter, "min");
        const auto* maximum_value = parameter == nullptr ? nullptr : Find(*parameter, "max");
        if (!id.has_value() || !unit.has_value() || default_value == nullptr ||
            minimum_value == nullptr || maximum_value == nullptr) {
            return {.model = std::nullopt, .error = ModelAuditError::MissingField};
        }
        const auto default_number = NumberValue(default_value);
        const auto minimum = NumberValue(minimum_value);
        const auto maximum = NumberValue(maximum_value);
        if (!default_number.has_value() || !minimum.has_value() || !maximum.has_value()) {
            return {.model = std::nullopt, .error = ModelAuditError::NonFiniteParameter};
        }
        if (!parameter_ids.insert(*id).second) {
            return {.model = std::nullopt, .error = ModelAuditError::DuplicateParameter};
        }
        if (*minimum >= *maximum || *default_number < *minimum || *default_number > *maximum) {
            return {.model = std::nullopt, .error = ModelAuditError::ParameterOutOfRange};
        }
        record.parameters.push_back({.id = *id,
                                     .default_value = *default_number,
                                     .minimum = *minimum,
                                     .maximum = *maximum,
                                     .unit = *unit});
    }

    const auto* source_object = Find(object, "source");
    const auto* source_fields = source_object == nullptr ? nullptr : source_object->AsObject();
    const auto origin = source_fields == nullptr ? std::nullopt : StringValue(Find(*source_fields, "origin"));
    const auto source_version = source_fields == nullptr ? std::nullopt : StringValue(Find(*source_fields, "version"));
    const auto integrity = source_fields == nullptr ? std::nullopt : StringValue(Find(*source_fields, "integrity"));
    if (source_fields == nullptr) {
        return {.model = std::nullopt, .error = ModelAuditError::MissingField};
    }
    if (!origin.has_value() || !source_version.has_value() || !integrity.has_value()) {
        return {.model = std::nullopt, .error = ModelAuditError::MissingField};
    }
    record.source = {.origin = *origin, .version = *source_version, .integrity = *integrity};

    const auto* license_object = Find(object, "license");
    const auto* license_fields = license_object == nullptr ? nullptr : license_object->AsObject();
    const auto spdx = license_fields == nullptr ? std::nullopt : StringValue(Find(*license_fields, "spdx"));
    const auto permission = license_fields == nullptr
        ? std::nullopt : StringValue(Find(*license_fields, "permissionEvidence"));
    const auto review = license_fields == nullptr
        ? std::nullopt : StringValue(Find(*license_fields, "reviewStatus"));
    if (license_fields == nullptr || !review.has_value() || Find(*license_fields, "spdx") == nullptr ||
        Find(*license_fields, "permissionEvidence") == nullptr) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidLicense};
    }
    const auto review_status = ParseReviewStatus(*review);
    if (!review_status.has_value()) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidLicense};
    }
    if (*review_status == ModelLicenseReviewStatus::Approved &&
        (!spdx.has_value() || !permission.has_value())) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidLicense};
    }
    record.license = {.spdx = spdx.value_or(""),
                      .permission_evidence = permission.value_or(""),
                      .review_status = *review_status};

    const auto* applicability_value = Find(object, "applicability");
    const auto* applicability = applicability_value == nullptr ? nullptr : applicability_value->AsArray();
    if (applicability == nullptr || applicability->empty()) {
        return {.model = std::nullopt, .error = ModelAuditError::InvalidApplicability};
    }
    for (const auto& range_value : *applicability) {
        const auto* range = range_value.AsObject();
        const auto quantity = range == nullptr ? std::nullopt : StringValue(Find(*range, "quantity"));
        const auto unit = range == nullptr ? std::nullopt : StringValue(Find(*range, "unit"));
        const auto minimum = range == nullptr ? std::nullopt : NumberValue(Find(*range, "min"));
        const auto maximum = range == nullptr ? std::nullopt : NumberValue(Find(*range, "max"));
        if (!quantity.has_value() || !unit.has_value() || !minimum.has_value() || !maximum.has_value() ||
            *minimum >= *maximum) {
            return {.model = std::nullopt, .error = ModelAuditError::InvalidApplicability};
        }
        record.applicability.push_back({.quantity = *quantity,
                                        .minimum = *minimum,
                                        .maximum = *maximum,
                                        .unit = *unit});
    }

    const auto* evidence_value = Find(object, "evidenceRefs");
    const auto* evidence = evidence_value == nullptr ? nullptr : evidence_value->AsArray();
    if (evidence == nullptr || evidence->empty()) {
        return {.model = std::nullopt, .error = ModelAuditError::MissingField};
    }
    for (const auto& reference : *evidence) {
        const auto value = StringValue(&reference);
        if (!value.has_value()) {
            return {.model = std::nullopt, .error = ModelAuditError::InvalidField};
        }
        record.evidence_refs.push_back(*value);
    }
    return {.model = std::move(record), .error = std::nullopt};
}

std::optional<ModelAuditError> ModelDatabase::Register(ModelAuditRecord model) {
    if (model.license.review_status != ModelLicenseReviewStatus::Approved) {
        return ModelAuditError::MissingModelAuthorization;
    }
    if (!IsAuthorizedCleanRoomDevice(model) ||
        (model.kind == ModelKind::Bjt && (!model.polarity.has_value() || !HasBjtParameterContract(model))) ||
        (model.kind == ModelKind::Jfet && (!model.polarity.has_value() || !HasJfetParameterContract(model))) ||
        (model.kind == ModelKind::Mosfet && (!model.polarity.has_value() || !HasMosfetParameterContract(model))) ||
        ((model.kind == ModelKind::Opamp || model.kind == ModelKind::Ota) &&
         !HasAmplifierParameterContract(model)) ||
        (model.kind == ModelKind::Triode && !HasTriodeParameterContract(model)) ||
        (model.kind == ModelKind::Pentode && !HasPentodeParameterContract(model)) ||
        (model.kind == ModelKind::BeamTetrode && !HasBeamTetrodeParameterContract(model))) {
        return ModelAuditError::MissingModelAuthorization;
    }
    if (model.availability != ModelAvailability::Approved || !IsApprovedKind(model.kind)) {
        return ModelAuditError::Blocked;
    }
    const auto duplicate = std::find_if(records_.begin(), records_.end(),
                                        [&model](const auto& existing) {
                                            return existing.model_id == model.model_id;
                                        });
    if (duplicate != records_.end()) {
        return ModelAuditError::DuplicateModel;
    }
    records_.push_back(std::move(model));
    return std::nullopt;
}

ModelDatabaseBuildResult BuildModelDatabase(std::string_view source) {
    const auto parsed = ParseModelAuditJson(source);
    if (parsed.error.has_value()) {
        return {.database = std::nullopt, .error = parsed.error};
    }
    ModelDatabase database;
    const auto error = database.Register(*parsed.model);
    if (error.has_value()) {
        return {.database = std::nullopt, .error = error};
    }
    return {.database = std::move(database), .error = std::nullopt};
}

ModelLookupResult ModelDatabase::Lookup(std::string_view model_id) const {
    const auto iterator = std::find_if(records_.begin(), records_.end(),
                                       [model_id](const auto& model) { return model.model_id == model_id; });
    if (iterator == records_.end()) {
        return {.model = std::nullopt, .error = ModelAuditError::UnknownModel};
    }
    return {.model = *iterator, .error = std::nullopt};
}

ModelLookupResult ModelDatabase::Bind(const ModelBinding& binding) const {
    const auto result = Lookup(binding.model_id);
    if (result.error.has_value()) {
        return result;
    }
    if (result.model->kind != binding.expected_kind) {
        return {.model = std::nullopt, .error = ModelAuditError::ModelKindMismatch};
    }
    if (binding.expected_polarity.has_value() && result.model->polarity != binding.expected_polarity) {
        return {.model = std::nullopt, .error = ModelAuditError::ModelKindMismatch};
    }
    return result;
}

} // namespace solo_rig
