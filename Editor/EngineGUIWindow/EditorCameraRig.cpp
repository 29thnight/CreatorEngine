#include "EditorCameraRig.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "ProfileScope.h"
#include <wrl/client.h>
#include <GameInput.h>
#include "ImGui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <mathematics/transform.hpp>

#pragma comment(lib, "GameInput.lib")

namespace editor::camera_diagnostics
{
    constexpr std::size_t counter_count = 23;

    const std::array<ce::profile_counter_id, counter_count>& counter_ids()
    {
        static const auto ids = []
        {
            const char* names[]{
                "EditorCamera.Input sequence", "EditorCamera.Mouse available", "EditorCamera.Keyboard available",
                "EditorCamera.Mouse reading timestamp", "EditorCamera.Mouse delta X", "EditorCamera.Mouse delta Y",
                "EditorCamera.Navigation active", "EditorCamera.PT delta", "EditorCamera.Revision",
                "EditorCamera.Yaw", "EditorCamera.Pitch", "SceneView.Render input frame",
                "SceneView.Render input sequence", "SceneView.Render camera revision", "SceneView.Completed frame",
                "SceneView.Completed input sequence", "SceneView.Completed camera revision",
                "SceneView.Image available",
                "SceneView.Image frame", "SceneView.Image input sequence", "SceneView.Image camera revision",
                "SceneView.Image references", "SceneView.Repeated image references"};
            const char* units[]{
                "sequence", "0/1", "0/1", "us", "counts", "counts", "0/1", "ms", "revision", "rad", "rad",
                "frame", "sequence", "revision", "frame", "sequence", "revision", "0/1", "frame", "sequence",
                "revision", "count", "count"};
            static_assert(std::size(names) == counter_count && std::size(units) == counter_count);
            std::array<ce::profile_counter_id, counter_count> result{};
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                result[i] = ce::register_counter(names[i], units[i], ce::counter_category::render);
            }
            return result;
        }();
        return ids;
    }
}

struct EditorCameraRig::PresentationInput
{
    Microsoft::WRL::ComPtr<GameInput::v3::IGameInput> gameInput;
    Microsoft::WRL::ComPtr<GameInput::v3::IGameInputDevice> mouseDevice;
    GameInput::v3::GameInputMouseState previousMouse{};
    std::chrono::steady_clock::time_point previousSample{};
    std::array<bool, 256> keys{};
    std::uint64_t mouseTimestamp{ 0 };
    std::uint64_t consumedSequence{ 0 };
    std::uint64_t imageCount{ 0 };
    std::uint64_t repeatedImageCount{ 0 };
    std::uint64_t previousImageFrame{ 0 };
    float deltaSeconds{ 0.f };
    float mouseDeltaX{ 0.f };
    float mouseDeltaY{ 0.f };
    int wheelDirection{ 0 };
    bool initialized{ false };
    bool focused{ false };
    bool mouseAvailable{ false };
    bool keyboardAvailable{ false };
    bool rightButton{ false };
    bool navigating{ false };
};

EditorCameraRig::EditorCameraRig()
{
    // A replacement rig must not inherit the previous editor view's history.
    // This is a source identity, independent of motion and cut revisions.
    static std::atomic<std::uint64_t> nextSourceIdentity{ 1 };
    m_sourceIdentity = nextSourceIdentity.fetch_add(1, std::memory_order_relaxed);
    // 파일 녹화가 시작되기 전에 설명자를 등록한다. 표시는 같은 ID를 재사용한다.
    (void)editor::camera_diagnostics::counter_ids();
    ResetToDefaultPose();
}

EditorCameraRig::~EditorCameraRig() = default;

FrameCameraSnapshot EditorCameraRig::CaptureFrameSnapshot(float aspectRatio) const
{
    FrameCameraSnapshot snapshot = m_camera.CaptureFrameSnapshot(aspectRatio);
    // Camera 직접 편집(선택 대상으로 이동·투영 메뉴·방향 기즈모)도 진단에 반영한다.
    // This diagnostic revision advances on ordinary motion; temporal history
    // uses cameraCutRevision instead and must never reset from this counter.
    // 패딩이나 부동소수점 바이트가 아니라 실제 행렬 값을 비교한다.
    if (m_cameraRevision == 0 || !(snapshot.view == m_lastCapturedCamera.view) ||
        !(snapshot.projection == m_lastCapturedCamera.projection))
    {
        ++m_cameraRevision;
        m_lastCapturedCamera = snapshot;
    }
    snapshot.editorInputSequence = m_inputSequence;
    snapshot.editorCameraRevision = m_cameraRevision;
    return snapshot;
}

