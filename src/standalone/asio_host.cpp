#include "standalone/asio_host.hpp"
#include "standalone/asio_sample_conversion.hpp"

#include <objbase.h>
#include <winreg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace solo_rig::standalone {
namespace {

using asio_abi::ASIOError;
using asio_abi::ASIOSampleRate;
using asio_abi::IASIO;

std::array<std::atomic<AsioHost*>, AsioHost::kMaximumActiveHosts> g_active_hosts{};

std::optional<std::wstring> ReadRegistryString(HKEY key, std::wstring_view value_name) {
    DWORD type = 0U;
    DWORD byte_count = 0U;
    const auto first = RegQueryValueExW(key,
                                        value_name.empty() ? nullptr : value_name.data(),
                                        nullptr,
                                        &type,
                                        nullptr,
                                        &byte_count);
    if (first != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byte_count == 0U) {
        return std::nullopt;
    }
    std::wstring value(byte_count / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key,
                         value_name.empty() ? nullptr : value_name.data(),
                         nullptr,
                         &type,
                         reinterpret_cast<LPBYTE>(value.data()),
                         &byte_count) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value.empty() ? std::nullopt : std::optional<std::wstring>(std::move(value));
}

std::string Utf8FromWide(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const auto length = WideCharToMultiByte(CP_UTF8,
                                            WC_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr,
                                            0,
                                            nullptr,
                                            nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            value.data(),
                            static_cast<int>(value.size()),
                            result.data(),
                            length,
                            nullptr,
                            nullptr) != length) {
        return {};
    }
    return result;
}

std::string GuidText(const GUID& guid) {
    wchar_t buffer[40]{};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
        return {};
    }
    return Utf8FromWide(buffer);
}

std::optional<GUID> GuidFromText(std::wstring_view value) {
    GUID result{};
    std::wstring copy(value);
    if (CLSIDFromString(copy.data(), &result) != S_OK) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::wstring> DriverDllPath(const GUID& class_id) {
    const auto class_id_text = GuidText(class_id);
    if (class_id_text.empty()) {
        return std::nullopt;
    }
    std::wstring path = L"CLSID\\";
    path += std::wstring(class_id_text.begin(), class_id_text.end());
    path += L"\\InprocServer32";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path.c_str(), 0U, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    const auto result = ReadRegistryString(key, {});
    RegCloseKey(key);
    return result;
}

std::string AsioDriverError(IASIO* driver, std::string_view operation, ASIOError error) {
    std::array<char, 256> message{};
    if (driver != nullptr) {
        driver->getErrorMessage(message.data());
    }
    std::string result(operation);
    result += " failed (ASIO error ";
    result += std::to_string(error);
    result += ")";
    if (message[0] != '\0') {
        result += ": ";
        result += message.data();
    }
    return result;
}

bool IsFinitePositive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool ReadDriverKey(HKEY root, std::wstring_view key_name, AsioDriverInfo* result) {
    HKEY key = nullptr;
    const auto key_name_string = std::wstring(key_name);
    if (RegOpenKeyExW(root, key_name_string.c_str(), 0U, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const auto class_id_value = ReadRegistryString(key, L"clsid");
    if (!class_id_value.has_value()) {
        RegCloseKey(key);
        return false;
    }
    const auto class_id = GuidFromText(*class_id_value);
    if (!class_id.has_value()) {
        RegCloseKey(key);
        return false;
    }
    const auto description = ReadRegistryString(key, L"description");
    RegCloseKey(key);

    result->driver_id = Utf8FromWide(key_name);
    result->display_name = description.has_value() ? Utf8FromWide(*description) : result->driver_id;
    result->class_id_text = GuidText(*class_id);
    const auto dll_path = DriverDllPath(*class_id);
    result->dll_path = dll_path.has_value() ? Utf8FromWide(*dll_path) : std::string{};
    result->class_id = *class_id;
    return !result->driver_id.empty() && !result->class_id_text.empty();
}

} // namespace

std::vector<AsioDriverInfo> AsioHost::EnumerateDrivers() {
    std::map<std::string, AsioDriverInfo, std::less<>> drivers;
    // A 64-bit build normally sees the 64-bit view first. The second view is
    // included so a machine with only a 32-bit ASIO registration still gets a
    // deterministic discovery result rather than an empty list.
    for (const auto registry_view : {static_cast<REGSAM>(KEY_WOW64_64KEY),
                                     static_cast<REGSAM>(KEY_WOW64_32KEY)}) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\ASIO",
                          0U,
                          KEY_READ | registry_view,
                          &root) != ERROR_SUCCESS) {
            continue;
        }
        DWORD index = 0U;
        for (;;) {
            wchar_t key_name[256]{};
            DWORD key_name_length = static_cast<DWORD>(std::size(key_name));
            const auto result = RegEnumKeyExW(root,
                                              index++,
                                              key_name,
                                              &key_name_length,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr);
            if (result != ERROR_SUCCESS) {
                break;
            }
            AsioDriverInfo driver;
            if (ReadDriverKey(root, std::wstring_view(key_name, key_name_length), &driver)) {
                drivers.emplace(driver.driver_id, std::move(driver));
            }
        }
        RegCloseKey(root);
    }

    std::vector<AsioDriverInfo> result;
    result.reserve(drivers.size());
    for (auto& [unused_id, driver] : drivers) {
        static_cast<void>(unused_id);
        result.push_back(std::move(driver));
    }
    return result;
}

