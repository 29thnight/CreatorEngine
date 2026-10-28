#include "../Containers/ContainerAPI.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "ScriptApiTable.h"
#include "ScriptObjectRegistry.h"
#include "ScriptComponent.h"
#include "Entity.h"
#include "Scene.h"
#include "SceneManager.h"

namespace
{
	ScriptObjectHandle __stdcall Api_Entity_FindByName(const char* name)
	{
		if (nullptr == name)
		{
			return {};
		}

		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene)
		{
			return {};
		}

		auto object = scene->GetEntity(name);
		if (!object)
		{
			return {};
		}

		return ScriptObjectRegistry::Get().Register(object);
	}

	int __stdcall Api_Entity_IsAlive(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object && !object->IsDestroyMark()) ? 1 : 0;
	}

	int __stdcall Api_Entity_GetName(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0)
		{
			return 0;
		}

		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return 0;
		}

		const std::string name = object->m_name;
		const int length = static_cast<int>(std::min<size_t>(name.size(), static_cast<size_t>(capacity)));
		std::memcpy(buffer, name.data(), length);
		return length;
	}

	void __stdcall Api_Entity_SetEnabled(ScriptObjectHandle handle, int enabled)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr != object)
		{
			object->SetEnabled(0 != enabled);
		}
	}

	/// 스크립트 컴포넌트 하나만 켜고 끈다(PHASE 9 트랙 L · 활성 축).
	///
	/// ── 왜 역맵을 두지 않는가 ──
	///
	/// instanceId → ScriptComponent* 맵을 들면 파괴·핫리로드·재생 왕복마다 그 맵을
	/// 정확히 걷어야 하고, 한 번이라도 놓치면 dangling을 역참조한다. 관리 측은 이미
	/// 자기 Entity 핸들을 들고 있으므로, 그 오브젝트의 ScriptComponent들만 훑어
	/// id로 고르면 새 상태를 만들지 않는다 — QueueManagedCollision이 쓰는 것과 같은
	/// 관용구다. 오브젝트 하나의 스크립트 수는 한 자리라 선형 탐색으로 충분하다.
	///
	/// 돌려주는 값은 "실제로 그 컴포넌트를 찾아 적용했는가"다. 0이면 관리 측이
	/// 국소 폴백으로 내려가고 경고를 남긴다 — 조용히 무시되면 스크립트가 자기를
	/// 껐는데 계속 도는, 이 슬라이스가 고치려는 바로 그 증상이 다시 생긴다.
	int __stdcall Api_Script_SetEnabled(ScriptObjectHandle owner, int instanceId, int enabled)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(owner);
		if (nullptr == object)
		{
			return 0;
		}

		for (ScriptComponent* script : object->GetComponents<ScriptComponent>())
		{
			if (nullptr == script || script->GetInstanceId() != instanceId)
			{
				continue;
			}

			// Component::SetEnabled가 전이일 때만 OnEnable/OnDisable을 부르고,
			// ScriptComponent의 그 override가 관리 측으로 되돌려 준다. 여기서
			// 훅을 직접 부르지 않는 것이 요점이다 — 드라이버가 하나여야 한다.
			script->SetEnabled(0 != enabled);
			return 1;
		}
		return 0;
	}

}

namespace ce::script::binding
{
    void RegisterEntityApi(ScriptApiTable& table)
    {
        table.Entity_FindByName = &Api_Entity_FindByName;
        table.Entity_IsAlive = &Api_Entity_IsAlive;
        table.Entity_GetName = &Api_Entity_GetName;
        table.Entity_SetEnabled = &Api_Entity_SetEnabled;
        table.Script_SetEnabled = &Api_Script_SetEnabled;
    }
}
