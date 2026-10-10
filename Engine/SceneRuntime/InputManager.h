#pragma once
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#include <GameInput.h>
#include <wrl.h>
#include <array>
#include <deque>
#include <mutex>
#include <vector>
#include "ClassProperty.h"
#include "KeyState.h"
#include "KeyArray.h"
#include "KeyBitFlag.h"
#include "InputGraph.h"

using namespace GameInput::v3;
using namespace Microsoft::WRL;

struct InputCaptureDiagnostics
{
    std::size_t pendingRecords{};
    std::size_t highWaterRecords{};
    std::uint64_t droppedRecords{};
    std::uint64_t routingEpoch{};
    std::uint64_t policyGeneration{};
    Input::Timestamp oldestRecordAge{};
    bool recoveryPending{};
};

struct InputDeviceSnapshot
{
    Input::DeviceID id{};
    std::uint64_t epoch{};
    std::uint64_t assignmentEpoch{};
    Input::UserID user{};
    Input::DeviceKind kind{};
    int controllerIndex{-1};
    bool connected{};
};

// GameInput owns the asynchronous producer. This adapter never waits for rendering,
// creates no input thread, and never calls a Scene, script, or ImGui from a callback.
class InputManager : public Singleton<InputManager>
{
    friend class Singleton<InputManager>;
    InputManager() = default;
    ~InputManager();

public:
    bool Initialize(HWND window);
    void Update(float deltaTime); // Editor/tool snapshot only; gameplay consumes the timeline.
    void Shutdown();
    Input::Timestamp Now() const noexcept;
    std::vector<InputDeviceSnapshot> GetDevices() const;
    InputCaptureDiagnostics GetCaptureDiagnostics() const;
    void DrainRecords(std::vector<Input::RoutedInputRecord>& records, bool& historyGap);
    void SetWindowFocused(bool focused);
    void SetGameInputOwned(bool owned);
    void CommitRoutingPolicy(std::uint64_t generation, bool keyboardCaptured, bool mouseCaptured);
    void CommitGameClock(bool paused, double timeScale);
    void PublishClockGap();
    void RequestResync();
    void ResetConsumerHistory();
    void SetUserDevices(Input::UserID user, int controllerIndex, bool shareKeyboard);
    void RemoveUserDevices(Input::UserID user);
    bool AssignDevice(Input::UserID user, Input::DeviceID device, std::uint64_t deviceEpoch,
        std::uint64_t assignmentEpoch, bool assigned);
    void SetRebindCapture(Input::UserID user, bool active);
    Input::Timestamp CurrentGameTime() const;
    void SetGraphClaims(Input::InputSessionHandle owner, Input::UserID user,
        const Input::InputGraphProgram& program, bool enabled = true);
    void RemoveGraphClaims(Input::InputSessionHandle owner);
    void SetLayerClaim(Input::InputSessionHandle owner, Input::UserID user, Input::LayerID layer, bool enabled);
    void SetPerformedClaim(Input::InputSessionHandle owner, Input::UserID user, Input::SignalID signal, bool active);
    void SetSessionClaimsEnabled(Input::InputSessionHandle owner, bool enabled);
    bool SetUserVibration(Input::UserID user, int controllerIndex, float seconds, math::vector4 strength);

    // These raw queries are retained exclusively for editor shortcuts and tools.
    bool IsKeyDown(auto key) const
    {
        const auto index = static_cast<size_t>(key);
        return index < kKeyboardCount && m_keyboardState.GetKeyState(index) == KeyState::Down;
    }
    bool IsKeyPressed(auto key) const
    {
        const auto index = static_cast<size_t>(key);
        return index < kKeyboardCount && m_keyboardState.GetKeyState(index) == KeyState::Pressed;
    }
    bool IsKeyReleased(auto key) const
    {
        const auto index = static_cast<size_t>(key);
        return index < kKeyboardCount && m_keyboardState.GetKeyState(index) == KeyState::Released;
    }
    bool IsAnyKeyPressed();
    void SetMousePos(POINT position);
    math::vector2 GetMousePos();
    math::vector2 GetMouseDelta() const;
    bool IsWheelUp();
    bool IsWheelDown();
    bool IsMouseButtonDown(MouseKey button);
    bool IsMouseButtonPressed(MouseKey button);
    bool IsMouseButtonReleased(MouseKey button);
    void HideCursor();
    void ShowCursor();
    void ResetMouseDelta();
    int16 GetWheelDelta() const;
    bool IsGameInputOwned() const noexcept { return m_gameInputOwned; }
    bool IsCursorHideRequested() const noexcept { return m_wantCursorHidden; }
    bool IsCursorHidden() const noexcept { return m_isCursorHidden; }
    HWND WindowHandle() const noexcept { return m_hwnd; }
    bool IsControllerConnected(DWORD index);
    bool IsControllerButtonDown(DWORD index, ControllerButton button) const;
    bool IsControllerButtonPressed(DWORD index, ControllerButton button) const;
    bool IsControllerButtonReleased(DWORD index, ControllerButton button) const;
    bool IsControllerTriggerL(DWORD index) const;
    bool IsControllerTriggerR(DWORD index) const;
    math::vector2 GetControllerThumbL(DWORD index) const;
    math::vector2 GetControllerThumbR(DWORD index) const;
    void SetControllerVibration(DWORD index, float left, float right, float low, float high, float seconds);
    void SetControllerVibration(DWORD index, float left, float right, float low, float high);
    void SetControllerVibrationTime(DWORD index, float seconds);
    void UpdateControllerVibration(float seconds);