AsioProbeResult AsioHost::ProbeDriver(const AsioDriverInfo& driver, HWND system_window) {
    AsioProbeResult result;
    AsioHost host;
    if (!host.OpenDriver(driver, system_window != nullptr ? system_window : GetDesktopWindow())) {
        result.driver_error = host.LastErrorMessage();
        return result;
    }
    if (!host.QueryDriver(&result)) {
        result.driver_error = host.LastErrorMessage();
        host.Close();
        return result;
    }
    result.succeeded = true;
    host.Close();
    return result;
}

AsioHost::~AsioHost() {
    Close();
}

bool AsioHost::OpenDriver(const AsioDriverInfo& driver, HWND system_window) {
    if (driver.driver_id.empty()) {
        return SetError(AsioHostError::DriverNotFound, "ASIO driver identifier is empty");
    }
    const auto co_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co_result) && co_result != RPC_E_CHANGED_MODE) {
        return SetError(AsioHostError::ComInitializationFailed, "COM initialization for ASIO failed");
    }
    com_initialized_ = SUCCEEDED(co_result);

    IASIO* driver_instance = nullptr;
    const auto create_result = CoCreateInstance(driver.class_id,
                                                nullptr,
                                                CLSCTX_INPROC_SERVER,
                                                driver.class_id,
                                                reinterpret_cast<void**>(&driver_instance));
    if (FAILED(create_result) || driver_instance == nullptr) {
        if (com_initialized_) {
            CoUninitialize();
            com_initialized_ = false;
        }
        return SetError(AsioHostError::DriverCreationFailed,
                        "ASIO driver COM instance could not be created");
    }
    driver_info_ = driver;
    driver_ = driver_instance;
    const auto window = system_window != nullptr ? system_window : GetDesktopWindow();
    if (driver_instance->init(static_cast<void*>(window)) != asio_abi::kTrue) {
        const auto message = AsioDriverError(driver_instance, "ASIO init", -1);
        driver_instance->Release();
        driver_ = nullptr;
        if (com_initialized_) {
            CoUninitialize();
            com_initialized_ = false;
        }
        return SetError(AsioHostError::DriverInitializationFailed, message);
    }
    initialized_ = true;
    return true;
}

