#include "test_framework.hpp"

#include "standalone/windows_rig_window.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeWindowMidiSource final : public solo_rig::standalone::MidiInputSource {
public:
    std::vector<solo_rig::MidiInputDevice> EnumerateDevices() override {
        ++enumerate_count;
        return {{.index = 9U, .display_name = L"Window Fake MIDI"}};
    }

    bool Open(std::uint32_t index) override {
        ++open_count;
        open = index == 9U;
        return open;
    }

    void Close() noexcept override {
        ++close_count;
        open = false;
    }

    bool IsOpen() const noexcept override {
        return open;
    }

    std::optional<solo_rig::MidiMessage> Poll() noexcept override {
        ++poll_count;
        return queue.TryPop();
    }

    std::uint64_t DroppedMessageCount() const noexcept override {
        return 0U;
    }

    void Inject(const solo_rig::MidiMessage& message) noexcept {
        static_cast<void>(queue.TryPush(message));
    }

    solo_rig::MidiMessageQueue queue;
    bool open{};
    std::size_t enumerate_count{};
    std::size_t open_count{};
    std::size_t close_count{};
    std::size_t poll_count{};
};

} // namespace

TEST_CASE("Win32 standalone editor registers creates renders resizes and destroys a hidden window") {
    REQUIRE(solo_rig::standalone::StandaloneAudioEngine::RunTunerCaptureSelfTest());
    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    REQUIRE(window.InitializeRenderResources());
    REQUIRE(window.Handle() != nullptr);
    const auto asio_route_label = GetDlgItem(window.Handle(), 1019);
    const auto asio_route_edit = GetDlgItem(window.Handle(), 1020);
    const auto asio_recover_button = GetDlgItem(window.Handle(), 1023);
    const auto tuner_button = GetDlgItem(window.Handle(), 1024);
    REQUIRE(asio_route_label != nullptr);
    REQUIRE(asio_route_edit != nullptr);
    REQUIRE(asio_recover_button != nullptr);
    REQUIRE(tuner_button != nullptr);
    solo_rig::standalone::StandaloneAudioEngine audio_engine;
    window.SetAudioEngine(&audio_engine);
    REQUIRE(!audio_engine.TunerEnabled());
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1024, BN_CLICKED), 0) == 0);
    REQUIRE(audio_engine.TunerEnabled());
    REQUIRE(editor.AudioStatus().find("tuner") != std::string_view::npos);
    wchar_t route_text[128]{};
    REQUIRE(GetWindowTextW(asio_route_edit, route_text, static_cast<int>(std::size(route_text))) > 0);
    REQUIRE(std::wstring(route_text).find(L"0:in=0,1;out=0,1") != std::wstring::npos);
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

TEST_CASE("Win32 standalone editor controls MIDI session by messages without a startup timer") {
    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeWindowMidiSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(session.ConfigureBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                        .channel = 0,
                                        .number = 11,
                                        .target_kind = solo_rig::MidiTargetKind::Parameter,
                                        .target_id = "drive-1.drive",
                                        .minimum = 0.0F,
                                        .maximum = 1.0F}}));

    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    window.SetMidiControlSession(&session);
    REQUIRE(!window.MidiDrainTimerEnabled());
    REQUIRE(!session.IsOpen());
    REQUIRE(session.StatusText().find("MIDI: closed") != std::string::npos);

    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1004, BN_CLICKED), 0) == 0);
    REQUIRE(!window.MidiDrainTimerEnabled());
    REQUIRE(source->enumerate_count == 1U);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1005, BN_CLICKED), 0) == 0);
    REQUIRE(window.MidiDrainTimerEnabled());
    REQUIRE(session.IsOpen());
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 11,
                    .value = 127});
    REQUIRE(SendMessageW(window.Handle(), WM_TIMER, 1U, 0) == 0);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == 1.0F);
    REQUIRE(editor.IsDirty());
    REQUIRE(session.StatusText().find("updated editor") != std::string::npos);
    window.Paint();
    REQUIRE(window.LastPaintSucceeded());

    const auto polls_before_close = source->poll_count;
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1006, BN_CLICKED), 0) == 0);
    REQUIRE(!window.MidiDrainTimerEnabled());
    REQUIRE(!session.IsOpen());
    REQUIRE(SendMessageW(window.Handle(), WM_TIMER, 1U, 0) == 0);
    REQUIRE(source->poll_count == polls_before_close);
    REQUIRE(window.Handle() != nullptr);
    REQUIRE(window.TriggerMidiOpen());
    REQUIRE(source->open);
    const auto close_count_before_destroy = source->close_count;
    window.Destroy();
    REQUIRE(window.Handle() == nullptr);
    REQUIRE(!source->open);
    REQUIRE(source->close_count > close_count_before_destroy);
    REQUIRE(!window.MidiDrainTimerEnabled());
}

