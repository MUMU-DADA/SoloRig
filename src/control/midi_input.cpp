#include "control/midi_input.hpp"

#include <windows.h>
#include <mmsystem.h>

namespace solo_rig {

bool MidiMessageQueue::TryPush(const MidiMessage& message) noexcept {
    const auto write_index = write_index_.load(std::memory_order_relaxed);
    const auto next_write_index = (write_index + 1U) % Capacity;
    if (next_write_index == read_index_.load(std::memory_order_acquire)) {
        return false;
    }
    messages_[write_index] = message;
    write_index_.store(next_write_index, std::memory_order_release);
    return true;
}

std::optional<MidiMessage> MidiMessageQueue::TryPop() noexcept {
    const auto read_index = read_index_.load(std::memory_order_relaxed);
    if (read_index == write_index_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const auto message = messages_[read_index];
    read_index_.store((read_index + 1U) % Capacity, std::memory_order_release);
    return message;
}

namespace {

void CALLBACK MidiInputCallback(HMIDIIN,
                                UINT message,
                                DWORD_PTR instance,
                                DWORD_PTR parameter1,
                                DWORD_PTR) {
    if (message != MIM_DATA || instance == 0U) {
        return;
    }
    auto* input = reinterpret_cast<WindowsMidiInput*>(instance);
    input->OnPackedShortMessage(static_cast<std::uint32_t>(parameter1));
}

} // namespace

WindowsMidiInput::~WindowsMidiInput() {
    Close();
}

std::vector<MidiInputDevice> WindowsMidiInput::EnumerateDevices() {
    std::vector<MidiInputDevice> devices;
    const auto count = midiInGetNumDevs();
    devices.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        MIDIINCAPSW capabilities{};
        if (midiInGetDevCapsW(index, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR) {
            continue;
        }
        devices.push_back({.index = index, .display_name = capabilities.szPname});
    }
    return devices;
}

bool WindowsMidiInput::Open(std::uint32_t device_index) {
    Close();

    HMIDIIN handle = nullptr;
    const auto result = midiInOpen(&handle,
                                   device_index,
                                   reinterpret_cast<DWORD_PTR>(&MidiInputCallback),
                                   reinterpret_cast<DWORD_PTR>(this),
                                   CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        return false;
    }
    if (midiInStart(handle) != MMSYSERR_NOERROR) {
        midiInClose(handle);
        return false;
    }
    handle_ = reinterpret_cast<std::uintptr_t>(handle);
    return true;
}

void WindowsMidiInput::Close() noexcept {
    if (handle_ == 0U) {
        return;
    }
    const auto handle = reinterpret_cast<HMIDIIN>(handle_);
    midiInStop(handle);
    midiInReset(handle);
    midiInClose(handle);
    handle_ = 0U;
    decoder_.Reset();
}

bool WindowsMidiInput::IsOpen() const noexcept {
    return handle_ != 0U;
}

std::optional<MidiMessage> WindowsMidiInput::Poll() noexcept {
    return messages_.TryPop();
}

std::uint64_t WindowsMidiInput::DroppedMessageCount() const noexcept {
    return dropped_messages_.load(std::memory_order_acquire);
}

void WindowsMidiInput::OnPackedShortMessage(std::uint32_t packed_message) noexcept {
    const auto status = static_cast<std::uint8_t>(packed_message & 0xFFU);
    const auto first_data = static_cast<std::uint8_t>((packed_message >> 8U) & 0x7FU);
    const auto second_data = static_cast<std::uint8_t>((packed_message >> 16U) & 0x7FU);
    const auto emit = [this](std::optional<MidiMessage> message) noexcept {
        if (message.has_value()) {
            if (!messages_.TryPush(*message)) {
                dropped_messages_.fetch_add(1U, std::memory_order_relaxed);
            }
        }
    };

    emit(decoder_.PushByte(status));
    const auto channel_message_type = static_cast<std::uint8_t>(status & 0xF0U);
    if (channel_message_type == 0xC0U) {
        emit(decoder_.PushByte(first_data));
    } else if (channel_message_type == 0xB0U) {
        emit(decoder_.PushByte(first_data));
        emit(decoder_.PushByte(second_data));
    }
}

} // namespace solo_rig
