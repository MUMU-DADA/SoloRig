#include "standalone/windows_rig_window.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

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
    UpdateEditorViewport();
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
                      D2D1::RectF(22.0F, 18.0F, width - 20.0F, 52.0F), text);
    const auto status = Widen(std::string(scene.audio_status));
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
        InvalidateRect(window_, nullptr, FALSE);
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
        if (w_param == 'T' && editor_ != nullptr) {
            editor_->ToggleTheme();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window_, message, w_param, l_param);
    }
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
