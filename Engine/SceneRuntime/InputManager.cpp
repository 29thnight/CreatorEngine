#include "InputManager.h"
#include "LogSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>

#pragma comment(lib, "GameInput.lib")

namespace
{
    constexpr std::array<GameInputGamepadButtons, 14> kPadButtons = {
        GameInputGamepadA, GameInputGamepadB, GameInputGamepadX, GameInputGamepadY,
        GameInputGamepadDPadUp, GameInputGamepadDPadDown, GameInputGamepadDPadLeft, GameInputGamepadDPadRight,
        GameInputGamepadMenu, GameInputGamepadView, GameInputGamepadLeftShoulder, GameInputGamepadRightShoulder,
        GameInputGamepadLeftThumbstick, GameInputGamepadRightThumbstick};
    constexpr auto kCaptureKinds = GameInputKindKeyboard | GameInputKindMouse | GameInputKindGamepad;
}

InputManager::~InputManager()
{
    Shutdown();
}

bool InputManager::Initialize(HWND window)
{
    if (m_gameInput)
    {
        return true;
    }
    if (FAILED(GameInputCreate(&m_gameInput)))
    {
        return false;
    }
    m_hwnd = window;
    m_clockReal = Now();
    m_gameInput->SetFocusPolicy(GameInputEnableBackgroundInput);
    // v3 callbacks are serialized by GameInput's existing worker. Our mutex also
    // orders GT policy changes with those callbacks and protects the finite log.
    if (FAILED(m_gameInput->RegisterReadingCallback(nullptr, kCaptureKinds, this, &OnReading, &m_readingToken)) ||
        FAILED(m_gameInput->RegisterDeviceCallback(nullptr, kCaptureKinds, GameInputDeviceConnected,
            GameInputAsyncEnumeration, this, &OnDevice, &m_deviceToken)))
    {
        Shutdown();
        return false;
    }
    return true;
}

void InputManager::Shutdown()
{
    if (!m_gameInput)
    {
        return;
    }
    // Never unregister while holding the callback mutex: unregister joins delivery.
    if (m_readingToken)
    {
        m_gameInput->UnregisterCallback(m_readingToken);
        m_readingToken = {};
    }
    if (m_deviceToken)
    {
        m_gameInput->UnregisterCallback(m_deviceToken);
        m_deviceToken = {};
    }
    std::lock_guard lock(m_captureMutex);
    for (auto& device : m_devices)
    {
        StopRumble(device);
        device.native.Reset();
    }
    m_records.clear();
    m_gameInput.Reset();
}

Input::Timestamp InputManager::Now() const noexcept
{
    return m_gameInput ? static_cast<Input::Timestamp>(m_gameInput->GetCurrentTimestamp() * 1000) : 0;
}

Input::Timestamp InputManager::GameTimeAt(Input::Timestamp realTime) const noexcept
{
    return m_clockGame + static_cast<Input::Timestamp>(
        static_cast<double>((std::max)(Input::Timestamp{}, realTime - m_clockReal)) * m_timeScale);
}

void InputManager::Append(Input::RoutedInputRecord record)
{
    record.receivedTime = Now();
    record.realTime = record.sourceTime;
    if (record.realTime < m_lastTimestamp)
    {
        record.realTime = m_lastTimestamp;
        record.late = true;
    }
    m_lastTimestamp = record.realTime;
    record.gameTime = GameTimeAt(record.realTime);
    record.sequence = ++m_sequence;
    record.routingEpoch = m_routingEpoch;
    while (!m_records.empty() && (m_records.size() >= kRecordCapacity ||
        record.realTime - m_records.front().realTime > kRetention))
    {
        m_records.pop_front();
        ++m_droppedRecords;
        m_historyGap = true;
    }
    m_records.push_back(record);
    m_highWaterRecords = (std::max)(m_highWaterRecords, m_records.size());
}

void InputManager::EmitPolicy(Input::RecordKind kind, Input::CancelReason reason,
    Input::Recipient recipients, bool enabled)
{
    Input::RoutedInputRecord record;
    record.kind = kind;
    record.sourceTime = Now();
    record.reason = reason;
    record.recipients = recipients;
    record.enabled = enabled;
    ++m_routingEpoch;
    Append(record);
}

InputManager::Device* InputManager::FindDevice(IGameInputDevice* native)
{
    for (auto& device : m_devices)
    {
        if (device.native.Get() == native)
        {
            return &device;
        }
    }
    return nullptr;
}

