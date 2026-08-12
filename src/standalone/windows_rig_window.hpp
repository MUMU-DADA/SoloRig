#pragma once

#include "standalone/midi_control_session.hpp"
#include "ui/rig_editor.hpp"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>

namespace solo_rig::standalone {

enum class FileDialogAction {
    Open,
    Save,
};

using FileDialogProvider = std::function<std::optional<std::filesystem::path>(
    FileDialogAction action,
    const std::optional<std::filesystem::path>& current_path)>;

class Win32RigWindow final {
public:
    Win32RigWindow() = default;
    ~Win32RigWindow();

    Win32RigWindow(const Win32RigWindow&) = delete;
    Win32RigWindow& operator=(const Win32RigWindow&) = delete;

    [[nodiscard]] bool Create(HINSTANCE instance, RigEditorViewModel& editor, bool show);
    [[nodiscard]] bool InitializeRenderResources();
    void Resize(std::uint32_t width, std::uint32_t height, float dpi_scale);
    void Paint();
    void Destroy() noexcept;
    [[nodiscard]] int RunMessageLoop() const;
    [[nodiscard]] HWND Handle() const noexcept;
    [[nodiscard]] bool LastPaintSucceeded() const noexcept;
    [[nodiscard]] std::uint64_t PaintCount() const noexcept;
    void SetFileDialogProvider(FileDialogProvider provider);
    [[nodiscard]] bool TriggerSave();
    [[nodiscard]] bool TriggerSaveAs();
    [[nodiscard]] bool TriggerOpen();
    // Centralized command dispatch keeps keyboard shortcuts testable without
    // depending on the host keyboard state. WM_KEYDOWN forwards to this path.
    [[nodiscard]] bool HandleShortcut(std::uint32_t key, bool control, bool shift);
    void SetMidiControlSession(StandaloneMidiControlSession* session);
    [[nodiscard]] bool TriggerMidiRefresh();
    [[nodiscard]] bool TriggerMidiOpen();
    [[nodiscard]] bool TriggerMidiClose();
    [[nodiscard]] std::size_t DrainMidi();
    [[nodiscard]] bool MidiDrainTimerEnabled() const noexcept;

    [[nodiscard]] static bool RunSmokeTest(HINSTANCE instance);

private:
    static constexpr wchar_t kWindowClassName[] = L"SoloRigStandaloneWindow";

    [[nodiscard]] static bool RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void ReleaseRenderResources() noexcept;
    void UpdateEditorViewport();
    void CreateToolbarButtons();
    void LayoutToolbarButtons();
    [[nodiscard]] bool HandleFileAction(FileDialogAction action);
    [[nodiscard]] std::optional<std::filesystem::path> ShowNativeFileDialog(FileDialogAction action) const;
    void UpdateWindowTitle();

    HINSTANCE instance_{};
    HWND window_{};
    RigEditorViewModel* editor_{};
    void* d2d_factory_{};
    void* dwrite_factory_{};
    void* render_target_{};
    void* text_format_{};
    float dpi_scale_{1.0F};
    bool class_registered_{};
    bool last_paint_succeeded_{};
    std::uint64_t paint_count_{};
    HWND save_button_{};
    HWND save_as_button_{};
    HWND open_button_{};
    HWND midi_refresh_button_{};
    HWND midi_open_button_{};
    HWND midi_close_button_{};
    FileDialogProvider file_dialog_provider_;
    StandaloneMidiControlSession* midi_session_{};
    bool midi_timer_enabled_{};
};

} // namespace solo_rig::standalone
