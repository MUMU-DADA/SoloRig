#include "standalone/windows_rig_window.hpp"

#include <filesystem>
#include <iterator>

namespace {

std::filesystem::path ExecutableDirectory() {
    wchar_t buffer[32768]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return length == 0U ? std::filesystem::current_path() : std::filesystem::path(buffer, buffer + length).parent_path();
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    solo_rig::RigEditorViewModel editor;
    const auto resource_directory = ExecutableDirectory() / "resources";
    static_cast<void>(editor.LoadThemeFile(resource_directory / "themes" / "dark.json", false));
    static_cast<void>(editor.LoadSkinFile(resource_directory / "skins" / "default-pedal.json"));

    solo_rig::standalone::Win32RigWindow window;
    if (!window.Create(instance, editor, true)) {
        MessageBoxW(nullptr,
                    L"SoloRig could not initialize its Direct2D editor window.",
                    L"SoloRig",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    return window.RunMessageLoop();
}
