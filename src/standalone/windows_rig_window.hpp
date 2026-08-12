#pragma once

#include "ui/rig_editor.hpp"

#include <windows.h>

#include <cstdint>

namespace solo_rig::standalone {

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

    [[nodiscard]] static bool RunSmokeTest(HINSTANCE instance);

private:
    static constexpr wchar_t kWindowClassName[] = L"SoloRigStandaloneWindow";

    [[nodiscard]] static bool RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    void ReleaseRenderResources() noexcept;
    void UpdateEditorViewport();

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
};

} // namespace solo_rig::standalone
