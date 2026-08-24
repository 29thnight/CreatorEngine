#pragma once

#include "Camera.h"

// Scene View 전용 카메라와 조작 상태의 단일 소유자.
// 게임 CameraComponent registry에 등록되지 않으며 RenderCore에는 매 프레임
// FrameCameraSnapshot 값만 보낸다.
class EditorCameraRig final
{
public:
	Camera& GetCamera() noexcept { return m_camera; }
	const Camera& GetCamera() const noexcept { return m_camera; }

	FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const
	{
		return m_camera.CaptureFrameSnapshot(aspectRatio);
	}

	void HandleMovement(float deltaTime);
	void ApplySnapshot(const FrameCameraSnapshot& snapshot) noexcept;

	float* SpeedPtr() noexcept { return &m_speed; }

private:
	void SetOrientation(float yaw, float pitch) noexcept
	{
		m_deltaYaw = yaw;
		m_deltaPitch = pitch;
	}

	Camera m_camera{};
	float m_speed{ 10.f };
	float m_speedMul{ 1.f };
	float m_deltaPitch{ 0.f };
	float m_deltaYaw{ 0.f };
};