TEST_CASE("Win32 standalone editor closes a replaced MIDI session before rebinding") {
    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    auto first_fake = std::make_unique<FakeWindowMidiSource>();
    auto* first_source = first_fake.get();
    auto second_fake = std::make_unique<FakeWindowMidiSource>();
    auto* second_source = second_fake.get();
    solo_rig::standalone::StandaloneMidiControlSession first_session(editor, std::move(first_fake));
    solo_rig::standalone::StandaloneMidiControlSession second_session(editor, std::move(second_fake));

    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    window.SetMidiControlSession(&first_session);
    REQUIRE(window.TriggerMidiRefresh());
    REQUIRE(window.TriggerMidiOpen());
    REQUIRE(first_source->open);
    const auto close_count_before_replace = first_source->close_count;

    window.SetMidiControlSession(&second_session);
    REQUIRE(first_source->close_count > close_count_before_replace);
    REQUIRE(!first_session.IsOpen());
    REQUIRE(!first_source->open);
    REQUIRE(!second_session.IsOpen());
    REQUIRE(!second_source->open);
    REQUIRE(!window.MidiDrainTimerEnabled());
    window.Destroy();
}

TEST_CASE("Win32 standalone MIDI mapping controls perform CC learn delete and clear") {
    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeWindowMidiSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    window.SetMidiControlSession(&session);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1004, BN_CLICKED), 0) == 0);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1005, BN_CLICKED), 0) == 0);
    REQUIRE(session.IsOpen());

    const auto target_combo = GetDlgItem(window.Handle(), 1007);
    const auto mapping_list = GetDlgItem(window.Handle(), 1012);
    REQUIRE(target_combo != nullptr);
    REQUIRE(mapping_list != nullptr);
    REQUIRE(SendMessageW(target_combo, CB_GETCOUNT, 0, 0) >= 2);
    const auto target_index = SendMessageW(target_combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                           reinterpret_cast<LPARAM>(L"delay-1.time"));
    REQUIRE(target_index >= 0);
    REQUIRE(SendMessageW(target_combo, CB_SETCURSEL, static_cast<WPARAM>(target_index), 0) >= 0);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1007, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(target_combo)) == 0);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1008, BN_CLICKED), 0) == 0);
    REQUIRE(session.IsLearnArmed());
    REQUIRE(session.StatusText().find("learn armed") != std::string::npos);

    const auto before_learn = editor.ExportPreset().nodes.at(1).parameter_values.at("time");
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 74,
                    .value = 127});
    REQUIRE(SendMessageW(window.Handle(), WM_TIMER, 1U, 0) == 0);
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(editor.ExportPreset().nodes.at(1).parameter_values.at("time") == before_learn);
    REQUIRE(editor.IsDirty());
    REQUIRE(session.StatusText().find("learned") != std::string::npos);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);

    REQUIRE(SendMessageW(mapping_list, LB_SETCURSEL, 0, 0) >= 0);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1010, BN_CLICKED), 0) == 0);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 0);

    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1008, BN_CLICKED), 0) == 0);
    REQUIRE(session.IsLearnArmed());
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1009, BN_CLICKED), 0) == 0);
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1008, BN_CLICKED), 0) == 0);
    REQUIRE(session.IsLearnArmed());
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 75,
                    .value = 64});
    REQUIRE(SendMessageW(window.Handle(), WM_TIMER, 1U, 0) == 0);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1011, BN_CLICKED), 0) == 0);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 0);
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1008, BN_CLICKED), 0) == 0);
    REQUIRE(session.IsLearnArmed());
    REQUIRE(SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(1006, BN_CLICKED), 0) == 0);
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(!session.IsOpen());
    REQUIRE(!window.MidiDrainTimerEnabled());
    window.Destroy();
}