bool AsioHost::QueryDriver(AsioProbeResult* probe) {
    auto* driver = static_cast<IASIO*>(driver_);
    if (driver == nullptr || !initialized_ || probe == nullptr) {
        return SetError(AsioHostError::DriverQueryFailed, "ASIO driver is not initialized");
    }

    long input_channels = 0;
    long output_channels = 0;
    auto error = driver->getChannels(&input_channels, &output_channels);
    if (error != asio_abi::kOk || input_channels < 0 || output_channels < 0) {
        return SetError(AsioHostError::DriverQueryFailed,
                        AsioDriverError(driver, "ASIO getChannels", error));
    }
    ASIOSampleRate sample_rate = 0.0;
    error = driver->getSampleRate(&sample_rate);
    if (error != asio_abi::kOk || !IsFinitePositive(sample_rate)) {
        return SetError(AsioHostError::DriverQueryFailed,
                        AsioDriverError(driver, "ASIO getSampleRate", error));
    }
    long input_latency = 0;
    long output_latency = 0;
    error = driver->getLatencies(&input_latency, &output_latency);
    if (error != asio_abi::kOk || input_latency < 0 || output_latency < 0) {
        return SetError(AsioHostError::DriverQueryFailed,
                        AsioDriverError(driver, "ASIO getLatencies", error));
    }
    long minimum_buffer = 0;
    long maximum_buffer = 0;
    long preferred_buffer = 0;
    long granularity = 0;
    error = driver->getBufferSize(&minimum_buffer, &maximum_buffer, &preferred_buffer, &granularity);
    if (error != asio_abi::kOk || minimum_buffer <= 0 || maximum_buffer < minimum_buffer ||
        preferred_buffer < minimum_buffer || preferred_buffer > maximum_buffer) {
        return SetError(AsioHostError::DriverQueryFailed,
                        AsioDriverError(driver, "ASIO getBufferSize", error));
    }

    device_ = {.device_id = driver_info_.driver_id,
               .display_name = driver_info_.display_name,
               .input_channel_count = static_cast<std::uint32_t>(input_channels),
               .output_channel_count = static_cast<std::uint32_t>(output_channels),
               .native_sample_rate_hz = sample_rate,
               .clock_domain_id = driver_info_.driver_id};
    input_latency_samples_ = static_cast<std::uint32_t>(input_latency);
    output_latency_samples_ = static_cast<std::uint32_t>(output_latency);
    minimum_buffer_size_ = static_cast<std::uint32_t>(minimum_buffer);
    maximum_buffer_size_ = static_cast<std::uint32_t>(maximum_buffer);
    preferred_buffer_size_ = static_cast<std::uint32_t>(preferred_buffer);
    buffer_granularity_ = static_cast<std::int32_t>(granularity);

    probe->device = device_;
    probe->input_latency_samples = input_latency_samples_;
    probe->output_latency_samples = output_latency_samples_;
    probe->minimum_buffer_size = minimum_buffer_size_;
    probe->maximum_buffer_size = maximum_buffer_size_;
    probe->preferred_buffer_size = preferred_buffer_size_;
    probe->buffer_granularity = buffer_granularity_;
    return true;
}

