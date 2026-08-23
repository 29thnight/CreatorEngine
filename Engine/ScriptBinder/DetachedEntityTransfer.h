#pragma once

#include "Entity.h"

#include <memory>

// Scene 사이에서 Entity 소유권을 옮기는 동안 원본 HierarchyStore 슬롯은 이미
// 해제된다. 따라서 재부착에 필요한 관계를 Entity의 직렬화 shadow 필드에 기대지
// 않고, 슬롯을 놓기 전에 이 전송 레코드에 스냅샷으로 보존한다.
struct DetachedEntityTransfer
{
	std::unique_ptr<Entity> entity{};
	Entity::Index oldIndex{ Entity::INVALID_INDEX };
	Entity::Index oldParentIndex{ Entity::INVALID_INDEX };
	Entity::Index oldRootIndex{ Entity::INVALID_INDEX };
};