TEST_CASE("Win32 standalone MIDI session syncs loaded bindings and preserves learn on cancel or error") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto valid_path = directory / "solorig-midi-learn-valid.srig";
    const auto corrupt_path = directory / "solorig-midi-learn-corrupt.srig";
    std::error_code cleanup;
    std::filesystem::remove(valid_path, cleanup);
    std::filesystem::remove(corrupt_path, cleanup);

    solo_rig::RigEditorViewModel new_document;
    REQUIRE(new_document.SetMidiBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                           .channel = 0,
                                           .number = 22,
                                           .target_kind = solo_rig::MidiTargetKind::Parameter,
                                           .target_id = "delay-1.time",
                                           .minimum = 0.0F,
                                           .maximum = 2.0F}}));
    REQUIRE(new_document.SaveTo(valid_path).success);
    {
        std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }

    const auto instance = GetModuleHandleW(nullptr);
    REQUIRE(instance != nullptr);
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeWindowMidiSource>();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(session.ConfigureBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                        .channel = 0,
                                        .number = 11,
                                        .target_kind = solo_rig::MidiTargetKind::Parameter,
                                        .target_id = "drive-1.drive",
                                        .minimum = 0.0F,
                                        .maximum = 16.0F}}));
    solo_rig::standalone::Win32RigWindow window;
    REQUIRE(window.Create(instance, editor, false));
    window.SetMidiControlSession(&session);
    REQUIRE(window.TriggerMidiRefresh());
    REQUIRE(window.TriggerMidiOpen());
    REQUIRE(window.TriggerMidiLearn());
    REQUIRE(session.IsLearnArmed());
    const auto before_cancel_preset = editor.ExportPreset();
    const auto before_cancel_source = editor.SourcePath();
    const auto before_cancel_dirty = editor.IsDirty();
    const auto mapping_list = GetDlgItem(window.Handle(), 1012);
    REQUIRE(mapping_list != nullptr);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);
    bool cancel = true;
    bool error = false;
    window.SetFileDialogProvider([&](solo_rig::standalone::FileDialogAction action,
                                     const std::optional<std::filesystem::path>&) {
        if (action != solo_rig::standalone::FileDialogAction::Open) {
            return std::optional<std::filesystem::path>{};
        }
        if (cancel) {
            return std::optional<std::filesystem::path>{};
        }
        return std::optional<std::filesystem::path>(error ? corrupt_path : valid_path);
    });
    REQUIRE(!window.TriggerOpen());
    REQUIRE(session.IsLearnArmed());
    REQUIRE(session.Bindings().size() == 1U);
    REQUIRE(session.Bindings().front().number == 11U);
    REQUIRE(session.Bindings().front().target_id == "drive-1.drive");
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") ==
            before_cancel_preset.nodes.front().parameter_values.at("drive"));
    REQUIRE(editor.SourcePath() == before_cancel_source);
    REQUIRE(editor.IsDirty() == before_cancel_dirty);

    cancel = false;
    error = true;
    REQUIRE(!window.TriggerOpen());
    REQUIRE(session.IsLearnArmed());
    REQUIRE(session.Bindings().size() == 1U);
    REQUIRE(session.Bindings().front().number == 11U);
    REQUIRE(session.Bindings().front().target_id == "drive-1.drive");
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);
    REQUIRE(editor.SourcePath() == before_cancel_source);
    REQUIRE(editor.IsDirty() == before_cancel_dirty);

    error = false;
    REQUIRE(window.TriggerOpen());
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(!editor.IsDirty());
    REQUIRE(session.Bindings().size() == 1U);
    REQUIRE(session.Bindings().front().target_id == "delay-1.time");
    REQUIRE(session.Bindings().front().number == 22U);
    REQUIRE(SendMessageW(mapping_list, LB_GETCOUNT, 0, 0) == 1);
    wchar_t mapping_text[256]{};
    REQUIRE(SendMessageW(mapping_list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(mapping_text)) > 0);
    REQUIRE(std::wstring(mapping_text).find(L"#22") != std::wstring::npos);
    window.Destroy();
    std::filesystem::remove(valid_path, cleanup);
    std::filesystem::remove(corrupt_path, cleanup);
}
