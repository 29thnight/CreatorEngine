#pragma once

#include "Camera.h"
#include <cstdint>
#include <memory>

struct EnhancedLiveDisplayTexture;

// Scene View 전용 카메라와 조작 상태의 단일 소유자.
// 게임 CameraComponent registry에 등록되지 않으며 RenderCore에는 매 프레임
// FrameCameraSnapshot 값만 보낸다.
class EditorCameraRig final
{
public:
    // Unity·Blender 의 새 씬과 같은 3/4 부감. 원점을 pivot 으로 두고
    // 오른쪽·위·뒤에서 내려다본다. Camera 의 값 기본값(원점 정면 수평)은
    // 런타임 카메라의 것이고, 씬 뷰는 그 자리에서 깊이가 전혀 읽히지 않았다.
    // yaw/pitch 는 HandleMovement 와 같은 규약이다 — yaw = atan2(forward.x,
    // forward.z), pitch 는 양수가 내려다보는 쪽이다.
    static constexpr math::vector3 kDefaultPivot{ 0.f, 0.f, 0.f };
    static constexpr float kDefaultYawDegrees{ -135.f };
    static constexpr float kDefaultPitchDegrees{ 30.f };
    // 기본 orbit 반경. 뷰 기즈모의 pivot 폴백도 같은 값을 써서, 시작 직후의
    // 회전 중심이 기본 포즈가 바라보는 지점(원점)과 어긋나지 않는다.
    static constexpr float kDefaultOrbitDistance{ 8.f };

    EditorCameraRig();
    ~EditorCameraRig();

    Camera& GetCamera() noexcept { return m_camera; }
    const Camera& GetCamera() const noexcept { return m_camera; }
    std::uint64_t GetSourceIdentity() const noexcept { return m_sourceIdentity; }

    FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const;

    // PT가 장치 판독과 조작 주기를 소유한다. GT는 씬 잠금 안에서 카메라 값만
    // 복사하며, PT의 이전 마우스 판독을 초기화하지 않는다.
    void BeginPresentationFrame(bool applicationFocused);
    void HandleMovement(bool enabled);
    void PublishPresentationDiagnostics(const EnhancedLiveDisplayTexture& displayed, bool imageDrawn);
    void ResetToDefaultPose() noexcept;
    void ApplySnapshot(const FrameCameraSnapshot& snapshot, std::uint64_t sourceIdentity,
        bool cameraCut = true) noexcept;
    void SetPose(const math::vector3& position, const math::quaternion& rotation,
        bool cameraCut = true) noexcept;
    void NotifyCameraCut() noexcept { m_camera.NotifyCameraCut(); }

    float* SpeedPtr() noexcept { return &m_speed; }

private:
    void SetOrientation(float yaw, float pitch) noexcept
    {
        m_deltaYaw = yaw;
        m_deltaPitch = pitch;
    }

    struct PresentationInput;
    std::unique_ptr<PresentationInput> m_presentationInput;
    std::uint64_t m_sourceIdentity{ 0 };
    std::uint64_t m_inputSequence{ 0 };
    mutable std::uint64_t m_cameraRevision{ 0 };
    mutable FrameCameraSnapshot m_lastCapturedCamera{};
    std::uint64_t m_followSourceIdentity{ 0 };
    std::uint64_t m_followSourceCutRevision{ 0 };
    Camera m_camera{};
    float m_speed{ 10.f };
    float m_speedMul{ 1.f };
    float m_deltaPitch{ 0.f };
    float m_deltaYaw{ 0.f };
};