InputManager::Device* InputManager::AddDevice(IGameInputDevice* native, GameInputKind kind,
    Input::Timestamp timestamp)
{
    if (auto* device = FindDevice(native))
    {
        return device;
    }
    for (size_t slot = 0; slot < m_devices.size(); ++slot)
    {
        auto& device = m_devices[slot];
        if (device.native)
        {
            continue;
        }
        device.native = native;
        device.id = slot + 1;
        ++device.epoch;
        ++device.assignmentEpoch;
        device.controls.clear();
        device.controls.reserve(256);
        device.initialized = false;
        device.controllerIndex = -1;
        device.kind = (kind & GameInputKindGamepad) ? Input::DeviceKind::Gamepad :
            (kind & GameInputKindKeyboard) ? Input::DeviceKind::Keyboard : Input::DeviceKind::Mouse;
        device.user = 1;
        if (device.kind == Input::DeviceKind::Gamepad)
        {
            for (int index = 0; index < static_cast<int>(kMaxController); ++index)
            {
                const bool occupied = std::ranges::any_of(m_devices, [index, &device](const Device& other) {
                    return &other != &device && other.native && other.controllerIndex == index;
                });
                if (!occupied)
                {
                    device.controllerIndex = index;
                    device.user = static_cast<Input::UserID>(index + 1);
                    for (const auto& assignment : m_assignments)
                    {
                        if (assignment.controllerIndex == index)
                        {
                            device.user = assignment.user;
                        }
                    }
                    break;
                }
            }
        }
        Input::RoutedInputRecord record;
        record.kind = Input::RecordKind::DeviceAssigned;
        record.sourceTime = timestamp;
        record.device = device.id;
        record.deviceEpoch = device.epoch;
        record.assignmentEpoch = device.assignmentEpoch;
        record.user = device.user;
        record.enabled = true;
        Append(record);
        return &device;
    }
    m_historyGap = true;
    return nullptr;
}

void CALLBACK InputManager::OnDevice(GameInputCallbackToken, void* context, IGameInputDevice* native,
    std::uint64_t timestamp, GameInputDeviceStatus current, GameInputDeviceStatus)
{
    auto& self = *static_cast<InputManager*>(context);
    try
    {
        const bool connected = (current & GameInputDeviceConnected) != 0;
        {
            std::lock_guard lock(self.m_captureMutex);
            if (!connected)
            {
                if (auto* device = self.FindDevice(native))
                {
                    self.StopRumble(*device);
                    Input::RoutedInputRecord record;
                    record.kind = Input::RecordKind::DeviceDisconnected;
                    record.reason = Input::CancelReason::DeviceDisconnected;
                    record.sourceTime = static_cast<Input::Timestamp>(timestamp * 1000);
                    record.device = device->id;
                    record.deviceEpoch = device->epoch;
                    record.assignmentEpoch = device->assignmentEpoch;
                    // A shared keyboard disconnect cancels every assigned user's state.
                    record.user = 0;
                    self.Append(record);
                    device->native.Reset();
                    device->controls.clear();
                    device->initialized = false;
                }
                return;
            }
        }
        // Seed devices even when no key changes after connection. These are Resync
        // records, so connecting while held cannot manufacture a new press.
        ComPtr<IGameInputReading> reading;
        if (SUCCEEDED(self.m_gameInput->GetCurrentReading(kCaptureKinds, native, &reading)) && reading)
        {
            self.CaptureReading(reading.Get());
        }
    }
    catch (...)
    {
        std::lock_guard lock(self.m_captureMutex);
        self.m_historyGap = true;
    }
}

void CALLBACK InputManager::OnReading(GameInputCallbackToken, void* context, IGameInputReading* reading)
{
    auto& self = *static_cast<InputManager*>(context);
    try
    {
        self.CaptureReading(reading);
    }
    catch (...)
    {
        // Exceptions cannot cross the GameInput callback ABI. Recovery is visible
        // to both cursors as a history gap at the next owner-thread drain.
        std::lock_guard lock(self.m_captureMutex);
        self.m_historyGap = true;
    }
}

void InputManager::Emit(Device& device, Input::Timestamp timestamp, Input::ControlID control,
    Input::InputValue value, bool delta, bool resync)
{
    auto found = std::ranges::find(device.controls, control, &CapturedControl::id);
    if (!delta && !resync && found != device.controls.end() && found->value == value)
    {
        return;
    }
    if (!delta)
    {
        if (found == device.controls.end())
        {
            if (device.controls.size() >= 1024)
            {
                m_historyGap = true;
                return;
            }
            device.controls.push_back({control, value});
        }
        else
        {
            found->value = value;
        }
    }
    Input::RoutedInputRecord record;
    record.kind = resync ? Input::RecordKind::Resync : Input::RecordKind::Control;
    record.sourceTime = timestamp;
    record.device = device.id;
    record.deviceEpoch = device.epoch;
    record.assignmentEpoch = device.assignmentEpoch;
    record.user = device.user;
    record.control = control;
    record.value = value;
    const bool keyboard = control.kind == Input::SourceKind::Key;
    const bool mouse = device.kind == Input::DeviceKind::Mouse;
    const bool captured = (keyboard && m_keyboardCaptured) || (mouse && m_mouseCaptured);
    const auto route = [&](Input::UserID user) {
        if (user == 0)
        {
            return;
        }
        record.user = user;
        record.recipients = !m_focused || !m_gameInputOwned || captured ? Input::Recipient::None :
            m_paused ? Input::Recipient::UI : Input::Recipient::Both;
        if (record.recipients == Input::Recipient::Both && std::ranges::any_of(m_claims,
            [&record](const Claim& claim) {
                return claim.ownerEnabled && claim.enabled && (claim.policy == Input::ClaimPolicy::OnPress || claim.active) &&
                    claim.user == record.user && claim.control == record.control;
            }))
        {
            // OnPress candidates reserve the source before the original publication.
            record.recipients = Input::Recipient::UI;
        }
        if (std::ranges::find(m_rebindUsers, user) != m_rebindUsers.end())
        {
            record.recipients = Input::Recipient::None;
        }
        Append(record);
    };
    route(device.user);
    if (device.kind != Input::DeviceKind::Gamepad)
    {
        for (const auto& assignment : m_assignments)
        {
            if (assignment.shareKeyboard && assignment.user != device.user)
            {
                route(assignment.user);
            }
        }
    }
}

