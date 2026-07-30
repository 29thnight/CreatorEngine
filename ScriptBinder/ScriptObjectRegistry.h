#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include <vector>
#include <mutex>

class GameObject;

// 관리 코드에 넘기는 객체 핸들.
//
// 관리 코드는 네이티브 포인터의 유효성을 알 수 없다. 슬롯을 재사용할 때 세대를 올려두면
// 파괴된 객체를 가리키던 핸들이 다음 조회에서 자동으로 걸러진다.
// C# 측 ObjectHandle과 배치가 같아야 한다(둘 다 uint32 두 개).
struct ScriptObjectHandle
{
	uint32_t index{ 0 };
	uint32_t generation{ 0 };   // 0 = 무효

	bool IsValid() const { return generation != 0; }
};

// 스크립트가 참조하는 GameObject만 담는 슬롯 테이블.
//
// 씬의 모든 오브젝트를 넣지 않는다 — 스크립트가 실제로 잡고 있는 것만 등록하므로
// 규모가 작고, 조회는 인덱스 한 번이다.
class ScriptObjectRegistry
{
public:
	static ScriptObjectRegistry& Get();

	// 이미 등록된 객체면 기존 핸들을 그대로 돌려준다.
	ScriptObjectHandle Register(GameObject* object);

	// 객체가 파괴될 때 부른다. 세대가 올라가 기존 핸들이 전부 무효가 된다.
	void Unregister(GameObject* object);

	// 세대가 어긋나면 nullptr. 이 검사 하나가 UAF를 구조적으로 막는다.
	GameObject* Resolve(ScriptObjectHandle handle) const;

	void Clear();
	size_t LiveCount() const;

private:
	struct Slot
	{
		GameObject* object{ nullptr };
		uint32_t generation{ 0 };
	};

	mutable std::mutex m_mutex;
	std::vector<Slot> m_slots;
	std::vector<uint32_t> m_freeSlots;

	// 같은 객체를 두 번 등록하지 않기 위한 역방향 조회.
	std::unordered_map<GameObject*, uint32_t> m_lookup;
};
#endif // !DYNAMICCPP_EXPORTS
