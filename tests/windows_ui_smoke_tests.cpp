#include "test_framework.hpp"

#include "standalone/windows_rig_window.hpp"

TEST_CASE("Win32 standalone editor registers creates renders resizes and destroys a hidden window") {
    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    REQUIRE(window.InitializeRenderResources());
    REQUIRE(window.Handle() != nullptr);
    window.Resize(1024, 768, 1.25F);
    window.Paint();
    REQUIRE(window.LastPaintSucceeded());
    REQUIRE(window.PaintCount() > 0U);
    const auto original_x = editor.ExportPreset().nodes.front().layout_x;
    REQUIRE(SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(92, 198)) == 0);
    REQUIRE(SendMessageW(window.Handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(220, 260)) == 0);
    REQUIRE(SendMessageW(window.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(220, 260)) == 0);
    REQUIRE(editor.ExportPreset().nodes.front().layout_x != original_x);
    const auto dark_before = editor.IsDarkTheme();
    REQUIRE(SendMessageW(window.Handle(), WM_KEYDOWN, 'T', 0) == 0);
    REQUIRE(editor.IsDarkTheme() != dark_before);
    window.Destroy();
    REQUIRE(window.Handle() == nullptr);
}