bool AsioHost::Open(const AsioDriverInfo& driver,
                    HWND system_window,
                    const StandaloneAudioRouting& routing,
                    AsioProcessCallback callback,
                    void* callback_user) {
    Close();
    last_error_ = AsioHostError::None;
    last_error_message_.clear();
    try {
        if (!OpenDriver(driver, system_window)) {
            return false;
        }
        AsioProbeResult probe;
        if (!QueryDriver(&probe)) {
            Close();
            return false;
        }
        if (routing.inputs.empty() || routing.outputs.empty()) {
            Close();
            return SetError(AsioHostError::InvalidRouting, "ASIO routing requires input and output routes");
        }
        for (const auto& route : routing.inputs) {
            if (route.device_id != driver.driver_id) {
                Close();
                return SetError(AsioHostError::MultipleDevicesNotSupported,
                                "the first ASIO host milestone accepts one device only");
            }
        }
        for (const auto& route : routing.outputs) {
            if (route.device_id != driver.driver_id) {
                Close();
                return SetError(AsioHostError::MultipleDevicesNotSupported,
                                "the first ASIO host milestone accepts one device only");
            }
        }
        const auto validation = ValidateStandaloneAudioRouting({device_}, routing);
        if (!validation.errors.empty()) {
            Close();
            return SetError(AsioHostError::InvalidRouting, "ASIO route validation rejected the requested map");
        }
        if (std::fabs(routing.processing_format.sample_rate_hz - 192000.0) > 0.001) {
            Close();
            return SetError(AsioHostError::InvalidRouting, "ASIO host processing format must remain 192 kHz");
        }
        auto* asio = static_cast<IASIO*>(driver_);
        const auto device_rate = device_.native_sample_rate_hz;
        const auto can_device_rate_error = asio->canSampleRate(device_rate);
        if (can_device_rate_error != asio_abi::kOk) {
            const auto message = AsioDriverError(asio, "ASIO native sample-rate capability query", can_device_rate_error);
            Close();
            return SetError(AsioHostError::UnsupportedSampleRate, message);
        }
        ASIOSampleRate configured_rate = 0.0;
        if (asio->getSampleRate(&configured_rate) != asio_abi::kOk ||
            std::fabs(configured_rate - device_rate) > 0.001) {
            Close();
            return SetError(AsioHostError::UnsupportedSampleRate,
                            "ASIO driver sample rate changed while preparing the native-rate callback");
        }
        const auto requested_buffer_size = routing.device_buffer_size == 0U
            ? routing.processing_format.max_block_size
            : routing.device_buffer_size;
        if (!ValidateBufferSize(requested_buffer_size)) {
            Close();
            return SetError(AsioHostError::UnsupportedBufferSize,
                            "requested ASIO block size is outside the driver's supported granularity");
        }

        input_bindings_.clear();
        output_bindings_.clear();
        buffer_infos_.clear();
        input_bindings_.reserve(routing.inputs.size());
        output_bindings_.reserve(routing.outputs.size());
        buffer_infos_.reserve(routing.inputs.size() + routing.outputs.size());
        for (const auto& route : routing.inputs) {
            asio_abi::ASIOSampleType sample_type = 0;
            if (!QuerySelectedChannelType(true, route.device_channel, &sample_type)) {
                Close();
                return false;
            }
            buffer_infos_.push_back({.is_input = asio_abi::kTrue,
                                     .channel_number = static_cast<long>(route.device_channel),
                                     .buffers = {nullptr, nullptr}});
            input_bindings_.push_back({.input = true,
                                       .processing_channel = route.processing_channel,
                                       .buffer_index = static_cast<std::uint32_t>(buffer_infos_.size() - 1U),
                                       .sample_type = sample_type});
        }
        for (const auto& route : routing.outputs) {
            asio_abi::ASIOSampleType sample_type = 0;
            if (!QuerySelectedChannelType(false, route.device_channel, &sample_type)) {
                Close();
                return false;
            }
            buffer_infos_.push_back({.is_input = asio_abi::kFalse,
                                     .channel_number = static_cast<long>(route.device_channel),
                                     .buffers = {nullptr, nullptr}});
            output_bindings_.push_back({.input = false,
                                        .processing_channel = route.processing_channel,
                                        .buffer_index = static_cast<std::uint32_t>(buffer_infos_.size() - 1U),
                                        .sample_type = sample_type});
        }

        buffer_size_ = requested_buffer_size;
        callbacks_ = {.bufferSwitch = &AsioHost::BufferSwitch,
                       .sampleRateDidChange = &AsioHost::SampleRateDidChange,
                       .asioMessage = &AsioHost::AsioMessage,
                       .bufferSwitchTimeInfo = nullptr};
        const auto buffer_count = static_cast<long>(buffer_infos_.size());
        const auto create_error = asio->createBuffers(buffer_infos_.data(),
                                                      buffer_count,
                                                      static_cast<long>(buffer_size_),
                                                      &callbacks_);
        if (create_error != asio_abi::kOk) {
            const auto message = AsioDriverError(asio, "ASIO createBuffers", create_error);
            Close();
            return SetError(AsioHostError::BufferCreationFailed, message);
        }
        buffers_created_ = true;
        zero_buffer_.assign(buffer_size_, 0.0F);
        input_conversion_scratch_.assign(input_bindings_.size() * buffer_size_, 0.0F);
        output_conversion_scratch_.assign(output_bindings_.size() * buffer_size_, 0.0F);
        for (std::size_t index = 0; index < input_bindings_.size(); ++index) {
            input_bindings_[index].scratch_offset = index * buffer_size_;
        }
        for (std::size_t index = 0; index < output_bindings_.size(); ++index) {
            output_bindings_[index].scratch_offset = index * buffer_size_;
        }
        input_pointers_.assign(routing.processing_format.channel_count, zero_buffer_.data());
        output_pointers_.assign(routing.processing_format.channel_count, nullptr);
        callback_ = callback;
        callback_user_ = callback_user;
        reset_requested_.store(false, std::memory_order_release);
        dropout_reported_.store(false, std::memory_order_release);
        sample_rate_changed_to_.store(0.0, std::memory_order_release);
        ClearOutputBuffers();
        last_error_ = AsioHostError::None;
        last_error_message_.clear();
        return true;
    } catch (...) {
        Close();
        return SetError(AsioHostError::DriverQueryFailed, "ASIO host preparation ran out of memory");
    }
}