    KeyboardState m_keyboardState{};
    KeyBitFlag m_curKeyStates{};
    MouseBitFlag m_curMouseState{};
    GamePadBitFlag m_curPadState[kMaxController]{};
    math::vector2 m_gameViewPos{};
    math::vector2 m_gameViewSize{};
    float deadZone = 0.24f;
    float triggerdeadZone = 0.1f;

private:
    struct CapturedControl
    {
        Input::ControlID id{};
        Input::InputValue value{};
    };
    struct Device
    {
        ComPtr<IGameInputDevice> native;
        Input::DeviceID id{};
        std::uint64_t epoch{};
        std::uint64_t assignmentEpoch{1};
        Input::DeviceKind kind{};
        Input::UserID user{1};
        int controllerIndex{-1};
        bool initialized{};
        GameInputMouseState mouse{};
        std::vector<CapturedControl> controls;
    };
    struct Claim
    {
        Input::InputSessionHandle owner{};
        Input::UserID user{};
        Input::LayerID layer{};
        Input::SignalID signal{};
        Input::ControlID control{};
        Input::ClaimPolicy policy{};
        bool ownerEnabled{true};
        bool enabled{};
        bool active{};
    };
    struct UserAssignment
    {
        Input::UserID user{};
        int controllerIndex{-1};
        bool shareKeyboard{};
    };
    static void CALLBACK OnReading(GameInputCallbackToken token, void* context, IGameInputReading* reading);
    static void CALLBACK OnDevice(GameInputCallbackToken token, void* context, IGameInputDevice* device,
        std::uint64_t timestamp, GameInputDeviceStatus current, GameInputDeviceStatus previous);
    void CaptureReading(IGameInputReading* reading);
    Device* FindDevice(IGameInputDevice* device);
    Device* AddDevice(IGameInputDevice* device, GameInputKind kind, Input::Timestamp timestamp);
    void Append(Input::RoutedInputRecord record);
    void Emit(Device& device, Input::Timestamp timestamp, Input::ControlID control, Input::InputValue value,
        bool delta = false, bool resync = false);
    void EmitPolicy(Input::RecordKind kind, Input::CancelReason reason, Input::Recipient recipients, bool enabled);
    void ResyncDevices();
    void StopRumble(Device& device);
    void ApplyCursorHidden(bool hidden);
    Input::Timestamp GameTimeAt(Input::Timestamp realTime) const noexcept;

    static constexpr size_t kRecordCapacity = 32768;
    static constexpr Input::Timestamp kRetention = 5'000'000'000;
    mutable std::mutex m_captureMutex;
    ComPtr<IGameInput> m_gameInput;
    GameInputCallbackToken m_readingToken{};
    GameInputCallbackToken m_deviceToken{};
    std::array<Device, 32> m_devices{};
    std::vector<UserAssignment> m_assignments;
    std::vector<Input::UserID> m_rebindUsers;
    std::vector<Claim> m_claims;
    std::deque<Input::RoutedInputRecord> m_records;
    bool m_historyGap{};
    std::uint64_t m_sequence{};
    std::uint64_t m_routingEpoch{1};
    std::uint64_t m_policyGeneration{};
    std::uint64_t m_droppedRecords{};
    std::size_t m_highWaterRecords{};
    Input::Timestamp m_lastTimestamp{};
    Input::Timestamp m_clockReal{};
    Input::Timestamp m_clockGame{};
    double m_timeScale{1.0};
    bool m_paused{};
    bool m_focused{true};
    bool m_keyboardCaptured{};
    bool m_mouseCaptured{};
    bool m_gameInputOwned{true};
    bool m_wantCursorHidden{};
    bool m_isCursorHidden{};
    HWND m_hwnd{};
    MouseState m_mouseState{};
    PadState m_padState{};
    math::vector2 m_mousePos{};
    math::vector2 m_mouseDelta{};
    std::int64_t m_rawMouseX{};
    std::int64_t m_rawMouseY{};
    std::int64_t m_rawWheel{};
    bool m_rawMouseInitialized{};
    int16 m_mouseWheelDelta{};
    GameInputGamepadState m_gamepads[kMaxController]{};
    float m_vibrationSeconds[kMaxController]{};
};

inline static auto InputManagement = InputManager::GetInstance();