void EditorCameraRig::BeginPresentationFrame(bool applicationFocused)
{
    using namespace GameInput::v3;
    if (!m_presentationInput)
    {
        m_presentationInput = std::make_unique<PresentationInput>();
    }
    auto& input = *m_presentationInput;
    if (!input.initialized)
    {
        input.initialized = true;
        const HRESULT result = GameInputCreate(input.gameInput.GetAddressOf());
        if (FAILED(result))
        {
            std::printf("[EditorCameraInput] GameInput creation failed: 0x%08lX\n",
                static_cast<unsigned long>(result));
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const bool continuousFocus = applicationFocused && input.focused;
    const float elapsed = input.previousSample == std::chrono::steady_clock::time_point{}
        ? 0.f : std::chrono::duration<float>(now - input.previousSample).count();
    // 포커스 상실·최소화 중의 시간을 이동에 누적하지 않는다. 긴 PT 정지 뒤의
    // 이동량은 제한하되, 포커스를 유지했다면 누적 마우스 입력은 버리지 않는다.
    input.deltaSeconds = continuousFocus ? std::clamp(elapsed, 0.f, 0.1f) : 0.f;
    input.previousSample = now;
    input.focused = applicationFocused;
    if (!continuousFocus || input.consumedSequence != m_inputSequence)
    {
        input.navigating = false;
    }
    ++m_inputSequence;
    input.mouseDeltaX = input.mouseDeltaY = 0.f;
    input.wheelDirection = 0;
    input.rightButton = false;
    input.keys.fill(false);
    const bool hadMouse = input.mouseAvailable;
    input.mouseAvailable = false;
    input.keyboardAvailable = false;
    if (!input.gameInput)
    {
        return;
    }

    Microsoft::WRL::ComPtr<IGameInputReading> reading;
    if (SUCCEEDED(input.gameInput->GetCurrentReading(GameInputKindMouse, nullptr, reading.GetAddressOf())) && reading)
    {
        GameInputMouseState mouse{};
        if (reading->GetMouseState(&mouse))
        {
            Microsoft::WRL::ComPtr<IGameInputDevice> device;
            reading->GetDevice(device.GetAddressOf());
            const std::uint64_t timestamp = reading->GetTimestamp();
            if (continuousFocus && hadMouse && device.Get() == input.mouseDevice.Get() &&
                timestamp >= input.mouseTimestamp)
            {
                // ImGui 논리 픽셀이 아니라 장치의 누적 이동량이다. DPI와 무관하게
                // 기존 0.5 * 0.01 rad/count 감도를 그대로 유지한다.
                input.mouseDeltaX = static_cast<float>(mouse.positionX - input.previousMouse.positionX);
                input.mouseDeltaY = static_cast<float>(mouse.positionY - input.previousMouse.positionY);
                input.wheelDirection = (mouse.wheelY > input.previousMouse.wheelY) -
                    (mouse.wheelY < input.previousMouse.wheelY);
            }
            input.previousMouse = mouse;
            input.mouseDevice = std::move(device);
            input.mouseTimestamp = timestamp;
            input.mouseAvailable = true;
            input.rightButton = (mouse.buttons & GameInputMouseRightButton) != 0;
        }
    }

    reading.Reset();
    if (SUCCEEDED(input.gameInput->GetCurrentReading(
            GameInputKindKeyboard, nullptr, reading.GetAddressOf())) && reading)
    {
        std::array<GameInputKeyState, 256> keys{};
        const std::uint32_t count = reading->GetKeyState(static_cast<std::uint32_t>(keys.size()), keys.data());
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (keys[i].virtualKey < input.keys.size())
            {
                input.keys[keys[i].virtualKey] = true;
            }
        }
        input.keyboardAvailable = true;
    }
}

void EditorCameraRig::SetPose(const math::vector3& position, const math::quaternion& rotation,
    bool cameraCut) noexcept
{
    if (cameraCut)
    {
        NotifyCameraCut();
    }
    m_camera.m_eyePosition = position;
    m_camera.rotate = math::normalize(rotation);
    m_camera.m_forward = math::normalize(math::rotate(Camera::kForward, m_camera.rotate));
    m_camera.m_up = math::normalize(math::rotate(Camera::kUp, m_camera.rotate));
    m_camera.m_right = math::normalize(math::rotate(Camera::kRight, m_camera.rotate));
    SetOrientation(std::atan2(m_camera.m_forward.x, m_camera.m_forward.z),
        -std::asin(std::clamp(m_camera.m_forward.y, -1.f, 1.f)));
}

void EditorCameraRig::ResetToDefaultPose() noexcept
{
    // HandleMovement 와 같은 순서로 쌓는다(yaw 먼저, 그 다음 yaw 가 돌린 right
    // 축의 pitch). 순서가 다르면 SetPose 가 되돌려 놓는 m_deltaYaw/m_deltaPitch
    // 와 실제 자세가 어긋나, 우클릭 첫 프레임에 카메라가 튄다.
    const math::quaternion yawRotation = math::quaternion_from_axis_angle(
        Camera::kUp, math::radians(kDefaultYawDegrees));
    const math::quaternion pitchRotation = math::quaternion_from_axis_angle(
        math::rotate(Camera::kRight, yawRotation), math::radians(kDefaultPitchDegrees));
    const math::quaternion rotation = math::normalize(yawRotation * pitchRotation);
    const math::vector3 forward =
        math::normalize(math::rotate(Camera::kForward, rotation));
    SetPose(kDefaultPivot - forward * kDefaultOrbitDistance, rotation);
}

void EditorCameraRig::HandleMovement(bool enabled)
{
    if (!m_presentationInput || m_presentationInput->consumedSequence == m_inputSequence)
    {
        return;
    }
    auto& input = *m_presentationInput;
    input.consumedSequence = m_inputSequence;
    const bool wasNavigating = input.navigating;
    input.navigating = enabled && input.focused &&
        (input.mouseAvailable ? input.rightButton : ImGui::IsMouseDown(ImGuiMouseButton_Right));
    if (!input.navigating)
    {
        return;
    }

    const auto keyDown = [&](unsigned key, ImGuiKey fallback)
    {
        return input.keyboardAvailable ? input.keys[key] : ImGui::IsKeyDown(fallback);
    };
    float x = 0.f, y = 0.f, z = 0.f;
    constexpr float minSpeed = 10.f;
    constexpr float maxSpeed = 100.f;
    if (keyDown('W', ImGuiKey_W))
    {
        z += 1.f;
    }
    if (keyDown('S', ImGuiKey_S))
    {
        z -= 1.f;
    }
    if (keyDown('A', ImGuiKey_A))
    {
        x -= 1.f;
    }
    if (keyDown('D', ImGuiKey_D))
    {
        x += 1.f;
    }
    if (keyDown('Q', ImGuiKey_Q))
    {
        y -= 1.f;
    }
    if (keyDown('E', ImGuiKey_E))
    {
        y += 1.f;
    }
    if (input.wheelDirection != 0)
    {
        m_speedMul = std::clamp(m_speedMul + 0.01f * input.wheelDirection, 0.01f, 2.f);
        m_speed = std::clamp(m_speed * m_speedMul, minSpeed, maxSpeed);
    }

    // 캔버스 진입·드래그 시작 때는 조작권을 얻기 전 이동을 재생하지 않는다.
    // 그 다음 PT 판독부터는 두 판독 사이에 쌓인 이동량을 빠짐없이 소비한다.
    if (wasNavigating && input.mouseAvailable)
    {
        m_deltaPitch += input.mouseDeltaY * 0.005f;
        m_deltaYaw += input.mouseDeltaX * 0.005f;

        const math::quaternion qYaw = math::quaternion_from_axis_angle(Camera::kUp, m_deltaYaw);
        const math::vector3 right = math::rotate(Camera::kRight, qYaw);
        const math::quaternion qPitch = math::quaternion_from_axis_angle(right, m_deltaPitch);
        const math::quaternion cameraRotation = math::normalize(qYaw * qPitch);
        m_camera.m_forward = math::normalize(math::rotate(Camera::kForward, cameraRotation));
        m_camera.m_up = math::normalize(math::rotate(Camera::kUp, cameraRotation));
        m_camera.m_right = math::normalize(math::cross(m_camera.m_up, m_camera.m_forward));
        m_camera.rotate = cameraRotation;
    }

    const float deltaSeconds = wasNavigating ? input.deltaSeconds : 0.f;
    const math::vector3 movement =
        (m_camera.m_forward * z + m_camera.m_up * y + m_camera.m_right * x) * (m_speed * deltaSeconds);
    m_camera.m_eyePosition += movement;
}

void EditorCameraRig::PublishPresentationDiagnostics(const EnhancedLiveDisplayTexture& displayed, bool imageDrawn)
{
    if (!m_presentationInput)
    {
        return;
    }
    auto& input = *m_presentationInput;
    if (imageDrawn)
    {
        ++input.imageCount;
        if (input.previousImageFrame == displayed.frame.completedFrameId)
        {
            ++input.repeatedImageCount;
        }
        input.previousImageFrame = displayed.frame.completedFrameId;
    }
    if (!ce::profiler().counter_enabled(ce::counter_category::render))
    {
        return;
    }

    // 한 묶음의 모든 값은 같은 PT 관측이다. cpu.tick으로 시각이 있는 instant와
    // 연결하고, 같은 GT 프레임 안의 여러 표본이 서로 덮어쓰지 않게 한다.
    // Image는 ImGui 그리기 참조 수다. 실제 화면 출력이나 Present 성공 수가 아니다.
    const FrameCameraSnapshot camera = CaptureFrameSnapshot();
    const auto& ids = editor::camera_diagnostics::counter_ids();
    const double values[]{
        static_cast<double>(m_inputSequence), input.mouseAvailable ? 1.0 : 0.0, input.keyboardAvailable ? 1.0 : 0.0,
        static_cast<double>(input.mouseTimestamp), input.mouseDeltaX, input.mouseDeltaY,
        input.navigating ? 1.0 : 0.0, input.deltaSeconds * 1000.0, static_cast<double>(camera.editorCameraRevision),
        std::atan2(camera.forward.x, camera.forward.z), -std::asin(std::clamp(camera.forward.y, -1.f, 1.f)),
        static_cast<double>(displayed.frame.sourceFrameId), static_cast<double>(displayed.frame.sourceInputSequence),
        static_cast<double>(displayed.frame.sourceCameraRevision),
        static_cast<double>(displayed.latestCompletedFrameId),
        static_cast<double>(displayed.latestCompletedInputSequence),
        static_cast<double>(displayed.latestCompletedCameraRevision), imageDrawn ? 1.0 : 0.0,
        imageDrawn ? static_cast<double>(displayed.frame.completedFrameId) : 0.0,
        imageDrawn ? static_cast<double>(displayed.frame.completedCamera.editorInputSequence) : 0.0,
        imageDrawn ? static_cast<double>(displayed.frame.completedCamera.editorCameraRevision) : 0.0,
        static_cast<double>(input.imageCount), static_cast<double>(input.repeatedImageCount)};
    static_assert(std::size(values) == editor::camera_diagnostics::counter_count);
    const ce::cpu_span_context context{0, m_inputSequence, 0};
    std::array<ce::profile_counter_sample, editor::camera_diagnostics::counter_count> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = {ids[i], values[i], context};
    }
    ce::profiler().mark_instant(ce::marker<"EditorCameraPresentationSample">(), context);
    ce::profiler().publish_counters(ce::profiler().current_frame(), ce::counter_category::render, samples);
}