bool AsioHost::Start() {
    if (!initialized_ || !buffers_created_ || driver_ == nullptr) {
        return SetError(AsioHostError::StartFailed, "ASIO buffers are not prepared");
    }
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!AcquireActiveSlot()) {
        return SetError(AsioHostError::StartFailed, "maximum concurrent ASIO host callback slots are active");
    }
    switch (active_slot_) {
    case 0U:
        callbacks_.bufferSwitch = &AsioHost::BufferSwitch0;
        callbacks_.sampleRateDidChange = &AsioHost::SampleRateDidChange0;
        callbacks_.asioMessage = &AsioHost::AsioMessage0;
        break;
    case 1U:
        callbacks_.bufferSwitch = &AsioHost::BufferSwitch1;
        callbacks_.sampleRateDidChange = &AsioHost::SampleRateDidChange1;
        callbacks_.asioMessage = &AsioHost::AsioMessage1;
        break;
    case 2U:
        callbacks_.bufferSwitch = &AsioHost::BufferSwitch2;
        callbacks_.sampleRateDidChange = &AsioHost::SampleRateDidChange2;
        callbacks_.asioMessage = &AsioHost::AsioMessage2;
        break;
    default:
        callbacks_.bufferSwitch = &AsioHost::BufferSwitch3;
        callbacks_.sampleRateDidChange = &AsioHost::SampleRateDidChange3;
        callbacks_.asioMessage = &AsioHost::AsioMessage3;
        break;
    }
    auto* asio = static_cast<IASIO*>(driver_);
    const auto error = asio->start();
    if (error != asio_abi::kOk) {
        ReleaseActiveSlot();
        return SetError(AsioHostError::StartFailed, AsioDriverError(asio, "ASIO start", error));
    }
    running_.store(true, std::memory_order_release);
    return true;
}

void AsioHost::Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        ReleaseActiveSlot();
        return;
    }
    if (driver_ != nullptr) {
        static_cast<IASIO*>(driver_)->stop();
    }
    ReleaseActiveSlot();
}

void AsioHost::Close() noexcept {
    Stop();
    ReleaseActiveSlot();
    if (driver_ != nullptr) {
        auto* asio = static_cast<IASIO*>(driver_);
        if (buffers_created_) {
            asio->disposeBuffers();
        }
        asio->Release();
    }
    driver_ = nullptr;
    buffers_created_ = false;
    initialized_ = false;
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
    buffer_infos_.clear();
    input_bindings_.clear();
    output_bindings_.clear();
    input_pointers_.clear();
    output_pointers_.clear();
    zero_buffer_.clear();
    input_conversion_scratch_.clear();
    output_conversion_scratch_.clear();
    callback_ = nullptr;
    callback_user_ = nullptr;
    buffer_size_ = 0U;
    device_ = {};
}

