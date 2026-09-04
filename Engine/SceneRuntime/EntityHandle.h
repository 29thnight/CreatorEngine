#pragma once
#include <cstdint>

// 씬 그래프 노드의 런타임 정체성 (SceneGraphRedesignPlan §2.3, 트랙 E1 → 트랙 W).
//
// ScriptObjectHandle(ScriptObjectRegistry.h)과 "슬롯 인덱스 + 세대, 0은 무효"라는
// 개념은 여전히 같지만, 배치(uint32 두 개)는 더 이상 같지 않다 — 아래 sceneId가
// 늘며 셋이 됐다. 값(세대 시퀀스)까지 하나로 합치는 안은 트랙 E4에서 검토 후
// 기각했다 — EntityHandle은 씬 스코프(서로 다른 씬의 같은 index가 존재)인데
// ScriptObjectHandle은 전역이어야 하고, 무엇보다 DontDestroyOnLoad 이송 창
// (Scene::DetachEntityHierarchy가 슬롯을 놓고 AttachExistingEntity*가
// 다시 잡기 전 구간)에서 그 오브젝트는 어느 씬의 EntityHandle도 갖지 않는데
// 그 창에서 실제로 관리 코드(ScriptRegistry.SweepOrphans)가 도는 것을 코드
// 추적으로 확인했다(ScriptObjectRegistry.h 상단 주석에 전말). 그래서 두 핸들은
// 세대 시퀀스가 계속 별개다 — ScriptObjectRegistry가 자기 세대를
// Entity::Destroy() 한 지점에서만 올린다. C# 쪽 ObjectHandle과 배치를 맞춰야
// 하는 것은 ScriptObjectHandle뿐이라(그 헤더 상단 주석 참고), 여기 sceneId를
// 더해도 그 계약은 건드리지 않는다.
//
// ── sceneId가 필요한 이유 ──
//
// index+generation만으로는 "이 슬롯"을 씬 하나 안에서만 구분한다. Scene은
// 씬마다 독립된 슬롯맵(m_generations/m_freeSlots)을 갖고 0부터 다시 채우므로,
// 서로 다른 두 씬이 완전히 같은 {index, generation} 쌍을 동시에 가질 수 있다
// (예: 둘 다 방금 슬롯 3을 세대 1로 할당). sceneId 없이 핸들을 씬 경계 밖으로
// 들고 나가면(에디터 창 전환, DDOL, 프리팹 인스턴스 추적 등) 그 우연한 값
// 일치를 걸러낼 방법이 없었다 — 예전에는 그래서 EntityHandle을 쓰는 쪽이
// Scene*를 따로 같이 들고 다녀야 했다(PrefabUtility::InstanceRef가 그 우회책).
// sceneId를 핸들 안에 넣으면 Scene::Resolve가 그 자리에서 바로 씬 불일치를
// 걸러낼 수 있다 — "다른 씬의 같은 슬롯"이 구조적으로 막힌다.
//
// ── sceneId가 SceneManager 목록의 위치 인덱스가 아니라 일련번호인 이유 ──
//
// SceneManager::m_scenes에서의 위치(vector index)는 씬이 삭제되면 뒤 원소가
// 당겨지며 재사용된다 — 죽은 씬 A가 있던 자리에 새 씬 B가 들어오면 둘이 같은
// "위치"를 공유해 ABA가 난다(RenderEngine/Skeleton.h의 m_serial/NextSerial과
// 같은 사유 — 포인터/위치 재사용이 낡은 캐시를 속인다). sceneId는 대신 Scene
// 생성마다 단조 증가하는 값(Scene::NextSceneId, Scene.cpp)이라 프로세스가 살아
// 있는 동안 절대 재사용되지 않는다.
//
// 0은 "무효/미지정"으로 비워 둔다 — 일련번호는 1부터 발급한다(Scene.cpp의
// NextSceneId 참고). 기본 생성된 EntityHandle{}의 sceneId==0은 그래서 항상
// "어느 씬도 아님"을 뜻하고, generation==0(아래 IsValid)과 같은 뜻으로 겹치지
// 않게 갈라 둔다.
struct EntityHandle
{
	uint32_t sceneId{ 0 };      // 0 = 무효/미지정. Scene::NextSceneId가 매기는 일련번호(Scene.cpp).
	uint32_t index{ 0 };
	uint32_t generation{ 0 };   // 0 = 무효

	bool IsValid() const { return generation != 0; }

	bool operator==(const EntityHandle& other) const
	{
		return sceneId == other.sceneId && index == other.index && generation == other.generation;
	}
	bool operator!=(const EntityHandle& other) const
	{
		return !(*this == other);
	}
};
