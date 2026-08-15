#include "ui/rig_editor.hpp"

#include "content/default_preset.hpp"
#include "content/json.hpp"
#include "content/module_package.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <stack>
#include <system_error>

namespace solo_rig {
namespace {

const PedalSkinDefinition kDefaultSkin{
    .id = "solorig.default-pedal",
    .knob = "standard-knob",
    .toggle = "standard-toggle",
    .button = "standard-button",
    .footswitch = "standard-footswitch",
};

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringField(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    return value != nullptr && value->AsString() != nullptr ? std::optional<std::string>(*value->AsString()) : std::nullopt;
}

constexpr std::uintmax_t kMaximumSkinImageBytes = 16U * 1024U * 1024U;

struct SkinControlDefinition {
    std::string id;
    std::optional<std::filesystem::path> image;
};

bool IsSafeSkinImagePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == std::filesystem::path("..")) {
            return false;
        }
    }
    return true;
}

std::optional<std::filesystem::path> ResolveSkinImage(const std::filesystem::path& skin_file,
                                                      std::string_view image_name) {
    const std::filesystem::path relative(image_name);
    if (!IsSafeSkinImagePath(relative)) {
        return std::nullopt;
    }
    std::error_code file_error;
    const auto absolute = std::filesystem::absolute(skin_file.parent_path() / relative, file_error).lexically_normal();
    if (file_error || absolute.empty() || !std::filesystem::is_regular_file(absolute, file_error) || file_error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(absolute, file_error);
    if (file_error || size == 0U || size > kMaximumSkinImageBytes) {
        return std::nullopt;
    }
    return absolute;
}

std::optional<SkinControlDefinition> ReadSkinControl(const JsonValue& value,
                                                      const std::filesystem::path& skin_file) {
    if (const auto* id = value.AsString(); id != nullptr) {
        if (id->empty()) return std::nullopt;
        return SkinControlDefinition{.id = *id};
    }
    const auto* object = value.AsObject();
    if (object == nullptr) return std::nullopt;
    const auto id = StringField(*object, "id");
    if (!id.has_value() || id->empty()) return std::nullopt;
    std::optional<std::filesystem::path> image;
    if (const auto image_name = StringField(*object, "image"); image_name.has_value()) {
        image = ResolveSkinImage(skin_file, *image_name);
        if (!image.has_value()) return std::nullopt;
    }
    return SkinControlDefinition{.id = *id, .image = std::move(image)};
}

std::optional<PedalSkinDefinition> ReadPedalSkinFile(const std::filesystem::path& source) {
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source, file_error) || file_error) {
        return std::nullopt;
    }
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = ParseJson(contents);
    if (!parsed.value.has_value() || parsed.error.has_value() || parsed.value->AsObject() == nullptr) {
        return std::nullopt;
    }
    const auto& object = *parsed.value->AsObject();
    const auto schema = Find(object, "schemaVersion");
    const auto id = StringField(object, "id");
    const auto* controls = Find(object, "controls");
    if (schema == nullptr || schema->AsNumber() == nullptr || *schema->AsNumber() != 1.0 || !id.has_value() ||
        controls == nullptr || controls->AsObject() == nullptr || id->empty()) {
        return std::nullopt;
    }
    const auto knob = Find(*controls->AsObject(), "knob");
    const auto toggle = Find(*controls->AsObject(), "toggle");
    const auto button = Find(*controls->AsObject(), "button");
    const auto footswitch = Find(*controls->AsObject(), "footswitch");
    if (knob == nullptr || toggle == nullptr || button == nullptr || footswitch == nullptr) {
        return std::nullopt;
    }
    const auto knob_definition = ReadSkinControl(*knob, source);
    const auto toggle_definition = ReadSkinControl(*toggle, source);
    const auto button_definition = ReadSkinControl(*button, source);
    const auto footswitch_definition = ReadSkinControl(*footswitch, source);
    if (!knob_definition.has_value() || !toggle_definition.has_value() || !button_definition.has_value() ||
        !footswitch_definition.has_value()) {
        return std::nullopt;
    }
    return PedalSkinDefinition{
        .id = *id,
        .knob = knob_definition->id,
        .toggle = toggle_definition->id,
        .button = button_definition->id,
        .footswitch = footswitch_definition->id,
        .knob_image = knob_definition->image,
        .toggle_image = toggle_definition->image,
        .button_image = button_definition->image,
        .footswitch_image = footswitch_definition->image,
    };
}

std::vector<ModuleCatalogEntry> BuiltinCatalog() {
    return {
        {.module_id = "org.mumu-dada.solorig.drive",
         .display_name = "Drive",
         .category = "Analog",
         .description = "Built-in drive processor",
         .default_parameters = {{"drive", 4.0F}, {"outputGain", 1.0F}, {"diodeThreshold", 0.35F}},
         .latency_samples = 0U,
         .estimated_cpu_percent = 1.5},
        {.module_id = "org.mumu-dada.solorig.digital-delay",
         .display_name = "Delay",
         .category = "Digital",
         .description = "Built-in digital delay processor",
         .default_parameters = {{"time", 0.35F}, {"feedback", 0.35F}, {"mix", 0.25F}},
         .latency_samples = 96U,
         .estimated_cpu_percent = 2.5},
    };
}