void InputManager::CaptureReading(IGameInputReading* reading)
{
    if (!reading)
    {
        return;
    }
    ComPtr<IGameInputDevice> native;
    reading->GetDevice(&native);
    if (!native || !(native->GetDeviceStatus() & GameInputDeviceConnected))
    {
        return;
    }
    std::lock_guard lock(m_captureMutex);
    const auto timestamp = static_cast<Input::Timestamp>(reading->GetTimestamp() * 1000);
    auto* device = AddDevice(native.Get(), reading->GetInputKind(), timestamp);
    if (!device)
    {
        return;
    }
    const bool resync = !device->initialized;
    if (device->kind == Input::DeviceKind::Keyboard)
    {
        std::array<GameInputKeyState, 256> keys{};
        if (reading->GetKeyCount() > keys.size())
        {
            m_historyGap = true;
            return;
        }
        const auto count = reading->GetKeyState(static_cast<std::uint32_t>(keys.size()), keys.data());
        // Copy old IDs: Emit may grow the control array, but release traversal cannot.
        for (auto& control : device->controls)
        {
            const bool held = std::any_of(keys.begin(), keys.begin() + count, [&control](const auto& key) {
                return key.scanCode == control.id.code;
            });
            if (!held && control.value.x != 0)
            {
                Emit(*device, timestamp, control.id, Input::InputValue::FromButton(false), false, resync);
            }
        }
        for (std::uint32_t index = 0; index < count; ++index)
        {
            Emit(*device, timestamp, {Input::SourceKind::Key, keys[index].scanCode},
                Input::InputValue::FromButton(true), false, resync);
        }
    }
    else if (device->kind == Input::DeviceKind::Mouse)
    {
        GameInputMouseState mouse{};
        if (!reading->GetMouseState(&mouse))
        {
            return;
        }
        POINT point{};
        if (GetCursorPos(&point) && ScreenToClient(m_hwnd, &point))
        {
            Emit(*device, timestamp, {Input::SourceKind::PointerPosition, 0},
                Input::InputValue::FromVector2({static_cast<float>(point.x), static_cast<float>(point.y)}), false, resync);
        }
        for (std::uint32_t button = 0; button < 7; ++button)
        {
            Emit(*device, timestamp, {Input::SourceKind::MouseButton, button},
                Input::InputValue::FromButton((static_cast<unsigned>(mouse.buttons) & (1u << button)) != 0),
                false, resync);
        }
        if (!resync && (mouse.positions & GameInputMouseRelativePosition))
        {
            const Input::InputVector2 delta{static_cast<float>(mouse.positionX - device->mouse.positionX),
                static_cast<float>(mouse.positionY - device->mouse.positionY)};
            if (delta.x != 0 || delta.y != 0)
            {
                Emit(*device, timestamp, {Input::SourceKind::MouseDelta, 0},
                    Input::InputValue::FromVector2(delta), true);
            }
        }
        if (!resync)
        {
            const auto wheel = mouse.wheelY - device->mouse.wheelY;
            if (wheel != 0)
            {
                Emit(*device, timestamp, {Input::SourceKind::MouseWheel, 0},
                    Input::InputValue::FromFloat(static_cast<float>(wheel)), true);
            }
        }
        device->mouse = mouse;
    }
    else
    {
        GameInputGamepadState pad{};
        if (!reading->GetGamepadState(&pad))
        {
            return;
        }
        for (std::uint32_t button = 0; button < kPadButtons.size(); ++button)
        {
            Emit(*device, timestamp, {Input::SourceKind::GamepadButton, button},
                Input::InputValue::FromButton((pad.buttons & kPadButtons[button]) != 0), false, resync);
        }
        Emit(*device, timestamp, {Input::SourceKind::GamepadAxis, 0},
            Input::InputValue::FromVector2({pad.leftThumbstickX, pad.leftThumbstickY}), false, resync);
        Emit(*device, timestamp, {Input::SourceKind::GamepadAxis, 1},
            Input::InputValue::FromVector2({pad.rightThumbstickX, pad.rightThumbstickY}), false, resync);
        Emit(*device, timestamp, {Input::SourceKind::GamepadAxis, 2},
            Input::InputValue::FromFloat(pad.leftTrigger), false, resync);
        Emit(*device, timestamp, {Input::SourceKind::GamepadAxis, 3},
            Input::InputValue::FromFloat(pad.rightTrigger), false, resync);
    }
    device->initialized = true;
}

