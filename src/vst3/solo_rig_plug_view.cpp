#include "vst3/solo_rig_plug_view.h"

#include "pluginterfaces/gui/iplugview.h"

#include <windows.h>

#include <filesystem>
#include <optional>
#include <cstring>
#include <string>
#include <vector>

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

namespace solo_rig::vst3 {

using UiStart = void* (CORECLR_DELEGATE_CALLTYPE*)(void* parent, int width, int height);
using UiResize = int (CORECLR_DELEGATE_CALLTYPE*)(int width, int height);
using UiStop = void (CORECLR_DELEGATE_CALLTYPE*)();

class ManagedUiBridge final {
public:
    ~ManagedUiBridge() {
        Stop();
        if (runtime_context_ != nullptr && close_runtime_ != nullptr) {
            close_runtime_(runtime_context_);
        }
        if (hostfxr_ != nullptr) {
            FreeLibrary(hostfxr_);
        }
        if (nethost_ != nullptr) {
            FreeLibrary(nethost_);
        }
    }

    [[nodiscard]] bool Start(void* parent, int width, int height) {
        if (!Load()) {
            return false;
        }
        if (start_ == nullptr) {
            return false;
        }
        window_ = start_(parent, width, height);
        return window_ != nullptr;
    }

    void Stop() noexcept {
        if (window_ != nullptr && stop_ != nullptr) {
            stop_();
            window_ = nullptr;
        }
    }

    [[nodiscard]] bool Resize(int width, int height) noexcept {
        return window_ != nullptr && resize_ != nullptr && resize_(width, height) != 0;
    }

private:
    [[nodiscard]] bool Load() {
        if (start_ != nullptr) {
            return true;
        }
        const auto managed_directory = FindManagedUiDirectory();
        if (!managed_directory.has_value()) {
            return false;
        }
        const auto runtime_config = *managed_directory / L"SoloRig.Ui.Avalonia.runtimeconfig.json";
        const auto assembly = *managed_directory / L"SoloRig.Ui.Avalonia.dll";
        if (!std::filesystem::is_regular_file(runtime_config) ||
            !std::filesystem::is_regular_file(assembly)) {
            return false;
        }

        if (!LoadNethost()) {
            return false;
        }
        char_t hostfxr_path[4096]{};
        size_t hostfxr_path_size = std::size(hostfxr_path);
        if (get_hostfxr_path_(hostfxr_path, &hostfxr_path_size, nullptr) != 0) {
            return false;
        }
        hostfxr_ = LoadLibraryW(hostfxr_path);
        if (hostfxr_ == nullptr) {
            return false;
        }
        const auto initialize = GetFunction<hostfxr_initialize_for_runtime_config_fn>(
            "hostfxr_initialize_for_runtime_config");
        const auto get_delegate = GetFunction<hostfxr_get_runtime_delegate_fn>(
            "hostfxr_get_runtime_delegate");
        close_runtime_ = GetFunction<hostfxr_close_fn>("hostfxr_close");
        if (initialize == nullptr || get_delegate == nullptr || close_runtime_ == nullptr) {
            return false;
        }
        if (initialize(runtime_config.c_str(), nullptr, &runtime_context_) != 0 ||
            runtime_context_ == nullptr) {
            return false;
        }
        load_assembly_ = nullptr;
        if (get_delegate(runtime_context_, hdt_load_assembly_and_get_function_pointer,
                         reinterpret_cast<void**>(&load_assembly_)) != 0 ||
            load_assembly_ == nullptr) {
            return false;
        }
        if (!LoadMethod(assembly, L"Start", reinterpret_cast<void**>(&start_)) ||
            !LoadMethod(assembly, L"Resize", reinterpret_cast<void**>(&resize_)) ||
            !LoadMethod(assembly, L"Stop", reinterpret_cast<void**>(&stop_))) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool LoadMethod(const std::filesystem::path& assembly,
                                  const wchar_t* method,
                                  void** destination) const {
        std::wstring type_name = L"SoloRig.Ui.Avalonia.Vst3UiBridge, SoloRig.Ui.Avalonia";
        return load_assembly_(assembly.c_str(), type_name.c_str(), method,
                              UNMANAGEDCALLERSONLY_METHOD, nullptr, destination) == 0 &&
               *destination != nullptr;
    }

    template <typename Function>
    [[nodiscard]] Function GetFunction(const char* name) const {
        return reinterpret_cast<Function>(GetProcAddress(hostfxr_, name));
    }

    [[nodiscard]] bool LoadNethost() {
        if (get_hostfxr_path_ != nullptr) {
            return true;
        }
        const auto managed_directory = FindManagedUiDirectory();
        const auto module_directory = [] {
            wchar_t module_path[4096]{};
            const auto length = GetModuleFileNameW(nullptr, module_path, std::size(module_path));
            return length == 0 || length >= std::size(module_path)
                ? std::filesystem::path{}
                : std::filesystem::path(module_path).parent_path();
        }();
        std::vector<std::filesystem::path> candidates;
        if (managed_directory.has_value()) {
            candidates.push_back(*managed_directory / L"nethost.dll");
        }
        if (!module_directory.empty()) {
            candidates.push_back(module_directory / L"nethost.dll");
        }
#ifdef SOLO_RIG_DOTNET_HOST_NATIVE_DIR
        candidates.emplace_back(std::filesystem::path(SOLO_RIG_DOTNET_HOST_NATIVE_DIR) / L"nethost.dll");
#endif
        for (const auto& candidate : candidates) {
            nethost_ = LoadLibraryW(candidate.c_str());
            if (nethost_ != nullptr) {
                break;
            }
        }
        if (nethost_ == nullptr) {
            nethost_ = LoadLibraryW(L"nethost.dll");
        }
        if (nethost_ == nullptr) {
            return false;
        }
        get_hostfxr_path_ = reinterpret_cast<GetHostFxrPath>(
            GetProcAddress(nethost_, "get_hostfxr_path"));
        return get_hostfxr_path_ != nullptr;
    }

    [[nodiscard]] static std::optional<std::filesystem::path> FindManagedUiDirectory() {
        if (wchar_t environment[4096]{}; GetEnvironmentVariableW(
                L"SOLO_RIG_VST3_UI_DIR", environment, std::size(environment)) > 0) {
            const std::filesystem::path candidate(environment);
            if (std::filesystem::is_directory(candidate)) {
                return candidate;
            }
        }

        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&ManagedUiBridge::FindManagedUiDirectory),
                                &module)) {
            return std::nullopt;
        }
        wchar_t module_path[4096]{};
        const auto length = GetModuleFileNameW(module, module_path, std::size(module_path));
        if (length == 0 || length >= std::size(module_path)) {
            return std::nullopt;
        }
        const auto executable_directory = std::filesystem::path(module_path).parent_path();
        const std::vector<std::filesystem::path> candidates{
            executable_directory / L"managed" / L"vst3-ui",
            executable_directory / L"..\\managed\\vst3-ui",
            executable_directory / L"..\\Resources\\managed",
            executable_directory / L"..\\..\\managed\\vst3-ui",
            executable_directory / L"..\\..\\..\\managed\\vst3-ui",
            executable_directory / L"..\\..\\..\\..\\managed\\vst3-ui",
        };
        for (const auto& candidate : candidates) {
            const auto normalized = std::filesystem::weakly_canonical(candidate);
            if (std::filesystem::is_directory(normalized)) {
                return normalized;
            }
        }
        return std::nullopt;
    }

    HMODULE hostfxr_{};
    HMODULE nethost_{};
    hostfxr_handle runtime_context_{};
    using GetHostFxrPath = int (NETHOST_CALLTYPE*)(char_t*, size_t*, const get_hostfxr_parameters*);
    GetHostFxrPath get_hostfxr_path_{};
    hostfxr_close_fn close_runtime_{};
    load_assembly_and_get_function_pointer_fn load_assembly_{};
    UiStart start_{};
    UiResize resize_{};
    UiStop stop_{};
    void* window_{};
};

