#pragma once

#include "control/midi_mapping.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace solo_rig {

// Single-producer/single-consumer queue for crossing from an OS MIDI callback
// to the control thread without mutexes or dynamic allocation.
class MidiMessageQueue {
public:
    static constexpr std::size_t Capacity = 256;

    [[nodiscard]] bool TryPush(const MidiMessage& message) noexcept;
    [[nodiscard]] std::optional<MidiMessage> TryPop() noexcept;

private:
    std::array<MidiMessage, Capacity> messages_{};
    std::atomic<std::size_t> write_index_{};
    std::atomic<std::size_t> read_index_{};
};

struct MidiInputDevice {
    std::uint32_t index{};
    std::wstring display_name;
};

// Windows standalone adapter. Hosts normally supply MIDI directly to VST3;
// the standalone executable can use this adapter and drain Poll() on its
// control thread before applying parameter changes to the audio graph.
class WindowsMidiInput {
public:
    WindowsMidiInput() = default;
    ~WindowsMidiInput();

    WindowsMidiInput(const WindowsMidiInput&) = delete;
    WindowsMidiInput& operator=(const WindowsMidiInput&) = delete;

    [[nodiscard]] static std::vector<MidiInputDevice> EnumerateDevices();
    [[nodiscard]] bool Open(std::uint32_t device_index);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] std::optional<MidiMessage> Poll() noexcept;

    // Called by the WinMM callback with one packed short MIDI message.
    void OnPackedShortMessage(std::uint32_t packed_message) noexcept;

private:
    std::uintptr_t handle_{};
    MidiStreamDecoder decoder_;
    MidiMessageQueue messages_;
};

} // namespace solo_rig
