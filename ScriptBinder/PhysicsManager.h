#pragma once
#include "../physics/Physx.h"

class PhysicsManager
{
	//todo : 
	// - 물리엔진 초기화 및 업데이트
	// - 물리엔진 종료
	// - 물리엔진 씬 변경
	// - Object를 순회하며 물리컴포넌트를 찾아 생성 및 업데이트 및 삭제
	// - 물리엔진 콜리전 이벤트를 찾아서 콜백함수 호출
	// - 물리엔지 컴포넌트의 데이터를 기반으로 디버그 정보 드로우

public:
	PhysicsManager() = default;
	~PhysicsManager() = default;

	// 물리엔진 초기화 및 업데이트
	void Initialize();
	void Update(float fixedDeltaTime);

	// 물리엔진 종료
	void Shutdown();

	// 물리엔진 씬 변경
	void ChangeScene();

	//씬 로드 -> 남아 있을지 모를 물리객체를 삭제하고 현제 게임씬의 객체에 대한 물리엔진 객체를 생성및 등록
	void OnLoadScene();

	//씬 언로드
	void OnUnloadScene();



	//디버그 정보 드로우
	void DrawDebugInfo();

private:
	// 초기화 여부
	bool m_bIsInitialized{ false };

	// 물리엔진 시뮬레이트 여부
	bool m_bPlay{ false };

	//디버그 드로우 여부
	bool m_bDebugDraw{ false };

	//pre update  GameObject data -> pxScene data
	void SetPhysicData();

	//post update pxScene data -> GameObject data
	void GetPhysicData();

	unsigned int m_lastColliderID{ 0 };
	

};

