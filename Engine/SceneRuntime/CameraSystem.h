#pragma once
#include <vector>
#include <functional>

class CameraComponent;

// Scene이 직접 소유하는 카메라 비소유 등록·선택 시스템.
//
// CameraComponent가 Camera 값을 직접 소유하고, 이 시스템은 현재 씬 후보의
// 생명주기만 추적한다. 렌더 슬롯·표시 대상·카메라 객체 소유권은 가지지 않는다.
// Scene 밖의 프로세스 전역 registry가 아니므로 DDOL 이송과 씬 해제가 곧 등록
// 경계다. primary 선택은 등록 순서와 무관하게 결정론적이다.
//
// Update는 기존 SceneGraph 생명주기 trace와 순회 재진입 검증 지점만 보존한다.
// 카메라 자세는 소비 시점에 CameraComponent가 Entity Transform에서 값으로
// 해석하므로 공유 Camera를 프레임마다 덮어쓰지 않는다.
class CameraSystem final
{
public:
    CameraSystem() = default;
    ~CameraSystem() = default;

    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem::Register와 같은 이유
    // (조밀 벡터가 매 프레임 실제 로직을 돌므로, 중복이 들어가면 같은
    // CameraComponent가 한 프레임에 두 번 갱신된다).
    void Register(CameraComponent* camera);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(CameraComponent* camera);

    // 등록된 CameraComponent 전부를 한 번에 틱한다. 옛 Scene::RegistryTick이
    // 공통으로 해주던 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이
    // 대신 적용한다.
    //
    // midTraversalProbe: 순회 한복판 — 루프에 진입한 첫 반복에서 원소 유효성과
    // 무관하게 한 번 불리는 선택적 콜백이다(클래스 상단 "고유 사정 3", .cpp
    // 구현 참고). 비워 두면(기본값) 아무 일도 하지 않는다 — Scene 외 다른
    // 호출부(장차 생길 도구·테스트 등)가 이 시스템을 재진입 시험과 무관하게
    // 그냥 틱만 시키고 싶을 때를 위한 것이다.
    void Update(float tick, const std::function<void()>& midTraversalProbe = nullptr);

	// 이 Scene의 enabled 카메라 중 명시 primary를 우선하고, 없으면 instance ID가
	// 가장 작은 후보를 결정론적으로 고른다. 등록 순서나 전역 슬롯은 선택 의미가 아니다.
	CameraComponent* GetPrimaryCamera() const noexcept;
	const std::vector<CameraComponent*>& GetRegisteredCameras() const noexcept
	{
		return m_cameras;
	}

    size_t GetCount() const noexcept { return m_cameras.size(); }

private:
    std::vector<CameraComponent*> m_cameras;
};
