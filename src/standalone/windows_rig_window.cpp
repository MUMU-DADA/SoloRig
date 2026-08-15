#include "standalone/windows_rig_window.hpp"
#include "standalone/asio_route_text.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace solo_rig::standalone {
namespace {

ID2D1Factory* AsD2DFactory(void* value) noexcept {
    return static_cast<ID2D1Factory*>(value);
}

IDWriteFactory* AsDWriteFactory(void* value) noexcept {
    return static_cast<IDWriteFactory*>(value);
}

ID2D1HwndRenderTarget* AsRenderTarget(void* value) noexcept {
    return static_cast<ID2D1HwndRenderTarget*>(value);
}

IDWriteTextFormat* AsTextFormat(void* value) noexcept {
    return static_cast<IDWriteTextFormat*>(value);
}

std::wstring Widen(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return result;
}

D2D1_COLOR_F ColorFromHex(std::string_view value, D2D1_COLOR_F fallback) noexcept {
    if (value.size() != 7U && value.size() != 9U) {
        return fallback;
    }
    auto nibble = [](char character) noexcept -> int {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    };
    auto byte = [&nibble](char high, char low) noexcept -> int {
        const auto first = nibble(high);
        const auto second = nibble(low);
        return first < 0 || second < 0 ? -1 : first * 16 + second;
    };
    const auto red = byte(value[1], value[2]);
    const auto green = byte(value[3], value[4]);
    const auto blue = byte(value[5], value[6]);
    const auto alpha = value.size() == 9U ? byte(value[7], value[8]) : 255;
    if (red < 0 || green < 0 || blue < 0 || alpha < 0) {
        return fallback;
    }
    return D2D1::ColorF(static_cast<float>(red) / 255.0F,
                        static_cast<float>(green) / 255.0F,
                        static_cast<float>(blue) / 255.0F,
                        static_cast<float>(alpha) / 255.0F);
}

D2D1_COLOR_F ThemeColor(const ThemeDefinition& theme, std::string_view key, D2D1_COLOR_F fallback) noexcept {
    const auto iterator = theme.colors.find(key);
    return iterator == theme.colors.end() ? fallback : ColorFromHex(iterator->second, fallback);
}

std::wstring MidiBindingText(const MidiBinding& binding) {
    const auto kind = binding.message_type == MidiMessageType::ControlChange ? "CC" : "PC";
    return Widen(std::string(kind) + " ch" + std::to_string(binding.channel + 1U) +
                 " #" + std::to_string(binding.number) + " -> " + binding.target_id +
                 " [" + std::to_string(binding.minimum) + ".." + std::to_string(binding.maximum) + "]");
}

std::wstring EditText(HWND edit) {
    if (edit == nullptr) {
        return {};
    }
    const auto length = GetWindowTextLengthW(edit);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto copied = GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

} // namespace

Win32RigWindow::~Win32RigWindow() {
    Destroy();
}

bool Win32RigWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &Win32RigWindow::WindowProc;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool Win32RigWindow::Create(HINSTANCE instance, RigEditorViewModel& editor, bool show) {
    if (window_ != nullptr || instance == nullptr || !RegisterWindowClass(instance)) {
        return false;
    }
    instance_ = instance;
    editor_ = &editor;
    class_registered_ = true;
    window_ = CreateWindowExW(0,
                             kWindowClassName,
                             L"SoloRig - Guitar Rig Editor",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             1280,
                             760,
                             nullptr,
                             nullptr,
                             instance_,
                             this);
    if (window_ == nullptr) {
        editor_ = nullptr;
        return false;
    }
    if (!InitializeRenderResources()) {
        Destroy();
        return false;
    }
    CreateToolbarButtons();
    SetTimer(window_, 2U, 250U, nullptr);
    RefreshAsioControls();
    UpdateAudioStatus();
    UpdateEditorViewport();
    UpdateWindowTitle();
    if (show) {
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
    }
    return true;
}

bool Win32RigWindow::InitializeRenderResources() {
    if (window_ == nullptr) {
        return false;
    }
    ReleaseRenderResources();
    ID2D1Factory* d2d_factory = nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory))) {
        return false;
    }
    IDWriteFactory* dwrite_factory = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dwrite_factory)))) {
        d2d_factory->Release();
        return false;
    }
    RECT client{};
    GetClientRect(window_, &client);
    ID2D1HwndRenderTarget* render_target = nullptr;
    const auto size = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right - client.left)),
                                  static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top)));
    if (FAILED(d2d_factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                   D2D1::HwndRenderTargetProperties(window_, size),
                                                   &render_target))) {
        dwrite_factory->Release();
        d2d_factory->Release();
        return false;
    }
    IDWriteTextFormat* text_format = nullptr;
    const auto font_result = dwrite_factory->CreateTextFormat(L"Segoe UI",
                                                               nullptr,
                                                               DWRITE_FONT_WEIGHT_NORMAL,
                                                               DWRITE_FONT_STYLE_NORMAL,
                                                               DWRITE_FONT_STRETCH_NORMAL,
                                                               16.0F,
                                                               L"en-us",
                                                               &text_format);
    if (FAILED(font_result)) {
        render_target->Release();
        dwrite_factory->Release();
        d2d_factory->Release();
        return false;
    }
    d2d_factory_ = d2d_factory;
    dwrite_factory_ = dwrite_factory;
    render_target_ = render_target;
    text_format_ = text_format;
    return true;
}