void InputManager::ResyncDevices()
{
    const auto now = Now();
    for (auto& device : m_devices)
    {
        if (!device.native)
        {
            continue;
        }
        for (const auto& control : device.controls)
        {
            Emit(device, now, control.id, control.value, false, true);
        }
    }
}

void InputManager::DrainRecords(std::vector<Input::RoutedInputRecord>& records, bool& historyGap)
{
    std::lock_guard lock(m_captureMutex);
    historyGap = m_historyGap || (!m_records.empty() && Now() - m_records.front().realTime > kRetention);
    if (historyGap)
    {
        m_records.clear();
        m_historyGap = false;
        EmitPolicy(Input::RecordKind::HistoryGap, Input::CancelReason::HistoryGap, Input::Recipient::Both, false);
        ResyncDevices();
    }
    records.assign(m_records.begin(), m_records.end());
    m_records.clear();
}

void InputManager::SetWindowFocused(bool focused)
{
    std::lock_guard lock(m_captureMutex);
    if (m_focused == focused)
    {
        return;
    }
    m_focused = focused;
    EmitPolicy(focused ? Input::RecordKind::FocusGained : Input::RecordKind::FocusLost,
        focused ? Input::CancelReason::None : Input::CancelReason::FocusLost, Input::Recipient::Both, focused);
    if (focused)
    {
        ResyncDevices();
    }
    else
    {
        for (auto& device : m_devices)
        {
            StopRumble(device);
        }
    }
}

void InputManager::SetGameInputOwned(bool owned)
{
    {
        std::lock_guard lock(m_captureMutex);
        if (m_gameInputOwned == owned)
        {
            return;
        }
        m_gameInputOwned = owned;
        EmitPolicy(Input::RecordKind::OwnershipChanged, Input::CancelReason::UIOwnership,
            Input::Recipient::Both, owned);
        if (owned)
        {
            ResyncDevices();
        }
        else
        {
            for (auto& device : m_devices)
            {
                StopRumble(device);
            }
        }
    }
    ApplyCursorHidden(owned && m_wantCursorHidden);
}

void InputManager::CommitRoutingPolicy(std::uint64_t generation, bool keyboardCaptured, bool mouseCaptured)
{
    std::lock_guard lock(m_captureMutex);
    if (generation <= m_policyGeneration)
    {
        return;
    }
    m_policyGeneration = generation;
    if (m_keyboardCaptured == keyboardCaptured && m_mouseCaptured == mouseCaptured)
    {
        return;
    }
    m_keyboardCaptured = keyboardCaptured;
    m_mouseCaptured = mouseCaptured;
    // The commit is prospective. Even delayed PT reports cannot rewrite records
    // that another UI/game cursor has already observed.
    ++m_routingEpoch;
    ResyncDevices();
}

void InputManager::CommitGameClock(bool paused, double timeScale)
{
    std::lock_guard lock(m_captureMutex);
    const auto now = Now();
    m_clockGame = GameTimeAt(now);
    m_clockReal = now;
    if (!std::isfinite(timeScale) || timeScale < 0)
    {
        EmitPolicy(Input::RecordKind::ClockDiscontinuity, Input::CancelReason::ClockDiscontinuity,
            Input::Recipient::Both, false);
        timeScale = 0;
    }
    m_timeScale = paused ? 0.0 : timeScale;
    if (paused != m_paused)
    {
        m_paused = paused;
        if (paused)
        {
            for (auto& device : m_devices)
            {
                StopRumble(device);
            }
        }
        EmitPolicy(paused ? Input::RecordKind::Pause : Input::RecordKind::Resume,
            paused ? Input::CancelReason::Paused : Input::CancelReason::None, Input::Recipient::Game, !paused);
        ResyncDevices();
    }
}

void InputManager::PublishClockGap()
{
    std::lock_guard lock(m_captureMutex);
    EmitPolicy(Input::RecordKind::ClockDiscontinuity, Input::CancelReason::ClockDiscontinuity,
        Input::Recipient::Game, false);
    ResyncDevices();
}

