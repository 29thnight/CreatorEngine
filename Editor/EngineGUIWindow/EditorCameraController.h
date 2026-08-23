#pragma once

class Camera;

// ── 에디터 카메라 조작기 (PHASE 4-3 슬라이스 4) ──
//
// 이 코드는 Camera::HandleMovement로 RenderEngine에 있었다. 74줄짜리
// 함수 하나가 InputManager를 11번, ImGui를 7번 불렀고 — Camera.cpp가
// 게임플레이 헤더를 여는 유일한 이유였다.
//
// 그런데 부르는 쪽은 하나뿐이었다: 에디터 씬 뷰. 그리고 그 대상은
// 언제나 EnhancedSceneRenderer의 editorCamera다. 씬에 저장되는 게임
// 카메라는 이 경로를 한 번도 타지 않는다.
//
// 즉 렌더 계층의 Camera에 얹혀 있었을 뿐, 처음부터 에디터 도구였다.
//
// 조작 상태(속도·배율·누적 각도)도 함께 왔다. 넷 다 이 함수와 씬 뷰
// 슬라이더, 콘솔의 카메라 정합 말고는 쓰는 곳이 없었다.
class EditorCameraController
{
public:
	// 에디터 카메라가 하나뿐이라 조작 상태도 하나다.
	static EditorCameraController& Get();

	void HandleMovement(Camera& camera, float deltaTime);

	// 콘솔의 camera.editor follow가 시점을 게임 카메라에 맞춘 뒤,
	// 다음 우클릭에 옛 자세로 튀지 않도록 누적 각도를 되돌린다.
	void SetOrientation(float yaw, float pitch) noexcept
	{
		m_deltaYaw = yaw;
		m_deltaPitch = pitch;
	}

	// 씬 뷰 팝업의 "Camera Speed" 슬라이더가 직접 잡는다.
	float* SpeedPtr() noexcept { return &m_speed; }

private:
	EditorCameraController() = default;

	float m_speed{ 10.f };
	float m_speedMul{ 1.f };
	float m_deltaPitch{ 0.f };
	float m_deltaYaw{ 0.f };
};