void Win32RigWindow::Resize(std::uint32_t width, std::uint32_t height, float dpi_scale) {
    dpi_scale_ = std::isfinite(dpi_scale) && dpi_scale > 0.0F ? dpi_scale : 1.0F;
    if (AsRenderTarget(render_target_) != nullptr) {
        static_cast<void>(AsRenderTarget(render_target_)->Resize(
            D2D1::SizeU(std::max(width, 1U), std::max(height, 1U))));
    }
    UpdateEditorViewport();
}

void Win32RigWindow::Paint() {
    last_paint_succeeded_ = false;
    auto* target = AsRenderTarget(render_target_);
    if (target == nullptr || editor_ == nullptr) {
        return;
    }
    const auto& scene = editor_->Scene();
    ID2D1SolidColorBrush* background = nullptr;
    ID2D1SolidColorBrush* surface = nullptr;
    ID2D1SolidColorBrush* text = nullptr;
    ID2D1SolidColorBrush* accent = nullptr;
    ID2D1SolidColorBrush* warning = nullptr;
    const auto create = [target](D2D1_COLOR_F color, ID2D1SolidColorBrush** brush) {
        return target->CreateSolidColorBrush(color, brush);
    };
    if (FAILED(create(ThemeColor(scene.theme, "background", D2D1::ColorF(D2D1::ColorF::DarkGray)), &background)) ||
        FAILED(create(ThemeColor(scene.theme, "surface", D2D1::ColorF(D2D1::ColorF::Gray)), &surface)) ||
        FAILED(create(ThemeColor(scene.theme, "text", D2D1::ColorF(D2D1::ColorF::White)), &text)) ||
        FAILED(create(ThemeColor(scene.theme, "accent", D2D1::ColorF(D2D1::ColorF::Turquoise)), &accent)) ||
        FAILED(create(ThemeColor(scene.theme, "warning", D2D1::ColorF(D2D1::ColorF::Orange)), &warning))) {
        if (background != nullptr) background->Release();
        if (surface != nullptr) surface->Release();
        if (text != nullptr) text->Release();
        if (accent != nullptr) accent->Release();
        if (warning != nullptr) warning->Release();
        return;
    }

    target->BeginDraw();
    target->Clear(ThemeColor(scene.theme, "background", D2D1::ColorF(D2D1::ColorF::DarkGray)));
    const auto width = static_cast<float>(scene.viewport_width);
    const auto height = static_cast<float>(scene.viewport_height);
    target->FillRectangle(D2D1::RectF(0.0F, 0.0F, width, 64.0F), surface);
    target->DrawLine(D2D1::Point2F(0.0F, 64.0F), D2D1::Point2F(width, 64.0F), accent, 1.0F);
    target->FillRectangle(D2D1::RectF(0.0F, std::max(64.0F, height - 42.0F), width, height), surface);

    for (const auto& connection : scene.connections) {
        target->DrawLine(D2D1::Point2F(static_cast<float>(connection.source.x), static_cast<float>(connection.source.y)),
                         D2D1::Point2F(static_cast<float>(connection.destination.x), static_cast<float>(connection.destination.y)),
                         accent,
                         3.0F);
    }
    for (const auto& node : scene.nodes) {
        const auto rectangle = D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(node.bounds.x),
                        static_cast<float>(node.bounds.y),
                        static_cast<float>(node.bounds.x + node.bounds.width),
                        static_cast<float>(node.bounds.y + node.bounds.height)),
            10.0F,
            10.0F);
        target->FillRoundedRectangle(rectangle, surface);
        target->DrawRoundedRectangle(rectangle, node.selected ? accent : text, node.selected ? 4.0F : 1.0F);
        const auto title = Widen(node.instance_id == "drive-1" ? "Drive" :
                                 node.instance_id == "delay-1" ? "Delay" : node.module_id);
        const auto title_rect = D2D1::RectF(static_cast<float>(node.bounds.x + 16.0 * scene.dpi_scale),
                                            static_cast<float>(node.bounds.y + 16.0 * scene.dpi_scale),
                                            static_cast<float>(node.bounds.x + node.bounds.width - 16.0 * scene.dpi_scale),
                                            static_cast<float>(node.bounds.y + 48.0 * scene.dpi_scale));
        target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), AsTextFormat(text_format_), title_rect, text);
        const auto controls = Widen("  knob   knob   knob\n  footswitch");
        const auto control_rect = D2D1::RectF(static_cast<float>(node.bounds.x + 16.0 * scene.dpi_scale),
                                              static_cast<float>(node.bounds.y + 64.0 * scene.dpi_scale),
                                              static_cast<float>(node.bounds.x + node.bounds.width - 16.0 * scene.dpi_scale),
                                              static_cast<float>(node.bounds.y + node.bounds.height - 12.0 * scene.dpi_scale));
        target->DrawTextW(controls.c_str(), static_cast<UINT32>(controls.size()), AsTextFormat(text_format_), control_rect, text);
    }

    const auto toolbar = Widen("SoloRig   |   T: Toggle Light/Dark Theme");
    target->DrawTextW(toolbar.c_str(), static_cast<UINT32>(toolbar.size()), AsTextFormat(text_format_),
                      D2D1::RectF(300.0F, 18.0F, width - 20.0F, 52.0F), text);
    std::string status_text(scene.audio_status);
    if (!scene.file_status.empty()) {
        status_text += " | ";
        status_text += scene.file_status;
    }
    if (midi_session_ != nullptr) {
        status_text += " | ";
        status_text += midi_session_->StatusText();
    }
    if (scene.dirty) {
        status_text += " *";
    }
    const auto status = Widen(status_text);
    target->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), AsTextFormat(text_format_),
                      D2D1::RectF(22.0F, std::max(64.0F, height - 34.0F), width - 20.0F, height - 8.0F), warning);
    const auto result = target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        ReleaseRenderResources();
        static_cast<void>(InitializeRenderResources());
    }
    background->Release();
    surface->Release();
    text->Release();
    accent->Release();
    warning->Release();
    last_paint_succeeded_ = result == S_OK;
    if (last_paint_succeeded_) {
        ++paint_count_;
    }
}