bool AsioHost::IsOpen() const noexcept {
    return initialized_ && driver_ != nullptr && buffers_created_;
}

bool AsioHost::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

const AsioDeviceDescriptor& AsioHost::Device() const noexcept {
    return device_;
}

std::uint32_t AsioHost::BufferSize() const noexcept {
    return buffer_size_;
}

std::uint32_t AsioHost::InputLatencySamples() const noexcept {
    return input_latency_samples_;
}

std::uint32_t AsioHost::OutputLatencySamples() const noexcept {
    return output_latency_samples_;
}

AsioHostError AsioHost::LastError() const noexcept {
    return last_error_;
}

const std::string& AsioHost::LastErrorMessage() const noexcept {
    return last_error_message_;
}

bool AsioHost::ResetRequested() const noexcept {
    return reset_requested_.load(std::memory_order_acquire);
}

bool AsioHost::DropoutReported() const noexcept {
    return dropout_reported_.load(std::memory_order_acquire);
}

double AsioHost::SampleRateChangedTo() const noexcept {
    return sample_rate_changed_to_.load(std::memory_order_acquire);
}

bool AsioHost::SetError(AsioHostError error, std::string message) {
    last_error_ = error;
    last_error_message_ = std::move(message);
    return false;
}

bool AsioHost::ValidateBufferSize(std::uint32_t requested) const noexcept {
    if (requested < minimum_buffer_size_ || requested > maximum_buffer_size_) {
        return false;
    }
    if (buffer_granularity_ > 0) {
        return (requested - minimum_buffer_size_) % static_cast<std::uint32_t>(buffer_granularity_) == 0U;
    }
    if (buffer_granularity_ == -1) {
        auto candidate = minimum_buffer_size_;
        while (candidate < requested && candidate <= maximum_buffer_size_ / 2U) {
            candidate *= 2U;
        }
        return candidate == requested;
    }
    return requested == minimum_buffer_size_;
}

bool AsioHost::QuerySelectedChannelType(bool input,
                                        std::uint32_t channel,
                                        asio_abi::ASIOSampleType* sample_type) {
    auto* driver = static_cast<IASIO*>(driver_);
    if (sample_type == nullptr) {
        return SetError(AsioHostError::DriverQueryFailed, "ASIO sample type output is missing");
    }
    asio_abi::ASIOChannelInfo info{};
    info.channel = static_cast<long>(channel);
    info.is_input = input ? asio_abi::kTrue : asio_abi::kFalse;
    const auto error = driver->getChannelInfo(&info);
    if (error != asio_abi::kOk) {
        return SetError(AsioHostError::DriverQueryFailed,
                        AsioDriverError(driver, "ASIO getChannelInfo", error));
    }
    if (info.type != asio_abi::kFloat32Lsb && info.type != asio_abi::kInt16Lsb &&
        info.type != asio_abi::kInt24Lsb && info.type != asio_abi::kInt32Lsb) {
        return SetError(AsioHostError::UnsupportedSampleType,
                        "ASIO route uses an unsupported sample representation; supported formats are Float32, Int16, Int24 and Int32 LSB");
    }
    *sample_type = info.type;
    return true;
}