void InputManager::SetUserDevices(Input::UserID user, int controllerIndex, bool shareKeyboard)
{
    if (user == 0 || controllerIndex < -1 || controllerIndex >= static_cast<int>(kMaxController))
    {
        return;
    }
    std::lock_guard lock(m_captureMutex);
    auto found = std::ranges::find(m_assignments, user, &UserAssignment::user);
    if (found != m_assignments.end() && found->controllerIndex == controllerIndex && found->shareKeyboard == shareKeyboard)
    {
        return;
    }
    const int previousController = found == m_assignments.end() ? -1 : found->controllerIndex;
    const bool previousSharing = found != m_assignments.end() && found->shareKeyboard;
    for (auto& device : m_devices)
    {
        if (!device.native)
        {
            continue;
        }
        if (device.kind != Input::DeviceKind::Gamepad && device.user != user && previousSharing != shareKeyboard)
        {
            Input::RoutedInputRecord record;
            record.kind = shareKeyboard ? Input::RecordKind::DeviceAssigned : Input::RecordKind::DeviceDisconnected;
            record.reason = Input::CancelReason::DeviceReassigned;
            record.sourceTime = Now();
            record.user = user;
            record.device = device.id;
            record.deviceEpoch = device.epoch;
            record.assignmentEpoch = device.assignmentEpoch;
            record.enabled = shareKeyboard;
            Append(record);
        }
        if (device.kind == Input::DeviceKind::Gamepad && device.user == user &&
            device.controllerIndex == previousController && previousController != controllerIndex)
        {
            StopRumble(device);
            Input::RoutedInputRecord record;
            record.kind = Input::RecordKind::DeviceDisconnected;
            record.reason = Input::CancelReason::DeviceReassigned;
            record.sourceTime = Now();
            record.user = user;
            record.device = device.id;
            record.deviceEpoch = device.epoch;
            record.assignmentEpoch = ++device.assignmentEpoch;
            Append(record);
            device.user = 0;
        }
    }
    if (found == m_assignments.end())
    {
        m_assignments.push_back({user, controllerIndex, shareKeyboard});
    }
    else
    {
        *found = {user, controllerIndex, shareKeyboard};
    }
    for (auto& device : m_devices)
    {
        if (!device.native || device.kind != Input::DeviceKind::Gamepad || device.controllerIndex != controllerIndex ||
            previousController == controllerIndex)
        {
            continue;
        }
        StopRumble(device);
        Input::RoutedInputRecord record;
        record.kind = Input::RecordKind::DeviceAssigned;
        record.reason = Input::CancelReason::DeviceReassigned;
        record.sourceTime = Now();
        record.device = device.id;
        record.deviceEpoch = device.epoch;
        record.assignmentEpoch = ++device.assignmentEpoch;
        record.user = device.user;
        record.kind = Input::RecordKind::DeviceDisconnected;
        record.enabled = false;
        Append(record);
        device.user = user;
        record.user = user;
        record.kind = Input::RecordKind::DeviceAssigned;
        record.enabled = true;
        Append(record);
    }
    ResyncDevices();
}

void InputManager::RemoveUserDevices(Input::UserID user)
{
    std::lock_guard lock(m_captureMutex);
    std::erase_if(m_assignments, [user](const UserAssignment& value) { return value.user == user; });
    std::erase_if(m_claims, [user](const Claim& value) { return value.user == user; });
    std::erase(m_rebindUsers, user);
    for (auto& device : m_devices)
    {
        if (device.user == user)
        {
            StopRumble(device);
            device.user = device.kind == Input::DeviceKind::Gamepad ? 0 : 1;
            ++device.assignmentEpoch;
        }
    }
}

void InputManager::Update(float deltaTime)
{
    m_curKeyStates.Reset();
    m_curMouseState.Reset();
    m_mouseDelta = {};
    m_mouseWheelDelta = 0;
    std::fill(std::begin(m_gamepads), std::end(m_gamepads), GameInputGamepadState{});
    for (auto& state : m_curPadState)
    {
        state.Reset();
    }
    if (m_gameInput)
    {
        ComPtr<IGameInputReading> reading;
        if (SUCCEEDED(m_gameInput->GetCurrentReading(GameInputKindKeyboard, nullptr, &reading)) && reading)
        {
            std::array<GameInputKeyState, 256> keys{};
            const auto count = reading->GetKeyState(static_cast<std::uint32_t>(keys.size()), keys.data());
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (keys[index].virtualKey < kKeyboardCount)
                {
                    m_curKeyStates.Set(keys[index].virtualKey);
                }
            }
        }
        reading.Reset();
        if (SUCCEEDED(m_gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &reading)) && reading)
        {
            GameInputMouseState mouse{};
            if (reading->GetMouseState(&mouse))
            {
                for (std::uint32_t button = 0; button < kMouseCount; ++button)
                {
                    if ((static_cast<unsigned>(mouse.buttons) & (1u << button)) != 0)
                    {
                        m_curMouseState.Set(static_cast<uint8>(button));
                    }
                }
                if (m_rawMouseInitialized)
                {
                    m_mouseDelta = {static_cast<float>(mouse.positionX - m_rawMouseX),
                        static_cast<float>(mouse.positionY - m_rawMouseY)};
                    m_mouseWheelDelta = static_cast<int16>(std::clamp<std::int64_t>(mouse.wheelY - m_rawWheel, -32768, 32767));
                }
                m_rawMouseX = mouse.positionX;
                m_rawMouseY = mouse.positionY;
                m_rawWheel = mouse.wheelY;
                m_rawMouseInitialized = true;
            }
        }
        std::lock_guard lock(m_captureMutex);
        for (const auto& device : m_devices)
        {
            const auto index = device.controllerIndex;
            if (!device.native || index < 0 || index >= static_cast<int>(kMaxController))
            {
                continue;
            }
            reading.Reset();
            if (SUCCEEDED(m_gameInput->GetCurrentReading(GameInputKindGamepad, device.native.Get(), &reading)) && reading)
            {
                reading->GetGamepadState(&m_gamepads[index]);
                for (size_t button = 0; button < kPadButtons.size(); ++button)
                {
                    if ((m_gamepads[index].buttons & kPadButtons[button]) != 0)
                    {
                        m_curPadState[index].Set(button);
                    }
                }
            }
        }
    }
    // Update even on failed/disconnected reads: raw tool state cannot remain stuck.
    m_keyboardState.Update();
    m_mouseState.Update();
    m_padState.Update();
    UpdateControllerVibration(deltaTime);
}