void Win32RigWindow::Destroy() noexcept {
    if (audio_engine_ != nullptr) {
        audio_engine_->Close();
    }
    if (asio_close_button_ != nullptr) {
        DestroyWindow(asio_close_button_);
        asio_close_button_ = nullptr;
    }
    if (asio_recover_button_ != nullptr) {
        DestroyWindow(asio_recover_button_);
        asio_recover_button_ = nullptr;
    }
    if (asio_route_label_ != nullptr) {
        DestroyWindow(asio_route_label_);
        asio_route_label_ = nullptr;
    }
    if (asio_route_edit_ != nullptr) {
        DestroyWindow(asio_route_edit_);
        asio_route_edit_ = nullptr;
    }
    if (asio_start_button_ != nullptr) {
        DestroyWindow(asio_start_button_);
        asio_start_button_ = nullptr;
    }
    if (asio_open_button_ != nullptr) {
        DestroyWindow(asio_open_button_);
        asio_open_button_ = nullptr;
    }
    if (asio_refresh_button_ != nullptr) {
        DestroyWindow(asio_refresh_button_);
        asio_refresh_button_ = nullptr;
    }
    if (asio_driver_combo_ != nullptr) {
        DestroyWindow(asio_driver_combo_);
        asio_driver_combo_ = nullptr;
    }
    asio_driver_ids_.clear();
    if (midi_close_button_ != nullptr) {
        DestroyWindow(midi_close_button_);
        midi_close_button_ = nullptr;
    }
    if (midi_open_button_ != nullptr) {
        DestroyWindow(midi_open_button_);
        midi_open_button_ = nullptr;
    }
    if (midi_refresh_button_ != nullptr) {
        DestroyWindow(midi_refresh_button_);
        midi_refresh_button_ = nullptr;
    }
    if (midi_target_combo_ != nullptr) {
        DestroyWindow(midi_target_combo_);
        midi_target_combo_ = nullptr;
    }
    if (midi_mapping_list_ != nullptr) {
        DestroyWindow(midi_mapping_list_);
        midi_mapping_list_ = nullptr;
    }
    if (midi_learn_button_ != nullptr) {
        DestroyWindow(midi_learn_button_);
        midi_learn_button_ = nullptr;
    }
    if (midi_cancel_learn_button_ != nullptr) {
        DestroyWindow(midi_cancel_learn_button_);
        midi_cancel_learn_button_ = nullptr;
    }
    if (midi_delete_button_ != nullptr) {
        DestroyWindow(midi_delete_button_);
        midi_delete_button_ = nullptr;
    }
    if (midi_clear_button_ != nullptr) {
        DestroyWindow(midi_clear_button_);
        midi_clear_button_ = nullptr;
    }
    if (midi_hint_label_ != nullptr) {
        DestroyWindow(midi_hint_label_);
        midi_hint_label_ = nullptr;
    }
    midi_target_ids_.clear();
    if (window_ != nullptr) {
        KillTimer(window_, 1U);
        KillTimer(window_, 2U);
    }
    midi_timer_enabled_ = false;
    if (midi_session_ != nullptr) {
        midi_session_->Close();
        midi_session_ = nullptr;
    }
    if (open_button_ != nullptr) {
        DestroyWindow(open_button_);
        open_button_ = nullptr;
    }
    if (save_as_button_ != nullptr) {
        DestroyWindow(save_as_button_);
        save_as_button_ = nullptr;
    }
    if (save_button_ != nullptr) {
        DestroyWindow(save_button_);
        save_button_ = nullptr;
    }
    ReleaseRenderResources();
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    editor_ = nullptr;
}