void AsioHost::HandleBufferSwitch(long double_buffer_index) noexcept {
    if (double_buffer_index < 0 || double_buffer_index > 1 || !buffers_created_) {
        return;
    }
    const auto buffer_index = static_cast<std::size_t>(double_buffer_index);
    std::fill(input_pointers_.begin(), input_pointers_.end(), zero_buffer_.data());
    std::fill(output_pointers_.begin(), output_pointers_.end(), nullptr);
    for (const auto& binding : input_bindings_) {
        const auto* source = buffer_infos_[binding.buffer_index].buffers[buffer_index];
        if (binding.sample_type == asio_abi::kFloat32Lsb) {
            input_pointers_[binding.processing_channel] = static_cast<const float*>(source);
        } else {
            auto* converted = input_conversion_scratch_.data() + binding.scratch_offset;
            if (!ConvertAsioInputToFloat32(source,
                                           converted,
                                           binding.sample_type,
                                           buffer_size_)) {
                std::fill_n(converted, buffer_size_, 0.0F);
            }
            input_pointers_[binding.processing_channel] = converted;
        }
    }
    for (const auto& binding : output_bindings_) {
        auto* pointer = binding.sample_type == asio_abi::kFloat32Lsb
            ? static_cast<float*>(buffer_infos_[binding.buffer_index].buffers[buffer_index])
            : output_conversion_scratch_.data() + binding.scratch_offset;
        if (output_pointers_[binding.processing_channel] == nullptr) {
            output_pointers_[binding.processing_channel] = pointer;
        }
    }
    if (callback_ != nullptr) {
        callback_(callback_user_,
                  input_pointers_.data(),
                  output_pointers_.data(),
                  static_cast<std::uint32_t>(output_pointers_.size()),
                  buffer_size_);
    } else {
        for (const auto& binding : output_bindings_) {
            auto* pointer = output_pointers_[binding.processing_channel];
            if (pointer != nullptr) {
                std::fill_n(pointer, buffer_size_, 0.0F);
            }
        }
    }
    for (const auto& binding : output_bindings_) {
        auto* raw_pointer = buffer_infos_[binding.buffer_index].buffers[buffer_index];
        auto* pointer = binding.sample_type == asio_abi::kFloat32Lsb
            ? static_cast<float*>(raw_pointer)
            : nullptr;
        const auto* primary = output_pointers_[binding.processing_channel];
        if (primary == nullptr) {
            continue;
        }
        if (binding.sample_type == asio_abi::kFloat32Lsb) {
            if (primary != pointer) {
                std::memcpy(pointer, primary, static_cast<std::size_t>(buffer_size_) * sizeof(float));
            }
        } else {
            static_cast<void>(ConvertFloat32ToAsioOutput(primary,
                                                         raw_pointer,
                                                         binding.sample_type,
                                                         buffer_size_));
        }
    }
}

void AsioHost::ClearOutputBuffers() noexcept {
    for (const auto& binding : output_bindings_) {
        for (void* raw_buffer : buffer_infos_[binding.buffer_index].buffers) {
            if (raw_buffer == nullptr) {
                continue;
            }
            if (binding.sample_type == asio_abi::kFloat32Lsb) {
                std::fill_n(static_cast<float*>(raw_buffer), buffer_size_, 0.0F);
            } else {
                static_cast<void>(ConvertFloat32ToAsioOutput(zero_buffer_.data(),
                                                             raw_buffer,
                                                             binding.sample_type,
                                                             buffer_size_));
            }
        }
    }
}

void AsioHost::ReleaseDriver() noexcept {
    Close();
}

void AsioHost::BufferSwitch(long double_buffer_index, long direct_process) noexcept {
    BufferSwitchSlot(0U, double_buffer_index, direct_process);
}

void AsioHost::BufferSwitch0(long double_buffer_index, long direct_process) noexcept {
    BufferSwitchSlot(0U, double_buffer_index, direct_process);
}

void AsioHost::BufferSwitch1(long double_buffer_index, long direct_process) noexcept {
    BufferSwitchSlot(1U, double_buffer_index, direct_process);
}

void AsioHost::BufferSwitch2(long double_buffer_index, long direct_process) noexcept {
    BufferSwitchSlot(2U, double_buffer_index, direct_process);
}

void AsioHost::BufferSwitch3(long double_buffer_index, long direct_process) noexcept {
    BufferSwitchSlot(3U, double_buffer_index, direct_process);
}

