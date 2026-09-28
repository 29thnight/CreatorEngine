#pragma once
#include <mathematics/vector2.hpp>
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "BTEnum.h"


struct [[reflgen::reflect]] BTBuildNode
{
   public:
	BTBuildNode() = default;
	~BTBuildNode() = default;

	HashedGuid				ID; // 유니크 (uuid 등), ex) "node_001"
	BehaviorNodeType		Type; // 예: "Selector", "Wait", "MoveTo"
	std::string				Name; // UI 표시용
	HashedGuid				ParentID; // 연결 정보 (하위에 있는 노드가 부모를 참조)
	bool 					IsRoot; // 루트 노드 여부
	bool 					HasScript; // 스크립트 노드 여부("Action", "Condition")
	std::string 			ScriptName; // 스크립트 이름
	ParallelPolicy			Policy; // 병렬 실행 정책

	std::vector<HashedGuid> Children; // 자식 노드들

	std::vector<float>	ChildWeights; // 가중치 (WeightedSelector용)

	math::vector2	Position; // 노드 위치 (에디터용)

	[[reflgen::ignore]] std::string State;
};
