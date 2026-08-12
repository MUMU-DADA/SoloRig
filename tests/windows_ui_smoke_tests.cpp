#include "test_framework.hpp"

#include "standalone/windows_rig_window.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
    wchar_t title[512]{};
    REQUIRE(GetWindowTextW(window.Handle(), title, static_cast<int>(std::size(title))) > 0);
    REQUIRE(std::wstring(title).find(L"*") != std::wstring::npos);
    window.Destroy();
    REQUIRE(window.Handle() == nullptr);
}

TEST_CASE("Win32 standalone editor routes toolbar and shortcut persistence commands with cancel and error recovery") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto save_path = directory / "solorig-ui-toolbar.srig";
    const auto save_as_path = directory / "solorig-ui-shortcut.srig";
    const auto open_path = directory / "solorig-ui-open.srig";
    const auto corrupt_path = directory / "solorig-ui-corrupt.srig";
    std::error_code cleanup;
    for (const auto& path : {save_path, save_as_path, open_path, corrupt_path}) {
        std::filesystem::remove(path, cleanup);
    }
    solo_rig::RigEditorViewModel source;
    REQUIRE(source.SaveTo(open_path).success);
    {
        std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }

    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    REQUIRE(window.InitializeRenderResources());
    wchar_t title[512]{};
    std::vector<solo_rig::standalone::FileDialogAction> actions;
    bool cancel_dialog = false;
    bool error_dialog = false;
    window.SetFileDialogProvider([&](solo_rig::standalone::FileDialogAction action,
                                     const std::optional<std::filesystem::path>&) {
        actions.push_back(action);
        if (cancel_dialog) {
            return std::optional<std::filesystem::path>{};
        }
        if (error_dialog && action == solo_rig::standalone::FileDialogAction::Open) {
            return std::optional<std::filesystem::path>(corrupt_path);
        }
        if (action == solo_rig::standalone::FileDialogAction::Open) {
            return std::optional<std::filesystem::path>(open_path);
        }
        return std::optional<std::filesystem::path>(actions.size() == 1U ? save_path : save_as_path);
    });

    // WM_COMMAND exercises all three real toolbar buttons. Save on a new
    // document invokes Save As semantics and writes an actual .srig file.
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), 0) == 0);
    REQUIRE(std::filesystem::exists(save_path));
    REQUIRE(!editor.IsDirty());
    REQUIRE(GetWindowTextW(window.Handle(), title, static_cast<int>(std::size(title))) > 0);
    REQUIRE(std::wstring(title).find(save_path.filename().wstring()) != std::wstring::npos);

    REQUIRE(editor.SetNodeParameter("drive-1", "drive", 6.0F));
    REQUIRE(editor.IsDirty());
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0) == 0);
    REQUIRE(std::filesystem::exists(save_as_path));
    REQUIRE(!editor.IsDirty());

    // Ctrl+S, Ctrl+Shift+S and Ctrl+O share the production dispatch path.
    REQUIRE(editor.SetNodeParameter("drive-1", "drive", 5.0F));
    REQUIRE(window.HandleShortcut('S', true, false));
    REQUIRE(!editor.IsDirty());
    REQUIRE(window.HandleShortcut('S', true, true));
    REQUIRE(window.HandleShortcut('O', true, false));
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") ==
            source.ExportPreset().nodes.front().parameter_values.at("drive"));

    const auto before_failed_load = editor.ExportPreset();
    const auto before_source = editor.SourcePath();
    const auto before_dirty = editor.IsDirty();
    cancel_dialog = true;
    REQUIRE(!SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0));
    REQUIRE(!window.TriggerOpen());
    REQUIRE(window.Handle() != nullptr);
    REQUIRE(editor.SourcePath() == before_source);
    REQUIRE(editor.IsDirty() == before_dirty);
    cancel_dialog = false;
    error_dialog = true;
    REQUIRE(!SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1003, BN_CLICKED), 0));
    REQUIRE(window.Handle() != nullptr);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") ==
            before_failed_load.nodes.front().parameter_values.at("drive"));
    REQUIRE(editor.SourcePath() == before_source);
    REQUIRE(editor.IsDirty() == before_dirty);
    REQUIRE(std::ranges::find(actions, solo_rig::standalone::FileDialogAction::Save) != actions.end());
    REQUIRE(std::ranges::find(actions, solo_rig::standalone::FileDialogAction::Open) != actions.end());

    window.Destroy();
    REQUIRE(window.Handle() == nullptr);
    for (const auto& path : {save_path, save_as_path, open_path, corrupt_path}) {
        std::filesystem::remove(path, cleanup);
    }
}