bool InputManager::IsAnyKeyPressed()
{
    for (size_t index = 0; index < kKeyboardCount; ++index)
    {
        if (m_curKeyStates.Test(static_cast<uint8>(index)))
        {
            return true;
        }
    }
    return false;
}
void InputManager::SetMousePos(POINT position) { m_mousePos = {static_cast<float>(position.x), static_cast<float>(position.y)}; }
math::vector2 InputManager::GetMousePos()
{
    POINT position{};
    if (GetCursorPos(&position) && ScreenToClient(m_hwnd, &position))
    {
        SetMousePos(position);
    }
    return m_mousePos;
}
math::vector2 InputManager::GetMouseDelta() const { return m_mouseDelta; }
int16 InputManager::GetWheelDelta() const { return m_mouseWheelDelta; }
bool InputManager::IsWheelUp() { return m_mouseWheelDelta > 0; }
bool InputManager::IsWheelDown() { return m_mouseWheelDelta < 0; }
bool InputManager::IsMouseButtonDown(MouseKey button)
{
    const auto index = static_cast<size_t>(button);
    return index < kMouseCount && m_mouseState.GetKeyState(index) == KeyState::Down;
}
bool InputManager::IsMouseButtonPressed(MouseKey button)
{
    const auto index = static_cast<size_t>(button);
    return index < kMouseCount && m_mouseState.GetKeyState(index) == KeyState::Pressed;
}
bool InputManager::IsMouseButtonReleased(MouseKey button)
{
    const auto index = static_cast<size_t>(button);
    return index < kMouseCount && m_mouseState.GetKeyState(index) == KeyState::Released;
}
void InputManager::ResetMouseDelta() { m_mouseDelta = {}; m_mouseWheelDelta = 0; }
void InputManager::HideCursor()
{
    m_wantCursorHidden = true;
    if (m_gameInputOwned)
    {
        ApplyCursorHidden(true);
    }
}
void InputManager::ShowCursor() { m_wantCursorHidden = false; ApplyCursorHidden(false); }
void InputManager::ApplyCursorHidden(bool hidden)
{
    if (hidden == m_isCursorHidden)
    {
        return;
    }
    if (hidden)
    {
        while (::ShowCursor(FALSE) >= 0) {}
    }
    else
    {
        while (::ShowCursor(TRUE) < 0) {}
    }
    m_isCursorHidden = hidden;
}
bool InputManager::IsControllerConnected(DWORD index)
{
    if (index >= kMaxController)
    {
        return false;
    }
    std::lock_guard lock(m_captureMutex);
    return std::ranges::any_of(m_devices, [index](const Device& device) {
        return device.native && device.controllerIndex == static_cast<int>(index);
    });
}
bool InputManager::IsControllerButtonDown(DWORD index, ControllerButton button) const
{
    const auto code = static_cast<size_t>(button);
    return index < kMaxController && code < kGamepadKeyCount &&
        m_padState.GetKeyState(index, code) == KeyState::Down;
}
bool InputManager::IsControllerButtonPressed(DWORD index, ControllerButton button) const
{
    const auto code = static_cast<size_t>(button);
    return index < kMaxController && code < kGamepadKeyCount &&
        m_padState.GetKeyState(index, code) == KeyState::Pressed;
}
bool InputManager::IsControllerButtonReleased(DWORD index, ControllerButton button) const
{
    const auto code = static_cast<size_t>(button);
    return index < kMaxController && code < kGamepadKeyCount &&
        m_padState.GetKeyState(index, code) == KeyState::Released;
}
bool InputManager::IsControllerTriggerL(DWORD index) const { return index < kMaxController && m_gamepads[index].leftTrigger > triggerdeadZone; }
bool InputManager::IsControllerTriggerR(DWORD index) const { return index < kMaxController && m_gamepads[index].rightTrigger > triggerdeadZone; }
math::vector2 InputManager::GetControllerThumbL(DWORD index) const
{
    if (index >= kMaxController)
    {
        return {};
    }
    return {std::abs(m_gamepads[index].leftThumbstickX) < deadZone ? 0 : m_gamepads[index].leftThumbstickX,
        std::abs(m_gamepads[index].leftThumbstickY) < deadZone ? 0 : m_gamepads[index].leftThumbstickY};
}
math::vector2 InputManager::GetControllerThumbR(DWORD index) const
{
    if (index >= kMaxController)
    {
        return {};
    }
    return {std::abs(m_gamepads[index].rightThumbstickX) < deadZone ? 0 : m_gamepads[index].rightThumbstickX,
        std::abs(m_gamepads[index].rightThumbstickY) < deadZone ? 0 : m_gamepads[index].rightThumbstickY};
}
void InputManager::StopRumble(Device& device)
{
    if (device.native && device.kind == Input::DeviceKind::Gamepad)
    {
        GameInputRumbleParams neutral{};
        device.native->SetRumbleState(&neutral);
        if (device.controllerIndex >= 0 && device.controllerIndex < static_cast<int>(kMaxController))
        {
            m_vibrationSeconds[device.controllerIndex] = 0;
        }
    }
}
bool InputManager::SetUserVibration(Input::UserID user, int index, float seconds, math::vector4 strength)
{
    if (index < 0 || index >= static_cast<int>(kMaxController) || !std::isfinite(seconds) || seconds < 0)
    {
        return false;
    }
    std::lock_guard lock(m_captureMutex);
    for (auto& device : m_devices)
    {
        if (device.native && device.controllerIndex == index && device.user == user && m_focused && m_gameInputOwned && !m_paused)
        {
            const auto clamp = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
            GameInputRumbleParams value{};
            value.lowFrequency = clamp(strength.x);
            value.highFrequency = clamp(strength.y);
            value.leftTrigger = clamp(strength.z);
            value.rightTrigger = clamp(strength.w);
            device.native->SetRumbleState(&value);
            m_vibrationSeconds[index] = seconds;
            return true;
        }
    }
    return false;
}
void InputManager::SetControllerVibration(DWORD index, float left, float right, float low, float high, float seconds)
{
    SetUserVibration(static_cast<Input::UserID>(index) + 1, static_cast<int>(index), seconds, {low, high, left, right});
}
void InputManager::SetControllerVibration(DWORD index, float left, float right, float low, float high)
{
    SetControllerVibration(index, left, right, low, high, 0);
}
void InputManager::SetControllerVibrationTime(DWORD index, float seconds)
{
    if (index < kMaxController && std::isfinite(seconds) && seconds >= 0)
    {
        std::lock_guard lock(m_captureMutex);
        m_vibrationSeconds[index] = seconds;
    }
}
void InputManager::UpdateControllerVibration(float seconds)
{
    std::lock_guard lock(m_captureMutex);
    for (auto& device : m_devices)
    {
        const int index = device.controllerIndex;
        if (device.native && index >= 0 && index < static_cast<int>(kMaxController))
        {
            m_vibrationSeconds[index] -= (std::max)(0.0f, seconds);
            if (m_vibrationSeconds[index] <= 0)
            {
                StopRumble(device);
            }
        }
    }
}