std::string CategoryName(EffectCategory category) {
    switch (category) {
    case EffectCategory::AnalogCircuit:
        return "Analog";
    case EffectCategory::DigitalAlgorithm:
        return "Digital";
    case EffectCategory::NeuralAmpModel:
        return "NAM";
    case EffectCategory::ImpulseResponse:
        return "IR";
    case EffectCategory::Utility:
        return "Utility";
    }
    return "External";
}

std::string ImportedAssetModuleId(const std::filesystem::path& source,
                                  const std::vector<ModuleCatalogEntry>& catalog) {
    auto stem = source.stem().string();
    std::string safe_stem;
    safe_stem.reserve(stem.size());
    for (const auto character : stem) {
        const auto unsigned_character = static_cast<unsigned char>(character);
        if (std::isalnum(unsigned_character)) {
            safe_stem.push_back(static_cast<char>(std::tolower(unsigned_character)));
        } else if (safe_stem.empty() || safe_stem.back() != '-') {
            safe_stem.push_back('-');
        }
    }
    while (!safe_stem.empty() && safe_stem.back() == '-') safe_stem.pop_back();
    if (safe_stem.empty()) safe_stem = "asset";
    const std::string prefix = "org.mumu-dada.solorig.imported.";
    auto candidate = prefix + safe_stem;
    std::size_t suffix = 2U;
    while (std::ranges::any_of(catalog, [&candidate](const auto& entry) {
        return entry.module_id == candidate;
    })) {
        candidate = prefix + safe_stem + "-" + std::to_string(suffix++);
    }
    return candidate;
}

bool HasConnection(const RigPreset& preset, std::string_view source, std::string_view destination) {
    return std::ranges::any_of(preset.connections, [source, destination](const auto& connection) {
        return connection.source_instance_id == source && connection.destination_instance_id == destination;
    });
}

} // namespace

const PedalSkinDefinition& DefaultPedalSkin() {
    return kDefaultSkin;
}

RigPreset RigEditorViewModel::DefaultEditorPreset() {
    return DefaultRigPreset();
}

RigEditorViewModel::RigEditorViewModel(RigPreset preset)
    : layout_(DefaultDarkTheme().metrics.at("gridSize")), theme_(DefaultDarkTheme()), skin_(DefaultPedalSkin()) {
    RebuildCatalog();
    ApplyPreset(std::move(preset), std::nullopt, false);
}

void RigEditorViewModel::SetPreset(RigPreset preset) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    std::string diagnostic;
    auto packages = LoadExternalPackages(preset,
                                         source_path_.has_value() ? source_path_->parent_path() : std::filesystem::current_path(),
                                         diagnostic);
    ApplyPreset(std::move(preset), source_path_, true, packages.value_or({}));
    if (!diagnostic.empty()) {
        SetFileStatus("Preset loaded with external package placeholders: " + diagnostic);
    }
}

void RigEditorViewModel::RestoreDocument(RigPreset preset,
                                         std::optional<std::filesystem::path> source,
                                         bool dirty,
                                         std::string status) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    std::string diagnostic;
    const auto base_directory = source.has_value() ? source->parent_path() : std::filesystem::current_path();
    auto packages = LoadExternalPackages(preset, base_directory, diagnostic);
    ApplyPreset(std::move(preset), std::move(source), dirty, packages.value_or({}));
    if (!diagnostic.empty()) {
        status += " External package placeholders: " + diagnostic;
    }
    SetFileStatus(std::move(status));
}

void RigEditorViewModel::NewPreset() {
    ApplyPreset(DefaultEditorPreset(), std::nullopt, false);
    SetFileStatus("New preset created");
}

void RigEditorViewModel::ApplyPreset(RigPreset preset,
                                     std::optional<std::filesystem::path> source,
                                     bool dirty,
                                     std::map<std::string, ModulePackage, std::less<>> packages) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    preset_ = std::move(preset);
    imported_packages_ = std::move(packages);
    RebuildCatalog();
    RebuildImportedSkins();
    if (preset_.theme.has_value()) {
        theme_ = *preset_.theme;
        dark_theme_ = theme_.id.find("dark") != std::string::npos;
    } else {
        // Legacy .srig files have no theme metadata. Do not inherit a theme
        // from the previously open document; use the stable built-in fallback.
        theme_ = DefaultDarkTheme();
        dark_theme_ = true;
    }
    layout_ = PedalLayout(GridSize());
    unresolved_nodes_.clear();
    measured_performance_.clear();
    for (const auto& node : preset_.nodes) {
        const auto position = (std::isfinite(node.layout_x) && std::isfinite(node.layout_y))
            ? PedalPosition{node.layout_x, node.layout_y}
            : PedalPosition{};
        static_cast<void>(layout_.Add(node.instance_id, position));
        if (!IsKnownModule(node.module_id)) {
            unresolved_nodes_.push_back(node.instance_id);
        }
    }
    source_path_ = std::move(source);
    dirty_ = dirty;
    dragging_instance_id_.clear();
    RebuildScene();
}

RigPreset RigEditorViewModel::ExportPreset() const {
    auto result = preset_;
    result.theme = theme_;
    for (auto& node : result.nodes) {
        if (const auto position = layout_.Find(node.instance_id); position.has_value()) {
            node.layout_x = position->x;
            node.layout_y = position->y;
        }
    }
    return result;
}

