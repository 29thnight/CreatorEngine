#pragma once
#include "Entity.h"
#include "AuthoringReadNode.h"

// SerializationPlan D3-a-2 — 저작 문서에서 Entity 생성 입력을 읽는 어댑터.
//
// 왜 Entity에서 뗐는가: 이 두 함수는 Entity **인스턴스의 상태**가 아니라 디스크
// 표현을 해석하는 일이다. `Entity`에 정적 멤버로 있는 동안은 `Entity.h`가 노드
// 타입을 알아야 했고, 그것이 §5 완료 기준 9("Entity/ComponentFactory/Runtime
// interface에 YAML/JSON/ryml Node 타입 0")를 막는 이유 중 하나였다.
//
// 반환 타입은 그대로 `Entity`의 것이다 — `GameObjectType`과
// `Entity::SerializedHierarchy`는 포맷과 무관한 DTO라 옮길 이유가 없다. 이 슬라이스가
// 옮긴 것은 **해석 책임**이지 데이터 모델이 아니다.
//
// D3-b가 backend를 ryml로 바꿀 때 고쳐야 할 곳이 이 파일 하나로 좁아진다.
//
// ★ `Entity::OnAfterSerialize`는 여기 없다. 리플렉션이 SFINAE로 **멤버 함수의 존재를
//   탐지해** 호출하므로(`ReflectionTypedYml.h:519`) 자유 함수로 옮기면 훅이 조용히
//   끊긴다 — 컴파일은 되고 계층 직렬화만 사라진다. 그 함수는 리플렉션 계약과 함께
//   D3-a-4에서 바뀐다.
namespace EntityAuthoring
{
	// 노드가 어떤 생성 아키타입을 요구하는지 판정한다.
	//
	// 구형 노드는 `m_gameObjectType`을 읽기 호환 입력으로 받고, 신형 노드는 공간
	// 컴포넌트 조합(Rect만=UI, Rect+Transform=Canvas, 그 외=Empty)에서 역으로 읽는다.
	[[nodiscard]] GameObjectType InferCreationType(const Authoring::ReadNode& node);

	// top-level 계층 키를 로드 배치 DTO로 읽는다. 이 값들은 Entity 리플렉션 필드가
	// 아니며 Scene Store를 경유해 확정된다(H3).
	[[nodiscard]] Entity::SerializedHierarchy ReadSerializedHierarchy(const Authoring::ReadNode& node);
}