Input::Timestamp InputManager::CurrentGameTime() const
{
    std::lock_guard lock(m_captureMutex);
    return GameTimeAt(Now());
}

bool InputManager::AssignDevice(Input::UserID user, Input::DeviceID id, std::uint64_t deviceEpoch,
    std::uint64_t assignmentEpoch, bool assigned)
{
    std::lock_guard lock(m_captureMutex);
    for (auto& device : m_devices)
    {
        if (!device.native || device.id != id || device.epoch != deviceEpoch ||
            device.assignmentEpoch != assignmentEpoch ||
            (!assigned && device.user != user))
        {
            continue;
        }
        StopRumble(device);
        Input::RoutedInputRecord record;
        record.kind = Input::RecordKind::DeviceAssigned;
        record.reason = Input::CancelReason::DeviceReassigned;
        record.sourceTime = Now();
        record.device = id;
        record.deviceEpoch = device.epoch;
        record.assignmentEpoch = ++device.assignmentEpoch;
        record.user = device.user;
        record.kind = Input::RecordKind::DeviceDisconnected;
        record.enabled = false;
        Append(record);
        device.user = assigned ? user : 0;
        if (assigned)
        {
            record.user = user;
            record.kind = Input::RecordKind::DeviceAssigned;
            record.enabled = true;
            Append(record);
        }
        ResyncDevices();
        return true;
    }
    return false;
}

void InputManager::SetRebindCapture(Input::UserID user, bool active)
{
    std::lock_guard lock(m_captureMutex);
    const auto found = std::ranges::find(m_rebindUsers, user);
    if ((found != m_rebindUsers.end()) == active)
    {
        return;
    }
    if (active)
    {
        m_rebindUsers.push_back(user);
    }
    else
    {
        m_rebindUsers.erase(found);
    }
    Input::RoutedInputRecord record;
    record.kind = Input::RecordKind::OwnershipChanged;
    record.reason = Input::CancelReason::Rebound;
    record.sourceTime = Now();
    record.user = user;
    record.enabled = !active;
    ++m_routingEpoch;
    Append(record);
    ResyncDevices();
}