void AsioHost::BufferSwitchSlot(std::size_t slot, long double_buffer_index, long) noexcept {
    if (auto* host = ActiveHost(slot); host != nullptr) {
        host->HandleBufferSwitch(double_buffer_index);
    }
}

void AsioHost::SampleRateDidChange(double sample_rate) noexcept {
    SampleRateDidChangeSlot(0U, sample_rate);
}

void AsioHost::SampleRateDidChange0(double sample_rate) noexcept {
    SampleRateDidChangeSlot(0U, sample_rate);
}

void AsioHost::SampleRateDidChange1(double sample_rate) noexcept {
    SampleRateDidChangeSlot(1U, sample_rate);
}

void AsioHost::SampleRateDidChange2(double sample_rate) noexcept {
    SampleRateDidChangeSlot(2U, sample_rate);
}

void AsioHost::SampleRateDidChange3(double sample_rate) noexcept {
    SampleRateDidChangeSlot(3U, sample_rate);
}

void AsioHost::SampleRateDidChangeSlot(std::size_t slot, double sample_rate) noexcept {
    if (auto* host = ActiveHost(slot); host != nullptr) {
        host->sample_rate_changed_to_.store(sample_rate, std::memory_order_release);
    }
}

long AsioHost::AsioMessage(long selector, long value, void*, double*) noexcept {
    return AsioMessageSlot(0U, selector, value, nullptr, nullptr);
}

long AsioHost::AsioMessage0(long selector, long value, void* message, double* optional) noexcept {
    return AsioMessageSlot(0U, selector, value, message, optional);
}

long AsioHost::AsioMessage1(long selector, long value, void* message, double* optional) noexcept {
    return AsioMessageSlot(1U, selector, value, message, optional);
}

long AsioHost::AsioMessage2(long selector, long value, void* message, double* optional) noexcept {
    return AsioMessageSlot(2U, selector, value, message, optional);
}

long AsioHost::AsioMessage3(long selector, long value, void* message, double* optional) noexcept {
    return AsioMessageSlot(3U, selector, value, message, optional);
}

long AsioHost::AsioMessageSlot(std::size_t slot, long selector, long value, void*, double*) noexcept {
    auto* host = ActiveHost(slot);
    switch (selector) {
    case asio_abi::kSelectorSupported:
        return value == asio_abi::kEngineVersion || value == asio_abi::kResetRequest || value == asio_abi::kOverload
            ? 1L
            : 0L;
    case asio_abi::kEngineVersion:
        return 2L;
    case asio_abi::kResetRequest:
        if (host != nullptr) {
            host->reset_requested_.store(true, std::memory_order_release);
        }
        return 1L;
    case asio_abi::kOverload:
        if (host != nullptr) {
            host->dropout_reported_.store(true, std::memory_order_release);
        }
        return 1L;
    case asio_abi::kSupportsTimeInfo:
        return 0L;
    default:
        return 0L;
    }
}

AsioHost* AsioHost::ActiveHost(std::size_t slot) noexcept {
    return slot < g_active_hosts.size() ? g_active_hosts[slot].load(std::memory_order_acquire) : nullptr;
}

bool AsioHost::AcquireActiveSlot() noexcept {
    if (active_slot_ != kNoActiveSlot) {
        return true;
    }
    for (std::size_t slot = 0U; slot < g_active_hosts.size(); ++slot) {
        AsioHost* expected = nullptr;
        if (g_active_hosts[slot].compare_exchange_strong(expected,
                                                         this,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
            active_slot_ = slot;
            return true;
        }
    }
    return false;
}

void AsioHost::ReleaseActiveSlot() noexcept {
    if (active_slot_ == kNoActiveSlot) {
        return;
    }
    AsioHost* expected = this;
    static_cast<void>(g_active_hosts[active_slot_].compare_exchange_strong(expected,
                                                                            nullptr,
                                                                            std::memory_order_acq_rel,
                                                                            std::memory_order_acquire));
    active_slot_ = kNoActiveSlot;
}

} // namespace solo_rig::standalone