void EditorCameraRig::ApplySnapshot(const FrameCameraSnapshot& snapshot, std::uint64_t sourceIdentity,
    bool cameraCut) noexcept
{
    // Follow is continuous after the initial match, except when its game
    // camera is replaced or explicitly cuts. The editor's own revision is
    // independent of the followed source revision and diagnostic motion ID.
    if (cameraCut || sourceIdentity != m_followSourceIdentity ||
        snapshot.cameraCutRevision != m_followSourceCutRevision)
    {
        NotifyCameraCut();
    }
    m_followSourceIdentity = sourceIdentity;
    m_followSourceCutRevision = snapshot.cameraCutRevision;
    m_camera.m_eyePosition = snapshot.eyePosition;
    m_camera.m_forward = math::normalize(snapshot.forward);
    m_camera.m_up = math::normalize(snapshot.up);
    m_camera.m_right = math::normalize(snapshot.right);
    m_camera.m_fov = snapshot.fov;
    m_camera.m_nearPlane = snapshot.nearPlane;
    m_camera.m_farPlane = snapshot.farPlane;
    m_camera.m_isOrthographic = snapshot.isOrthographic;

    const math::matrix4x4 rotationMatrix{
        m_camera.m_right.x, m_camera.m_right.y, m_camera.m_right.z, 0.f,
        m_camera.m_up.x, m_camera.m_up.y, m_camera.m_up.z, 0.f,
        m_camera.m_forward.x, m_camera.m_forward.y, m_camera.m_forward.z, 0.f,
        0.f, 0.f, 0.f, 1.f };
    m_camera.rotate = math::normalize(
        math::quaternion_from_rotation_matrix(rotationMatrix));

    SetOrientation(std::atan2(m_camera.m_forward.x, m_camera.m_forward.z),
        -std::asin(std::clamp(m_camera.m_forward.y, -1.f, 1.f)));
}