void InputManager::SetGraphClaims(Input::InputSessionHandle owner, Input::UserID user,
    const Input::InputGraphProgram& program, bool enabled)
{
    std::lock_guard lock(m_captureMutex);
    const auto oldClaims = m_claims;
    std::erase_if(m_claims, [owner](const Claim& claim) { return claim.owner == owner; });
    const auto& graph = program.GetDefinition();
    for (const auto& binding : graph.bindings)
    {
        const auto signal = std::ranges::find(graph.signals, binding.signal, &Input::InputSignalDefinition::id);
        if (signal == graph.signals.end() || signal->domain != Input::Domain::UI)
        {
            continue;
        }
        const auto layer = std::ranges::find(graph.layers, signal->layer, &Input::InputLayer::id);
        if (layer == graph.layers.end() || layer->claim == Input::ClaimPolicy::PassThrough)
        {
            continue;
        }
        for (const auto& source : binding.sources)
        {
            const auto previous = std::ranges::find_if(oldClaims, [&](const Claim& claim) {
                return claim.owner == owner && claim.layer == layer->id;
            });
            const bool layerEnabled = previous == oldClaims.end() ? layer->enabled : previous->enabled;
            m_claims.push_back({owner, user, layer->id, signal->id, source.control, layer->claim,
                enabled, layerEnabled, false});
        }
    }
    ++m_routingEpoch;
    ResyncDevices();
}

void InputManager::SetLayerClaim(Input::InputSessionHandle owner, Input::UserID user, Input::LayerID layer, bool enabled)
{
    std::lock_guard lock(m_captureMutex);
    for (auto& claim : m_claims)
    {
        if (claim.owner == owner && claim.layer == layer)
        {
            claim.enabled = enabled;
        }
    }
    Input::RoutedInputRecord record;
    record.kind = Input::RecordKind::LayerChanged;
    record.reason = Input::CancelReason::LayerBlocked;
    record.sourceTime = Now();
    record.user = user;
    record.layer = layer;
    record.targetSessionId = owner.id;
    record.targetSessionGeneration = owner.generation;
    record.enabled = enabled;
    ++m_routingEpoch;
    Append(record);
    ResyncDevices();
}

void InputManager::SetPerformedClaim(Input::InputSessionHandle owner, Input::UserID user,
    Input::SignalID signal, bool active)
{
    std::lock_guard lock(m_captureMutex);
    bool changed = false;
    for (auto& claim : m_claims)
    {
        if (claim.owner == owner && claim.signal == signal && claim.policy == Input::ClaimPolicy::OnPerformed &&
            claim.active != active)
        {
            claim.active = active;
            changed = true;
        }
    }
    if (changed)
    {
        ++m_routingEpoch;
        ResyncDevices();
    }
}

void InputManager::RequestResync()
{
    std::lock_guard lock(m_captureMutex);
    ResyncDevices();
}

void InputManager::RemoveGraphClaims(Input::InputSessionHandle owner)
{
    std::lock_guard lock(m_captureMutex);
    std::erase_if(m_claims, [owner](const Claim& claim) { return claim.owner == owner; });
    ++m_routingEpoch;
    ResyncDevices();
}

std::vector<InputDeviceSnapshot> InputManager::GetDevices() const
{
    std::lock_guard lock(m_captureMutex);
    std::vector<InputDeviceSnapshot> result;
    result.reserve(m_devices.size());
    for (const auto& device : m_devices)
    {
        if (device.native)
        {
            result.push_back({device.id, device.epoch, device.assignmentEpoch, device.user,
                device.kind, device.controllerIndex, true});
        }
    }
    return result;
}

void InputManager::ResetConsumerHistory()
{
    std::lock_guard lock(m_captureMutex);
    m_records.clear();
    m_historyGap = false;
    EmitPolicy(m_focused ? Input::RecordKind::FocusGained : Input::RecordKind::FocusLost,
        m_focused ? Input::CancelReason::None : Input::CancelReason::FocusLost, Input::Recipient::Both, m_focused);
    EmitPolicy(Input::RecordKind::OwnershipChanged, Input::CancelReason::UIOwnership,
        Input::Recipient::Both, m_gameInputOwned);
    EmitPolicy(m_paused ? Input::RecordKind::Pause : Input::RecordKind::Resume,
        m_paused ? Input::CancelReason::Paused : Input::CancelReason::None, Input::Recipient::Game, !m_paused);
    ResyncDevices();
}

void InputManager::SetSessionClaimsEnabled(Input::InputSessionHandle owner, bool enabled)
{
    std::lock_guard lock(m_captureMutex);
    bool changed = false;
    for (auto& claim : m_claims)
    {
        if (claim.owner == owner && claim.ownerEnabled != enabled)
        {
            claim.ownerEnabled = enabled;
            claim.active = false;
            changed = true;
        }
    }
    if (changed)
    {
        ++m_routingEpoch;
        ResyncDevices();
    }
}

InputCaptureDiagnostics InputManager::GetCaptureDiagnostics() const
{
    std::lock_guard lock(m_captureMutex);
    return {m_records.size(), m_highWaterRecords, m_droppedRecords, m_routingEpoch, m_policyGeneration,
        m_records.empty() ? 0 : (std::max)(Input::Timestamp{}, Now() - m_records.front().realTime), m_historyGap};
}