int Win32RigWindow::RunMessageLoop() const {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

HWND Win32RigWindow::Handle() const noexcept {
    return window_;
}

bool Win32RigWindow::LastPaintSucceeded() const noexcept {
    return last_paint_succeeded_;
}

std::uint64_t Win32RigWindow::PaintCount() const noexcept {
    return paint_count_;
}

void Win32RigWindow::SetFileDialogProvider(FileDialogProvider provider) {
    file_dialog_provider_ = std::move(provider);
}

void Win32RigWindow::SetMidiControlSession(StandaloneMidiControlSession* session) {
    if (midi_session_ != nullptr && midi_session_ != session) {
        midi_session_->Close();
        midi_session_ = nullptr;
    }
    midi_session_ = session;
    if (window_ != nullptr) {
        KillTimer(window_, 1U);
        midi_timer_enabled_ = false;
        InvalidateRect(window_, nullptr, FALSE);
    }
    RefreshMidiMappingControls();
}

void Win32RigWindow::SetAudioEngine(StandaloneAudioEngine* engine) {
    if (audio_engine_ != nullptr && audio_engine_ != engine) {
        audio_engine_->Close();
    }
    audio_engine_ = engine;
    RefreshAsioControls();
    UpdateAudioStatus();
}

bool Win32RigWindow::TriggerAsioRefresh() {
    if (audio_engine_ == nullptr) {
        return false;
    }
    const auto refreshed = audio_engine_->RefreshDrivers();
    RefreshAsioControls();
    UpdateAudioStatus();
    InvalidateRect(window_, nullptr, FALSE);
    return refreshed;
}

bool Win32RigWindow::TriggerAsioOpen() {
    if (audio_engine_ == nullptr || editor_ == nullptr) {
        return false;
    }
    const auto routing = SelectedAsioRouting();
    if (!routing.has_value()) {
        editor_->SetAudioStatus("ASIO routing rejected: use comma-separated physical channel numbers");
        InvalidateRect(window_, nullptr, FALSE);
        return false;
    }
    std::vector<AsioDriverInfo> selected_drivers;
    for (const auto& driver : audio_engine_->Drivers()) {
        const auto referenced = std::any_of(routing->inputs.begin(), routing->inputs.end(),
                                            [&driver](const auto& route) { return route.device_id == driver.driver_id; }) ||
            std::any_of(routing->outputs.begin(), routing->outputs.end(),
                        [&driver](const auto& route) { return route.device_id == driver.driver_id; });
        if (referenced) {
            selected_drivers.push_back(driver);
        }
    }
    const auto opened = audio_engine_->OpenWithDevices(std::move(selected_drivers),
                                                       window_,
                                                       editor_->ExportPreset(),
                                                       *routing);
    UpdateAudioStatus();
    InvalidateRect(window_, nullptr, FALSE);
    return opened;
}

bool Win32RigWindow::TriggerAsioStart() {
    if (audio_engine_ == nullptr) {
        return false;
    }
    const auto started = audio_engine_->Start();
    UpdateAudioStatus();
    InvalidateRect(window_, nullptr, FALSE);
    return started;
}

bool Win32RigWindow::TriggerAsioClose() {
    if (audio_engine_ == nullptr) {
        return false;
    }
    audio_engine_->Close();
    UpdateAudioStatus();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

bool Win32RigWindow::TriggerAsioRecover() {
    if (audio_engine_ == nullptr) {
        return false;
    }
    const auto recovered = audio_engine_->RecoverAudio();
    UpdateAudioStatus();
    InvalidateRect(window_, nullptr, FALSE);
    return recovered;
}

std::optional<StandaloneAudioRouting> Win32RigWindow::SelectedAsioRouting() const {
    if (audio_engine_ == nullptr || asio_driver_combo_ == nullptr) {
        return std::nullopt;
    }
    const auto entries = ParseAsioRouteText(EditText(asio_route_edit_), audio_engine_->Drivers().size());
    if (!entries.has_value()) {
        return std::nullopt;
    }
    std::size_t maximum_channel = 0U;
    for (const auto& entry : *entries) {
        maximum_channel = std::max(maximum_channel, entry.inputs.size());
        maximum_channel = std::max(maximum_channel, entry.outputs.size());
    }
    if (maximum_channel == 0U || maximum_channel > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    StandaloneAudioRouting routing{
        .processing_format = {.sample_rate_hz = 192000.0,
                              .max_block_size = 1U,
                              .channel_count = static_cast<std::uint32_t>(maximum_channel),
                              .sample_representation = SampleRepresentation::Float32},
        .device_buffer_size = 0U};
    for (const auto& entry : *entries) {
        const auto& driver = audio_engine_->Drivers().at(entry.driver_index);
        for (std::size_t index = 0U; index < entry.inputs.size(); ++index) {
            routing.inputs.push_back({.device_id = driver.driver_id,
                                      .device_channel = entry.inputs[index],
                                      .processing_channel = static_cast<std::uint32_t>(index)});
        }
        for (std::size_t index = 0U; index < entry.outputs.size(); ++index) {
            routing.outputs.push_back({.device_id = driver.driver_id,
                                       .device_channel = entry.outputs[index],
                                       .processing_channel = static_cast<std::uint32_t>(index)});
        }
    }
    return routing;
}

bool Win32RigWindow::TriggerMidiRefresh() {
    if (midi_session_ == nullptr) {
        return false;
    }
    const auto devices = midi_session_->RefreshDevices();
    RefreshMidiMappingControls();
    InvalidateRect(window_, nullptr, FALSE);
    return !devices.empty();
}

bool Win32RigWindow::TriggerMidiOpen() {
    if (midi_session_ == nullptr) {
        return false;
    }
    const auto opened = midi_session_->OpenSelected();
    if (opened && window_ != nullptr) {
        SetTimer(window_, 1U, 25U, nullptr);
        midi_timer_enabled_ = true;
    } else if (!opened && window_ != nullptr) {
        KillTimer(window_, 1U);
        midi_timer_enabled_ = false;
    }
    InvalidateRect(window_, nullptr, FALSE);
    RefreshMidiMappingControls();
    return opened;
}

bool Win32RigWindow::TriggerMidiClose() {
    if (midi_session_ == nullptr) {
        return false;
    }
    midi_session_->Close();
    if (window_ != nullptr) {
        KillTimer(window_, 1U);
    }
    midi_timer_enabled_ = false;
    RefreshMidiMappingControls();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

std::size_t Win32RigWindow::DrainMidi() {
    if (midi_session_ == nullptr) {
        return 0U;
    }
    const auto drained = midi_session_->Drain();
    RefreshMidiMappingControls();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return drained;
}

bool Win32RigWindow::TriggerMidiLearn() {
    if (midi_session_ == nullptr) {
        return false;
    }
    const auto target = SelectedMidiTarget();
    const auto armed = midi_session_->ArmLearn(target.value_or(std::string{}));
    RefreshMidiMappingControls();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return armed;
}

bool Win32RigWindow::TriggerMidiCancelLearn() {
    if (midi_session_ == nullptr) {
        return false;
    }
    midi_session_->CancelLearn();
    RefreshMidiMappingControls();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

bool Win32RigWindow::TriggerMidiDeleteBinding() {
    if (midi_session_ == nullptr || midi_mapping_list_ == nullptr) {
        return false;
    }
    const auto selected = SendMessageW(midi_mapping_list_, LB_GETCURSEL, 0, 0);
    if (selected < 0) {
        static_cast<void>(midi_session_->RemoveBinding(std::numeric_limits<std::size_t>::max()));
        RefreshMidiMappingControls();
        InvalidateRect(window_, nullptr, FALSE);
        return false;
    }
    const auto removed = midi_session_->RemoveBinding(static_cast<std::size_t>(selected));
    RefreshMidiMappingControls();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return removed;
}

bool Win32RigWindow::TriggerMidiClearBindings() {
    if (midi_session_ == nullptr) {
        return false;
    }
    const auto cleared = midi_session_->ClearBindings();
    RefreshMidiMappingControls();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return cleared;
}

bool Win32RigWindow::MidiLearnArmed() const noexcept {
    return midi_session_ != nullptr && midi_session_->IsLearnArmed();
}

bool Win32RigWindow::MidiDrainTimerEnabled() const noexcept {
    return midi_timer_enabled_;
}

bool Win32RigWindow::TriggerSave() {
    if (editor_ == nullptr) {
        return false;
    }
    if (!editor_->SourcePath().has_value()) {
        const auto destination = file_dialog_provider_ ? file_dialog_provider_(FileDialogAction::Save, editor_->SourcePath())
                                                       : ShowNativeFileDialog(FileDialogAction::Save);
        if (!destination.has_value()) {
            return false;
        }
        const auto result = editor_->SaveTo(*destination);
        UpdateWindowTitle();
        InvalidateRect(window_, nullptr, FALSE);
        return result.success;
    }
    const auto result = editor_->SaveCurrent();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return result.success;
}

bool Win32RigWindow::TriggerSaveAs() {
    if (editor_ == nullptr) {
        return false;
    }
    const auto destination = file_dialog_provider_ ? file_dialog_provider_(FileDialogAction::Save, editor_->SourcePath())
                                                   : ShowNativeFileDialog(FileDialogAction::Save);
    if (!destination.has_value()) {
        return false;
    }
    const auto result = editor_->SaveTo(*destination);
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return result.success;
}

bool Win32RigWindow::TriggerOpen() {
    if (editor_ == nullptr) {
        return false;
    }
    const auto source = file_dialog_provider_ ? file_dialog_provider_(FileDialogAction::Open, editor_->SourcePath())
                                              : ShowNativeFileDialog(FileDialogAction::Open);
    if (!source.has_value()) {
        return false;
    }
    const auto result = editor_->LoadFrom(*source);
    if (result.success && midi_session_ != nullptr) {
        static_cast<void>(midi_session_->SyncFromEditor());
        RefreshMidiMappingControls();
    }
    if (result.success && audio_engine_ != nullptr && audio_engine_->IsOpen()) {
        static_cast<void>(audio_engine_->ReloadPreset(editor_->ExportPreset()));
        UpdateAudioStatus();
    }
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return result.success;
}

bool Win32RigWindow::HandleShortcut(std::uint32_t key, bool control, bool shift) {
    if (!control) {
        if (key == 'T' && editor_ != nullptr) {
            editor_->ToggleTheme();
            UpdateWindowTitle();
            InvalidateRect(window_, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (key == 'S') {
        return shift ? TriggerSaveAs() : TriggerSave();
    }
    if (key == 'O' && !shift) {
        return TriggerOpen();
    }
    return false;
}

bool Win32RigWindow::RunSmokeTest(HINSTANCE instance) {
    RigEditorViewModel editor;
    Win32RigWindow window;
    if (!window.Create(instance, editor, false) || !window.InitializeRenderResources()) {
        return false;
    }
    window.Resize(1024, 768, 1.0F);
    window.Paint();
    const auto handle = window.Handle();
    const auto painted = window.LastPaintSucceeded() && window.PaintCount() > 0U;
    window.Destroy();
    return handle != nullptr && painted && window.Handle() == nullptr;
}

LRESULT CALLBACK Win32RigWindow::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<Win32RigWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        self = static_cast<Win32RigWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    return self == nullptr ? DefWindowProcW(window, message, w_param, l_param) : self->HandleMessage(message, w_param, l_param);
}

LRESULT Win32RigWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        Paint();
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_SIZE:
        Resize(LOWORD(l_param), HIWORD(l_param), dpi_scale_);
        LayoutToolbarButtons();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (w_param == 1U) {
            if (midi_session_ == nullptr || !midi_session_->IsOpen()) {
                KillTimer(window_, 1U);
                midi_timer_enabled_ = false;
                return 0;
            }
            static_cast<void>(DrainMidi());
            return 0;
        }
        if (w_param == 2U) {
            UpdateAudioStatus();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_DPICHANGED: {
        dpi_scale_ = static_cast<float>(HIWORD(w_param)) / 96.0F;
        const auto* suggested = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateEditorViewport();
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (editor_ != nullptr && editor_->BeginDrag({static_cast<double>(GET_X_LPARAM(l_param)),
                                                       static_cast<double>(GET_Y_LPARAM(l_param))})) {
            SetCapture(window_);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (editor_ != nullptr && (w_param & MK_LBUTTON) != 0U && editor_->IsDragging()) {
            static_cast<void>(editor_->DragTo({static_cast<double>(GET_X_LPARAM(l_param)),
                                               static_cast<double>(GET_Y_LPARAM(l_param))}));
            UpdateWindowTitle();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (editor_ != nullptr) {
            editor_->EndDrag();
        }
        ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        static_cast<void>(HandleShortcut(static_cast<std::uint32_t>(w_param),
                                         (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                         (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    case WM_DESTROY:
        KillTimer(window_, 1U);
        KillTimer(window_, 2U);
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        if (HIWORD(w_param) == BN_CLICKED ||
            (LOWORD(w_param) == 1007 && HIWORD(w_param) == CBN_SELCHANGE) ||
            (LOWORD(w_param) == 1018 && HIWORD(w_param) == CBN_SELCHANGE)) {
            if (LOWORD(w_param) == 1001) {
                static_cast<void>(TriggerSave());
            } else if (LOWORD(w_param) == 1002) {
                static_cast<void>(TriggerSaveAs());
            } else if (LOWORD(w_param) == 1003) {
                static_cast<void>(TriggerOpen());
            } else if (LOWORD(w_param) == 1004) {
                static_cast<void>(TriggerMidiRefresh());
            } else if (LOWORD(w_param) == 1005) {
                static_cast<void>(TriggerMidiOpen());
            } else if (LOWORD(w_param) == 1006) {
                static_cast<void>(TriggerMidiClose());
            } else if (LOWORD(w_param) == 1008) {
                static_cast<void>(TriggerMidiLearn());
            } else if (LOWORD(w_param) == 1009) {
                static_cast<void>(TriggerMidiCancelLearn());
            } else if (LOWORD(w_param) == 1010) {
                static_cast<void>(TriggerMidiDeleteBinding());
            } else if (LOWORD(w_param) == 1011) {
                static_cast<void>(TriggerMidiClearBindings());
            } else if (LOWORD(w_param) == 1014) {
                static_cast<void>(TriggerAsioRefresh());
            } else if (LOWORD(w_param) == 1015) {
                static_cast<void>(TriggerAsioOpen());
            } else if (LOWORD(w_param) == 1016) {
                static_cast<void>(TriggerAsioStart());
            } else if (LOWORD(w_param) == 1017) {
                static_cast<void>(TriggerAsioClose());
            } else if (LOWORD(w_param) == 1023) {
                static_cast<void>(TriggerAsioRecover());
            }
            if (LOWORD(w_param) == 1007) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        return 0;
    default:
        return DefWindowProcW(window_, message, w_param, l_param);
    }
}

void Win32RigWindow::CreateToolbarButtons() {
    save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   16, 16, 70, 30, window_, reinterpret_cast<HMENU>(1001), instance_, nullptr);
    save_as_button_ = CreateWindowExW(0, L"BUTTON", L"Save As", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      92, 16, 80, 30, window_, reinterpret_cast<HMENU>(1002), instance_, nullptr);
    open_button_ = CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   178, 16, 70, 30, window_, reinterpret_cast<HMENU>(1003), instance_, nullptr);
    asio_refresh_button_ = CreateWindowExW(0, L"BUTTON", L"ASIO Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           16, 50, 96, 30, window_, reinterpret_cast<HMENU>(1014), instance_, nullptr);
    asio_driver_combo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         118, 50, 210, 220, window_, reinterpret_cast<HMENU>(1018), instance_, nullptr);
    asio_open_button_ = CreateWindowExW(0, L"BUTTON", L"ASIO Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        334, 50, 84, 30, window_, reinterpret_cast<HMENU>(1015), instance_, nullptr);
    asio_start_button_ = CreateWindowExW(0, L"BUTTON", L"ASIO Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         424, 50, 84, 30, window_, reinterpret_cast<HMENU>(1016), instance_, nullptr);
    asio_close_button_ = CreateWindowExW(0, L"BUTTON", L"ASIO Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         514, 50, 84, 30, window_, reinterpret_cast<HMENU>(1017), instance_, nullptr);
    asio_route_label_ = CreateWindowExW(0, L"STATIC", L"Routes:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        118, 56, 46, 20, window_, reinterpret_cast<HMENU>(1019), instance_, nullptr);
    asio_route_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0:in=0,1;out=0,1",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       164, 50, 190, 30, window_, reinterpret_cast<HMENU>(1020), instance_, nullptr);
    asio_recover_button_ = CreateWindowExW(0, L"BUTTON", L"ASIO Recover", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           884, 50, 98, 30, window_, reinterpret_cast<HMENU>(1023), instance_, nullptr);
    midi_refresh_button_ = CreateWindowExW(0, L"BUTTON", L"MIDI Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           256, 16, 98, 30, window_, reinterpret_cast<HMENU>(1004), instance_, nullptr);
    midi_open_button_ = CreateWindowExW(0, L"BUTTON", L"MIDI Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        360, 16, 88, 30, window_, reinterpret_cast<HMENU>(1005), instance_, nullptr);
    midi_close_button_ = CreateWindowExW(0, L"BUTTON", L"MIDI Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         454, 16, 92, 30, window_, reinterpret_cast<HMENU>(1006), instance_, nullptr);
    midi_target_combo_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         560, 16, 220, 220, window_, reinterpret_cast<HMENU>(1007), instance_, nullptr);
    midi_learn_button_ = CreateWindowExW(0, L"BUTTON", L"Learn CC", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         788, 16, 78, 30, window_, reinterpret_cast<HMENU>(1008), instance_, nullptr);
    midi_cancel_learn_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                870, 16, 72, 30, window_, reinterpret_cast<HMENU>(1009), instance_, nullptr);
    midi_delete_button_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          946, 16, 72, 30, window_, reinterpret_cast<HMENU>(1010), instance_, nullptr);
    midi_clear_button_ = CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         1022, 16, 72, 30, window_, reinterpret_cast<HMENU>(1011), instance_, nullptr);
    midi_mapping_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                         560, 72, 520, 110, window_, reinterpret_cast<HMENU>(1012), instance_, nullptr);
    midi_hint_label_ = CreateWindowExW(0, L"STATIC",
                                       L"MIDI CC Learn only | Program Change bank remains session-controlled",
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       560, 184, 520, 24, window_, reinterpret_cast<HMENU>(1013), instance_, nullptr);
    LayoutToolbarButtons();
    RefreshMidiMappingControls();
}

void Win32RigWindow::LayoutToolbarButtons() {
    if (window_ == nullptr) {
        return;
    }
    const auto move = [](HWND button, int x, int y, int width, int height) {
        if (button != nullptr) {
            SetWindowPos(button, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };
    move(save_button_, 16, 16, 70, 30);
    move(save_as_button_, 92, 16, 80, 30);
    move(open_button_, 178, 16, 70, 30);
    move(asio_refresh_button_, 16, 50, 96, 30);
    move(asio_route_label_, 118, 56, 46, 20);
    move(asio_route_edit_, 164, 50, 190, 30);
    move(asio_driver_combo_, 360, 50, 120, 220);
    move(asio_open_button_, 486, 50, 84, 30);
    move(asio_start_button_, 576, 50, 84, 30);
    move(asio_close_button_, 666, 50, 84, 30);
    move(asio_recover_button_, 756, 50, 98, 30);
    move(midi_refresh_button_, 256, 16, 98, 30);
    move(midi_open_button_, 360, 16, 88, 30);
    move(midi_close_button_, 454, 16, 92, 30);
    RECT client{};
    GetClientRect(window_, &client);
    const auto width = std::max<LONG>(1, client.right - client.left);
    const auto panel_left = std::max<LONG>(880, width - 440);
    move(midi_target_combo_, panel_left, 16, 180, 240);
    move(midi_learn_button_, panel_left + 188, 16, 72, 30);
    move(midi_cancel_learn_button_, panel_left + 264, 16, 70, 30);
    move(midi_delete_button_, panel_left + 338, 16, 70, 30);
    move(midi_clear_button_, panel_left + 338, 50, 70, 30);
    move(midi_mapping_list_, panel_left, 50, 330, 120);
    move(midi_hint_label_, panel_left, 174, 410, 36);
}

void Win32RigWindow::RefreshAsioControls() {
    if (asio_driver_combo_ == nullptr) {
        return;
    }
    std::string previous_driver;
    const auto previous_index = SendMessageW(asio_driver_combo_, CB_GETCURSEL, 0, 0);
    if (previous_index >= 0 && static_cast<std::size_t>(previous_index) < asio_driver_ids_.size()) {
        previous_driver = asio_driver_ids_[static_cast<std::size_t>(previous_index)];
    }
    SendMessageW(asio_driver_combo_, CB_RESETCONTENT, 0, 0);
    asio_driver_ids_.clear();
    if (audio_engine_ != nullptr) {
        for (const auto& driver : audio_engine_->Drivers()) {
            asio_driver_ids_.push_back(driver.driver_id);
            const auto label = Widen(driver.display_name);
            SendMessageW(asio_driver_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
    auto selected = 0L;
    if (!previous_driver.empty()) {
        const auto found = std::find(asio_driver_ids_.begin(), asio_driver_ids_.end(), previous_driver);
        if (found != asio_driver_ids_.end()) {
            selected = static_cast<LONG>(std::distance(asio_driver_ids_.begin(), found));
        }
    }
    if (!asio_driver_ids_.empty()) {
        SendMessageW(asio_driver_combo_, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }
}

std::optional<std::size_t> Win32RigWindow::SelectedAsioDriverIndex() const {
    if (asio_driver_combo_ == nullptr) {
        return std::nullopt;
    }
    const auto selected = SendMessageW(asio_driver_combo_, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<std::size_t>(selected) >= asio_driver_ids_.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(selected);
}

void Win32RigWindow::UpdateAudioStatus() {
    if (editor_ == nullptr || audio_engine_ == nullptr) {
        return;
    }
    editor_->SetAudioStatus(audio_engine_->RuntimeStatusText());
}

void Win32RigWindow::RefreshMidiMappingControls() {
    if (midi_target_combo_ == nullptr || midi_mapping_list_ == nullptr) {
        return;
    }
    const auto previous_target = SelectedMidiTarget();
    SendMessageW(midi_target_combo_, CB_RESETCONTENT, 0, 0);
    midi_target_ids_.clear();
    if (midi_session_ != nullptr) {
        for (const auto& target : midi_session_->ParameterTargets()) {
            midi_target_ids_.push_back(target.target_id);
            const auto label = Widen(target.display_name);
            SendMessageW(midi_target_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
    auto target_index = 0L;
    if (previous_target.has_value()) {
        const auto found = std::find(midi_target_ids_.begin(), midi_target_ids_.end(), *previous_target);
        if (found != midi_target_ids_.end()) {
            target_index = static_cast<LONG>(std::distance(midi_target_ids_.begin(), found));
        }
    }
    if (!midi_target_ids_.empty()) {
        SendMessageW(midi_target_combo_, CB_SETCURSEL, static_cast<WPARAM>(target_index), 0);
    }

    SendMessageW(midi_mapping_list_, LB_RESETCONTENT, 0, 0);
    if (midi_session_ != nullptr) {
        for (const auto& binding : midi_session_->Bindings()) {
            const auto label = MidiBindingText(binding);
            SendMessageW(midi_mapping_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
}

std::optional<std::string> Win32RigWindow::SelectedMidiTarget() const {
    if (midi_target_combo_ == nullptr) {
        return std::nullopt;
    }
    const auto selected = SendMessageW(midi_target_combo_, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<std::size_t>(selected) >= midi_target_ids_.size()) {
        return std::nullopt;
    }
    return midi_target_ids_[static_cast<std::size_t>(selected)];
}

bool Win32RigWindow::HandleFileAction(FileDialogAction action) {
    return action == FileDialogAction::Open ? TriggerOpen() : TriggerSave();
}

std::optional<std::filesystem::path> Win32RigWindow::ShowNativeFileDialog(FileDialogAction action) const {
    wchar_t filename[32768]{};
    if (editor_ != nullptr && editor_->SourcePath().has_value()) {
        const auto text = editor_->SourcePath()->wstring();
        wcsncpy_s(filename, text.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrFilter = L"SoloRig Preset (*.srig)\0*.srig\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrDefExt = L"srig";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    const auto accepted = action == FileDialogAction::Open ? GetOpenFileNameW(&dialog) :
                                                             GetSaveFileNameW(&dialog);
    return accepted != FALSE ? std::optional<std::filesystem::path>(std::filesystem::path(filename)) : std::nullopt;
}

void Win32RigWindow::UpdateWindowTitle() {
    if (window_ == nullptr || editor_ == nullptr) {
        return;
    }
    std::wstring title = L"SoloRig - Guitar Rig Editor";
    if (editor_->SourcePath().has_value()) {
        title += L" - ";
        title += editor_->SourcePath()->filename().wstring();
    }
    if (editor_->IsDirty()) {
        title += L" *";
    }
    SetWindowTextW(window_, title.c_str());
}

void Win32RigWindow::ReleaseRenderResources() noexcept {
    if (text_format_ != nullptr) {
        AsTextFormat(text_format_)->Release();
        text_format_ = nullptr;
    }
    if (render_target_ != nullptr) {
        AsRenderTarget(render_target_)->Release();
        render_target_ = nullptr;
    }
    if (dwrite_factory_ != nullptr) {
        AsDWriteFactory(dwrite_factory_)->Release();
        dwrite_factory_ = nullptr;
    }
    if (d2d_factory_ != nullptr) {
        AsD2DFactory(d2d_factory_)->Release();
        d2d_factory_ = nullptr;
    }
}

void Win32RigWindow::UpdateEditorViewport() {
    if (editor_ == nullptr || window_ == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    editor_->SetViewport(static_cast<double>(std::max<LONG>(1, client.right - client.left)),
                         static_cast<double>(std::max<LONG>(1, client.bottom - client.top)),
                         dpi_scale_);
}

} // namespace solo_rig::standalone