EditorFileResult RigEditorViewModel::SaveTo(const std::filesystem::path& destination) {
    const auto normalized = NormalizePresetPath(destination);
    if (!normalized.has_value()) {
        SetFileStatus("Save failed: only .srig preset files are accepted.");
        return {.success = false,
                .error = EditorFileError::InvalidExtension,
                .message = file_status_};
    }
    const auto result = solo_rig::SavePreset(*normalized, ExportPreset());
    if (result.has_value()) {
        const auto error = *result;
        const auto mapped = error == PresetFileError::CannotWrite ? EditorFileError::CannotWrite
                                                                    : EditorFileError::InvalidSchema;
        SetFileStatus(error == PresetFileError::CannotWrite ? "Save failed: path is not writable."
                                                              : "Save failed: current preset is invalid.");
        return {.success = false, .error = mapped, .message = file_status_};
    }
    source_path_ = *normalized;
    dirty_ = false;
    RebuildScene();
    SetFileStatus("Saved: " + source_path_->filename().string());
    return {.success = true, .message = file_status_};
}

EditorFileResult RigEditorViewModel::LoadFrom(const std::filesystem::path& source) {
    const auto normalized = NormalizePresetPath(source);
    if (!normalized.has_value()) {
        SetFileStatus("Open failed: only .srig preset files are accepted.");
        return {.success = false,
                .error = EditorFileError::InvalidExtension,
                .message = file_status_};
    }
    const auto loaded = solo_rig::LoadPreset(*normalized);
    if (std::holds_alternative<PresetFileError>(loaded)) {
        const auto error = std::get<PresetFileError>(loaded);
        const auto mapped = MapLoadError(error);
        SetFileStatus("Open failed: " + std::string(
            error == PresetFileError::CannotRead ? "file cannot be read."
                : error == PresetFileError::InvalidJson ? "invalid JSON."
                : "unsupported or invalid preset schema."));
        return {.success = false, .error = mapped, .message = file_status_};
    }
    auto candidate = std::get<RigPreset>(loaded);
    if (HasInvalidTopology(candidate)) {
        SetFileStatus("Open failed: preset contains an invalid cyclic topology.");
        return {.success = false, .error = EditorFileError::InvalidTopology, .message = file_status_};
    }
    std::string package_diagnostic;
    auto packages = LoadExternalPackages(candidate, normalized->parent_path(), package_diagnostic);
    if (!packages.has_value()) {
        SetFileStatus("Open failed: external module package " + package_diagnostic);
        return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
    }
    ApplyPreset(std::move(candidate), *normalized, false, std::move(*packages));
    if (!unresolved_nodes_.empty()) {
        SetFileStatus("Loaded with placeholders: missing module assets for " + unresolved_nodes_.front() + ".");
        return {.success = true, .warning = true, .message = file_status_};
    }
    SetFileStatus("Loaded: " + source_path_->filename().string());
    return {.success = true, .message = file_status_};
}

EditorFileResult RigEditorViewModel::SaveCurrent() {
    if (!source_path_.has_value()) {
        SetFileStatus("Save requires a destination path.");
        return {.success = false, .error = EditorFileError::CannotWrite, .message = file_status_};
    }
    return SaveTo(*source_path_);
}

bool RigEditorViewModel::SetNodeParameter(std::string_view instance_id,
                                          std::string_view parameter_id,
                                          float value) {
    const auto node = std::ranges::find_if(preset_.nodes, [instance_id](const PresetNode& candidate) {
        return candidate.instance_id == instance_id;
    });
    if (node == preset_.nodes.end() || !std::isfinite(value)) {
        return false;
    }
    const auto parameter = node->parameter_values.find(parameter_id);
    if (parameter == node->parameter_values.end()) {
        return false;
    }
    parameter->second = value;
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    return true;
}

bool RigEditorViewModel::SetMeasuredPerformance(std::string_view instance_id,
                                                uint32_t declared_latency_samples,
                                                double measured_cpu_percent,
                                                double peak_cpu_percent) {
    if (instance_id.empty() || !std::isfinite(measured_cpu_percent) ||
        !std::isfinite(peak_cpu_percent) || measured_cpu_percent < 0.0 ||
        peak_cpu_percent < measured_cpu_percent) {
        return false;
    }
    const auto node = std::ranges::find_if(preset_.nodes, [instance_id](const PresetNode& candidate) {
        return candidate.instance_id == instance_id;
    });
    if (node == preset_.nodes.end()) {
        return false;
    }
    measured_performance_[std::string(instance_id)] = {
        .declared_latency_samples = declared_latency_samples,
        .measured_cpu_percent = measured_cpu_percent,
        .peak_cpu_percent = peak_cpu_percent,
    };
    RebuildScene();
    return true;
}

void RigEditorViewModel::ClearMeasuredPerformance() {
    measured_performance_.clear();
    RebuildScene();
}

std::optional<std::vector<MidiBinding>> RigEditorViewModel::MidiBindings() const {
    return preset_.midi_bindings;
}