SoloRigPlugView::SoloRigPlugView()
    : managed_bridge_(std::make_unique<ManagedUiBridge>()) {
    rect = Steinberg::ViewRect{0, 0, 960, 640};
}

SoloRigPlugView::~SoloRigPlugView() = default;

Steinberg::tresult PLUGIN_API SoloRigPlugView::isPlatformTypeSupported(
    Steinberg::FIDString type) {
    return type != nullptr && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0
        ? Steinberg::kResultTrue
        : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API SoloRigPlugView::attached(
    void* parent, Steinberg::FIDString type) {
    if (parent == nullptr || isPlatformTypeSupported(type) != Steinberg::kResultTrue) {
        return Steinberg::kInvalidArgument;
    }
    const auto result = CPluginView::attached(parent, type);
    if (result != Steinberg::kResultOk ||
        !managed_bridge_->Start(parent, rect.getWidth(), rect.getHeight())) {
        CPluginView::removed();
        return Steinberg::kResultFalse;
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigPlugView::removed() {
    managed_bridge_->Stop();
    return CPluginView::removed();
}

Steinberg::tresult PLUGIN_API SoloRigPlugView::onSize(Steinberg::ViewRect* new_size) {
    const auto result = CPluginView::onSize(new_size);
    if (result != Steinberg::kResultTrue || new_size == nullptr) {
        return result;
    }
    static_cast<void>(managed_bridge_->Resize(new_size->getWidth(), new_size->getHeight()));
    return Steinberg::kResultTrue;
}

} // namespace solo_rig::vst3