bool RigEditorViewModel::SetMidiBindings(std::vector<MidiBinding> bindings) {
    auto candidate = preset_;
    candidate.midi_bindings = bindings;
    if (!ValidatePreset(candidate).empty()) {
        return false;
    }
    preset_.midi_bindings = std::move(bindings);
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::IsDirty() const noexcept {
    return dirty_;
}

std::optional<std::filesystem::path> RigEditorViewModel::SourcePath() const {
    return source_path_;
}

std::string_view RigEditorViewModel::FileStatus() const noexcept {
    return file_status_;
}

bool RigEditorViewModel::LoadThemeFile(const std::filesystem::path& source, bool mark_dirty) {
    const auto loaded = solo_rig::LoadThemeFile(source);
    if (std::holds_alternative<ThemeDefinition>(loaded)) {
        SetTheme(std::get<ThemeDefinition>(loaded), mark_dirty);
        return true;
    }
    // Keep the current valid theme when an external resource is missing or
    // malformed; the scene remains renderable with built-in defaults.
    return false;
}

void RigEditorViewModel::SetThemeMode(bool dark) {
    dark_theme_ = dark;
    theme_ = dark ? DefaultDarkTheme() : DefaultLightTheme();
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
}

void RigEditorViewModel::SetTheme(ThemeDefinition theme, bool mark_dirty) {
    if (theme.id.empty() || theme.colors.empty()) {
        return;
    }
    dark_theme_ = theme.id.find("dark") != std::string::npos;
    theme_ = std::move(theme);
    if (mark_dirty) {
        dirty_ = true;
        SetFileStatus("Unsaved changes");
    }
    RebuildScene();
}

bool RigEditorViewModel::IsDarkTheme() const noexcept {
    return dark_theme_;
}

bool RigEditorViewModel::LoadSkinFile(const std::filesystem::path& source) {
    const auto skin = ReadPedalSkinFile(source);
    if (!skin.has_value()) {
        return false;
    }
    skin_ = *skin;
    RebuildScene();
    return true;
}

std::vector<ModuleCatalogEntry> RigEditorViewModel::ModuleCatalog() {
    return BuiltinCatalog();
}

const std::vector<ModuleCatalogEntry>& RigEditorViewModel::Catalog() const noexcept {
    return catalog_;
}

void RigEditorViewModel::RebuildCatalog() {
    catalog_ = BuiltinCatalog();
    for (const auto& [module_id, package] : imported_packages_) {
        ModuleCatalogEntry entry{
            .module_id = module_id,
            .display_name = package.descriptor.display_name,
            .category = CategoryName(package.descriptor.category),
            .description = "External module package: " + package.descriptor.logic_entry_point,
            .latency_samples = package.descriptor.declared_latency_samples,
            .estimated_cpu_percent = package.descriptor.category == EffectCategory::AnalogCircuit ? 4.0 : 2.0,
        };
        entry.default_parameters.reserve(package.descriptor.parameters.size());
        for (const auto& parameter : package.descriptor.parameters) {
            entry.default_parameters.emplace_back(parameter.id, parameter.default_value);
        }
        catalog_.push_back(std::move(entry));
    }
}

bool RigEditorViewModel::AddNode(std::string_view module_id, std::string_view instance_id,
                                 double layout_x, double layout_y) {
    const auto* entry = FindCatalogEntry(module_id);
    if (entry == nullptr || instance_id.empty() || !std::isfinite(layout_x) || !std::isfinite(layout_y) ||
        std::ranges::any_of(preset_.nodes, [instance_id](const auto& node) { return node.instance_id == instance_id; })) {
        SetFileStatus("Edit failed: invalid or duplicate module node.");
        return false;
    }
    auto candidate = preset_;
    PresetNode node{.instance_id = std::string(instance_id),
                    .module_id = std::string(module_id),
                    .layout_x = layout_x,
                    .layout_y = layout_y};
    for (const auto& [parameter, value] : entry->default_parameters) {
        node.parameter_values.emplace(parameter, value);
    }
    candidate.nodes.push_back(std::move(node));
    if (!ValidatePreset(candidate).empty() || HasInvalidTopology(candidate)) {
        SetFileStatus("Edit failed: candidate preset validation rejected the node.");
        return false;
    }
    preset_ = std::move(candidate);
    static_cast<void>(layout_.Add(std::string(instance_id), {.x = layout_x, .y = layout_y}));
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::DeleteNode(std::string_view instance_id) {
    auto candidate = preset_;
    const auto iterator = std::ranges::find_if(candidate.nodes, [instance_id](const auto& node) {
        return node.instance_id == instance_id;
    });
    if (iterator == candidate.nodes.end()) {
        SetFileStatus("Edit failed: node was not found.");
        return false;
    }
    candidate.nodes.erase(iterator);
    std::erase_if(candidate.connections, [instance_id](const auto& connection) {
        return connection.source_instance_id == instance_id || connection.destination_instance_id == instance_id;
    });
    if (!ValidatePreset(candidate).empty() || HasInvalidTopology(candidate)) {
        SetFileStatus("Edit failed: candidate preset validation rejected node deletion.");
        return false;
    }
    preset_ = std::move(candidate);
    static_cast<void>(layout_.Remove(std::string(instance_id)));
    if (dragging_instance_id_ == instance_id) {
        dragging_instance_id_.clear();
    }
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::DuplicateNode(std::string_view source_instance_id, std::string_view new_instance_id) {
    const auto source = std::ranges::find_if(preset_.nodes, [source_instance_id](const auto& node) {
        return node.instance_id == source_instance_id;
    });
    if (source == preset_.nodes.end() || new_instance_id.empty() ||
        std::ranges::any_of(preset_.nodes, [new_instance_id](const auto& node) { return node.instance_id == new_instance_id; })) {
        SetFileStatus("Edit failed: source node or new instance ID is invalid.");
        return false;
    }
    auto candidate = preset_;
    auto copy = *source;
    copy.instance_id = std::string(new_instance_id);
    copy.layout_x += GridSize() * 2.0;
    candidate.nodes.push_back(copy);
    if (!ValidatePreset(candidate).empty() || HasInvalidTopology(candidate)) {
        SetFileStatus("Edit failed: candidate preset validation rejected node duplication.");
        return false;
    }
    preset_ = std::move(candidate);
    static_cast<void>(layout_.Add(copy.instance_id, {.x = copy.layout_x, .y = copy.layout_y}));
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::ConnectNodes(std::string_view source_instance_id, std::string_view destination_instance_id) {
    if (source_instance_id.empty() || destination_instance_id.empty() || source_instance_id == destination_instance_id ||
        !std::ranges::any_of(preset_.nodes, [source_instance_id](const auto& node) { return node.instance_id == source_instance_id; }) ||
        !std::ranges::any_of(preset_.nodes, [destination_instance_id](const auto& node) { return node.instance_id == destination_instance_id; }) ||
        HasConnection(preset_, source_instance_id, destination_instance_id)) {
        SetFileStatus("Connection failed: endpoints are invalid or already connected.");
        return false;
    }
    auto candidate = preset_;
    candidate.connections.push_back({.source_instance_id = std::string(source_instance_id),
                                      .destination_instance_id = std::string(destination_instance_id)});
    if (!ValidatePreset(candidate).empty() || HasInvalidTopology(candidate)) {
        SetFileStatus("Connection failed: cyclic or invalid topology.");
        return false;
    }
    preset_ = std::move(candidate);
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::DisconnectNodes(std::string_view source_instance_id, std::string_view destination_instance_id) {
    if (!HasConnection(preset_, source_instance_id, destination_instance_id)) {
        SetFileStatus("Connection failed: connection was not found.");
        return false;
    }
    auto candidate = preset_;
    std::erase_if(candidate.connections, [source_instance_id, destination_instance_id](const auto& connection) {
        return connection.source_instance_id == source_instance_id && connection.destination_instance_id == destination_instance_id;
    });
    if (!ValidatePreset(candidate).empty() || HasInvalidTopology(candidate)) {
        SetFileStatus("Connection failed: candidate topology is invalid.");
        return false;
    }
    preset_ = std::move(candidate);
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

EditorFileResult RigEditorViewModel::ImportResource(const std::filesystem::path& source) {
    std::error_code path_error;
    const auto is_directory = std::filesystem::is_directory(source, path_error);
    const auto is_manifest = !is_directory && source.filename() == "manifest.json" &&
        std::filesystem::is_regular_file(source, path_error);
    if (is_directory || is_manifest) {
        const auto package_root = is_directory ? source : source.parent_path();
        const auto loaded = LoadModulePackage(package_root);
        if (!loaded.package.has_value() || !loaded.errors.empty()) {
            SetFileStatus("Resource import failed: module package manifest or logic is invalid.");
            return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
        }
        const auto module_id = loaded.package->descriptor.module_id;
        if (FindCatalogEntry(module_id) != nullptr) {
            SetFileStatus("Resource import failed: module ID is already registered.");
            return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
        }
        std::error_code absolute_error;
        const auto absolute_root = std::filesystem::absolute(package_root, absolute_error).lexically_normal();
        if (absolute_error || absolute_root.empty()) {
            SetFileStatus("Resource import failed: package path cannot be normalized.");
            return {.success = false, .error = EditorFileError::CannotRead, .message = file_status_};
        }
        auto candidate = preset_;
        candidate.external_module_packages.push_back({.module_id = module_id, .root_directory = absolute_root});
        if (!ValidatePreset(candidate).empty()) {
            SetFileStatus("Resource import failed: package reference could not be added to the preset.");
            return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
        }
        auto package = std::move(*loaded.package);
        const auto relative_skin = package.custom_skin_path.has_value()
            ? package.custom_skin_path->lexically_relative(package.root_directory)
            : std::filesystem::path{};
        const auto display_name = package.descriptor.display_name;
        package.root_directory = absolute_root;
        package.logic_path = absolute_root / package.descriptor.logic_entry_point;
        if (!relative_skin.empty()) {
            package.custom_skin_path = absolute_root / relative_skin;
        }
        imported_packages_.emplace(module_id, std::move(package));
        preset_ = std::move(candidate);
        RebuildCatalog();
        RebuildImportedSkins();
        resource_status_ = "Imported module package: " + display_name;
        dirty_ = true;
        SetFileStatus("Unsaved changes");
        RebuildScene();
        return {.success = true, .message = resource_status_};
    }
    std::string extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::string label;
    ExternalModuleSourceKind source_kind = ExternalModuleSourceKind::PackageDirectory;
    if (extension == ".nam") {
        label = "NAM";
        source_kind = ExternalModuleSourceKind::NeuralAmpModel;
    } else if (extension == ".nam-a2") {
        label = "NAM A2";
        source_kind = ExternalModuleSourceKind::NeuralAmpModel;
    } else if (extension == ".ir" || extension == ".wav") {
        label = "IR";
        source_kind = ExternalModuleSourceKind::ImpulseResponse;
    } else if (extension == ".json") {
        label = "external circuit";
        source_kind = ExternalModuleSourceKind::AnalogCircuitFile;
    } else {
        SetFileStatus("Resource import failed: unsupported resource type.");
        return {.success = false, .error = EditorFileError::InvalidExtension, .message = file_status_};
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        SetFileStatus("Resource import failed: file cannot be read.");
        return {.success = false, .error = EditorFileError::CannotRead, .message = file_status_};
    }
    if (source_kind != ExternalModuleSourceKind::PackageDirectory) {
        std::error_code absolute_error;
        const auto absolute_asset = std::filesystem::absolute(source, absolute_error).lexically_normal();
        if (absolute_error || absolute_asset.empty()) {
            SetFileStatus("Resource import failed: asset path cannot be normalized.");
            return {.success = false, .error = EditorFileError::CannotRead, .message = file_status_};
        }
        const auto module_id = ImportedAssetModuleId(absolute_asset, catalog_);
        const auto loaded = LoadExternalModuleAsset(absolute_asset, source_kind, module_id,
                                                    label + ": " + source.filename().string());
        if (!loaded.package.has_value() || !loaded.errors.empty()) {
            SetFileStatus("Resource import failed: " + label + " asset is invalid or unsupported.");
            return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
        }
        auto candidate = preset_;
        candidate.external_module_packages.push_back({.module_id = module_id,
                                                       .root_directory = absolute_asset.parent_path(),
                                                       .source_kind = source_kind,
                                                       .asset_path = absolute_asset});
        if (!ValidatePreset(candidate).empty()) {
            SetFileStatus("Resource import failed: asset reference could not be added to the preset.");
            return {.success = false, .error = EditorFileError::InvalidSchema, .message = file_status_};
        }
        imported_packages_.emplace(module_id, std::move(*loaded.package));
        preset_ = std::move(candidate);
        RebuildCatalog();
        RebuildImportedSkins();
        resource_status_ = "Imported " + label + " module: " + module_id;
    } else {
        resource_status_ = "Imported " + label + ": " + source.filename().string();
    }
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return {.success = true, .message = resource_status_};
}

void RigEditorViewModel::SetViewport(double width, double height, double dpi_scale) {
    viewport_width_ = std::max(width, 1.0);
    viewport_height_ = std::max(height, 1.0);
    dpi_scale_ = std::isfinite(dpi_scale) && dpi_scale > 0.0 ? dpi_scale : 1.0;
    RebuildScene();
}

const RigEditorScene& RigEditorViewModel::Scene() const noexcept {
    return scene_;
}

std::optional<std::string> RigEditorViewModel::HitTest(EditorPoint point) const {
    for (auto iterator = scene_.nodes.rbegin(); iterator != scene_.nodes.rend(); ++iterator) {
        if (iterator->bounds.Contains(point)) {
            return iterator->instance_id;
        }
    }
    return std::nullopt;
}

bool RigEditorViewModel::SelectAt(EditorPoint point) {
    const auto hit = HitTest(point);
    for (auto& node : scene_.nodes) {
        node.selected = hit.has_value() && node.instance_id == *hit;
    }
    return hit.has_value();
}

bool RigEditorViewModel::BeginDrag(EditorPoint point) {
    const auto hit = HitTest(point);
    if (!hit.has_value()) {
        static_cast<void>(SelectAt(point));
        return false;
    }
    static_cast<void>(SelectAt(point));
    const auto* node = FindSceneNode(*hit);
    if (node == nullptr) {
        return false;
    }
    dragging_instance_id_ = *hit;
    drag_offset_ = {.x = point.x / dpi_scale_ - node->bounds.x / dpi_scale_,
                    .y = point.y / dpi_scale_ - node->bounds.y / dpi_scale_};
    return true;
}

bool RigEditorViewModel::DragTo(EditorPoint point, bool snap_to_grid) {
    if (dragging_instance_id_.empty()) {
        return false;
    }
    const auto* node = FindSceneNode(dragging_instance_id_);
    if (node == nullptr) {
        return false;
    }
    const auto position = PedalPosition{.x = point.x / dpi_scale_ - drag_offset_.x,
                                        .y = point.y / dpi_scale_ - drag_offset_.y};
    if (!layout_.Move(dragging_instance_id_, position, snap_to_grid)) {
        return false;
    }
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

void RigEditorViewModel::EndDrag() noexcept {
    dragging_instance_id_.clear();
}

void RigEditorViewModel::ToggleTheme() noexcept {
    SetThemeMode(!dark_theme_);
}

bool RigEditorViewModel::IsDragging() const noexcept {
    return !dragging_instance_id_.empty();
}

std::string_view RigEditorViewModel::AudioStatus() const noexcept {
    return scene_.audio_status;
}

void RigEditorViewModel::SetAudioStatus(std::string status) {
    audio_status_ = std::move(status);
    scene_.audio_status = audio_status_;
}

std::optional<std::map<std::string, ModulePackage, std::less<>>>
RigEditorViewModel::LoadExternalPackages(RigPreset& preset,
                                          const std::filesystem::path& base_directory,
                                          std::string& diagnostic) const {
    std::map<std::string, ModulePackage, std::less<>> packages;
    const auto builtins = BuiltinCatalog();
    for (auto& reference : preset.external_module_packages) {
        if (reference.module_id.empty() || reference.root_directory.empty()) {
            diagnostic = "reference is missing module ID or root directory";
            return std::nullopt;
        }
        if (std::ranges::any_of(builtins, [&reference](const auto& entry) {
                return entry.module_id == reference.module_id;
            }) || packages.contains(reference.module_id)) {
            diagnostic = "module ID is duplicated: " + reference.module_id;
            return std::nullopt;
        }
        auto root = reference.root_directory;
        auto asset = reference.asset_path;
        if (root.is_relative()) root = base_directory / root;
        if (asset.is_relative()) asset = base_directory / asset;
        std::error_code absolute_error;
        root = std::filesystem::absolute(root, absolute_error).lexically_normal();
        if (absolute_error || root.empty()) {
            diagnostic = "path cannot be normalized for " + reference.module_id;
            return std::nullopt;
        }
        const auto loaded = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
            ? LoadModulePackage(root)
            : LoadExternalModuleAsset(asset, reference.source_kind, reference.module_id, {});
        if (!loaded.package.has_value() || !loaded.errors.empty()) {
            diagnostic = "could not validate " + reference.module_id + " at " +
                (reference.source_kind == ExternalModuleSourceKind::PackageDirectory ? root : asset).string();
            return std::nullopt;
        }
        if (loaded.package->descriptor.module_id != reference.module_id) {
            diagnostic = "manifest ID does not match reference for " + reference.module_id;
            return std::nullopt;
        }
        auto package = std::move(*loaded.package);
        const auto relative_skin = package.custom_skin_path.has_value()
            ? package.custom_skin_path->lexically_relative(package.root_directory)
            : std::filesystem::path{};
        package.root_directory = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
            ? root : asset.parent_path();
        package.logic_path = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
            ? root / package.descriptor.logic_entry_point : asset;
        if (reference.source_kind != ExternalModuleSourceKind::PackageDirectory) {
            if (auto* model = std::get_if<NeuralAmpModelLogic>(&package.logic)) model->model_path = asset;
            if (auto* impulse = std::get_if<ImpulseResponseLogic>(&package.logic)) impulse->impulse_response_path = asset;
        }
        if (!relative_skin.empty()) {
            package.custom_skin_path = root / relative_skin;
        }
        reference.root_directory = package.root_directory;
        if (reference.source_kind != ExternalModuleSourceKind::PackageDirectory) reference.asset_path = asset;
        packages.emplace(reference.module_id, std::move(package));
    }
    return packages;
}

void RigEditorViewModel::RebuildScene() {
    scene_.viewport_width = viewport_width_;
    scene_.viewport_height = viewport_height_;
    scene_.dpi_scale = dpi_scale_;
    scene_.theme = theme_;
    scene_.skin = skin_;
    scene_.audio_status = audio_status_;
    scene_.file_status = file_status_;
    scene_.source_path = source_path_.has_value() ? source_path_->string() : std::string{};
    scene_.resource_status = resource_status_;
    scene_.performance = {};
    scene_.performance.measured_node_count = 0U;
    for (const auto& node : preset_.nodes) {
        if (const auto* entry = FindCatalogEntry(node.module_id); entry != nullptr) {
            scene_.performance.total_latency_samples += entry->latency_samples;
            scene_.performance.estimated_cpu_percent += entry->estimated_cpu_percent;
        }
        if (const auto measured = measured_performance_.find(node.instance_id);
            measured != measured_performance_.end()) {
            scene_.performance.measured_cpu_percent += measured->second.measured_cpu_percent;
            scene_.performance.peak_cpu_percent += measured->second.peak_cpu_percent;
            ++scene_.performance.measured_node_count;
        }
    }
    scene_.performance.measurement_available = !preset_.nodes.empty() &&
        static_cast<std::size_t>(scene_.performance.measured_node_count) == preset_.nodes.size();
    scene_.dirty = dirty_;
    scene_.nodes.clear();
    scene_.connections.clear();
    constexpr double kNodeWidth = 196.0;
    constexpr double kNodeHeight = 126.0;
    for (const auto& node : preset_.nodes) {
        const auto position = layout_.Find(node.instance_id).value_or(PedalPosition{});
        const auto* entry = FindCatalogEntry(node.module_id);
        const auto imported_skin = imported_skins_.find(node.module_id);
        const auto& node_skin = imported_skin == imported_skins_.end() ? skin_ : imported_skin->second;
        const auto measured = measured_performance_.find(node.instance_id);
        scene_.nodes.push_back({.instance_id = node.instance_id,
                                .module_id = node.module_id,
                                .skin = node_skin,
                                .bounds = {.x = position.x * dpi_scale_,
                                           .y = position.y * dpi_scale_,
                                           .width = kNodeWidth * dpi_scale_,
                                           .height = kNodeHeight * dpi_scale_},
                                .selected = node.instance_id == dragging_instance_id_,
                                .unresolved = !IsKnownModule(node.module_id),
                                .diagnostic = !IsKnownModule(node.module_id) ? "Missing module asset" : std::string{},
                                .declared_latency_samples = measured == measured_performance_.end()
                                    ? (entry == nullptr ? 0U : entry->latency_samples)
                                    : measured->second.declared_latency_samples,
                                .estimated_cpu_percent = entry == nullptr ? 0.0 : entry->estimated_cpu_percent,
                                .measured_cpu_percent = measured == measured_performance_.end()
                                    ? 0.0 : measured->second.measured_cpu_percent,
                                .peak_cpu_percent = measured == measured_performance_.end()
                                    ? 0.0 : measured->second.peak_cpu_percent,
                                .measurement_available = measured != measured_performance_.end()});
    }
    for (const auto& connection : preset_.connections) {
        const auto* source = FindSceneNode(connection.source_instance_id);
        const auto* destination = FindSceneNode(connection.destination_instance_id);
        if (source != nullptr && destination != nullptr) {
            scene_.connections.push_back({.source_instance_id = connection.source_instance_id,
                                          .destination_instance_id = connection.destination_instance_id,
                                          .source = {.x = source->bounds.x + source->bounds.width,
                                                      .y = source->bounds.y + source->bounds.height * 0.5},
                                          .destination = {.x = destination->bounds.x,
                                                          .y = destination->bounds.y + destination->bounds.height * 0.5}});
        }
    }
}

void RigEditorViewModel::RebuildImportedSkins() {
    imported_skins_.clear();
    for (const auto& [module_id, package] : imported_packages_) {
        if (!package.custom_skin_path.has_value()) {
            continue;
        }
        if (const auto skin = ReadPedalSkinFile(*package.custom_skin_path); skin.has_value()) {
            imported_skins_.emplace(module_id, *skin);
        }
    }
}

void RigEditorViewModel::SetFileStatus(std::string status) {
    file_status_ = std::move(status);
    scene_.file_status = file_status_;
    scene_.dirty = dirty_;
}

std::optional<std::filesystem::path> RigEditorViewModel::NormalizePresetPath(
    const std::filesystem::path& source) {
    auto extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (source.empty() || extension != ".srig") {
        return std::nullopt;
    }
    std::error_code error;
    auto normalized = std::filesystem::absolute(source, error);
    if (error) {
        return std::nullopt;
    }
    return normalized.lexically_normal();
}

std::optional<EditorFileError> RigEditorViewModel::MapLoadError(PresetFileError error) {
    switch (error) {
    case PresetFileError::CannotRead:
        return EditorFileError::CannotRead;
    case PresetFileError::InvalidJson:
        return EditorFileError::InvalidJson;
    case PresetFileError::InvalidSchema:
    case PresetFileError::InvalidPreset:
    case PresetFileError::CannotWrite:
        return EditorFileError::InvalidSchema;
    }
    return EditorFileError::InvalidSchema;
}

bool RigEditorViewModel::HasInvalidTopology(const RigPreset& preset) {
    std::map<std::string, std::vector<std::string>, std::less<>> graph;
    for (const auto& node : preset.nodes) {
        graph[node.instance_id];
    }
    for (const auto& connection : preset.connections) {
        graph[connection.source_instance_id].push_back(connection.destination_instance_id);
    }
    std::map<std::string, int, std::less<>> colors;
    const auto visit = [&graph, &colors](const auto& self, const std::string& node) -> bool {
        colors[node] = 1;
        for (const auto& destination : graph[node]) {
            if (colors[destination] == 1 || (colors[destination] == 0 && self(self, destination))) {
                return true;
            }
        }
        colors[node] = 2;
        return false;
    };
    for (const auto& [node, unused] : graph) {
        static_cast<void>(unused);
        if (colors[node] == 0 && visit(visit, node)) {
            return true;
        }
    }
    return false;
}

bool RigEditorViewModel::IsKnownModule(std::string_view module_id) const noexcept {
    return FindCatalogEntry(module_id) != nullptr;
}

const ModuleCatalogEntry* RigEditorViewModel::FindCatalogEntry(std::string_view module_id) const noexcept {
    const auto iterator = std::ranges::find_if(catalog_, [module_id](const auto& entry) {
        return entry.module_id == module_id;
    });
    return iterator == catalog_.end() ? nullptr : &*iterator;
}

double RigEditorViewModel::GridSize() const noexcept {
    const auto metric = theme_.metrics.find("gridSize");
    return metric != theme_.metrics.end() && metric->second > 0.0 ? metric->second : 16.0;
}

const RigEditorNode* RigEditorViewModel::FindSceneNode(std::string_view instance_id) const noexcept {
    const auto iterator = std::ranges::find_if(scene_.nodes, [instance_id](const RigEditorNode& node) {
        return node.instance_id == instance_id;
    });
    return iterator == scene_.nodes.end() ? nullptr : &*iterator;
}

} // namespace solo_rig
