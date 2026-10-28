#include "../Containers/ContainerAPI.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "ScriptBindingRegistry.h"
#include "ScriptObjectRegistry.h"
#include "Entity.h"
#include "Transform.h"
#include "Scene.h"

namespace
{
    using namespace ce::script::binding;
	/// C#의 setter 직후 GetWorld* 계약을 packed targeted pull에 연결한다.
	/// Scene은 target의 parentExec chain만 정방향 계산하며 global dirty-root queue와
	/// 자식 전파 신호를 소비하지 않는다.
	void EnsureResolved(Entity* object)
	{
		if (nullptr == object)
		{
			return;
		}
		Scene* scene = object->GetScene();
		if (scene)
		{
			scene->EnsureResolved(scene->HandleOf(object->m_index));
			return;
		}

		// Scene 밖의 리플렉션 임시 객체는 packed identity가 없으므로 기존 로컬
		// fallback을 유지한다.
		Transform& transform = object->Transform_();
		transform.SetAndDecomposeMatrix(transform.UpdateWorldMatrix());
	}

	int __stdcall Api_Transform_Exists(ScriptObjectHandle handle)
	{
		const Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object && object->HasTransform()) ? 1 : 0;
	}

	Float3 __stdcall Api_Transform_GetLocalPosition(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return {};
		}

		const auto& p = object->Transform_().GetPositionValue();
		return { p.x, p.y, p.z };
	}

	void __stdcall Api_Transform_SetLocalPosition(ScriptObjectHandle handle, Float3 position)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		object->Transform_().SetPosition(
			{ position.x, position.y, position.z }, TransformWriteReason::Script);
	}

	Float3 __stdcall Api_Transform_GetWorldPosition(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return {};
		}

		EnsureResolved(object);

		const math::vector3 world = object->Transform_().GetWorldPosition();
		return { world.x, world.y, world.z };
	}

	// 아래 Transform API는 전부 같은 모양이다 — 핸들을 풀고, 없으면 무해한 기본값.
	// 엔진 setter들이 내부에서 SetDirty를 부르므로 여기서 따로 표시할 것은 없다.


	Float4 __stdcall Api_Transform_GetLocalRotation(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 0.f, 0.f, 0.f, 1.f };   // 단위 쿼터니언
		}

		const auto& r = object->Transform_().GetRotationValue();
		return { r.x, r.y, r.z, r.w };
	}

	void __stdcall Api_Transform_SetLocalRotation(ScriptObjectHandle handle, Float4 rotation)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		object->Transform_().SetRotation(
			{ rotation.x, rotation.y, rotation.z, rotation.w },
			TransformWriteReason::Script);
	}

	Float3 __stdcall Api_Transform_GetLocalScale(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 1.f, 1.f, 1.f };
		}

		const auto& s = object->Transform_().GetScaleValue();
		return { s.x, s.y, s.z };
	}

	void __stdcall Api_Transform_SetLocalScale(ScriptObjectHandle handle, Float3 scale)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		object->Transform_().SetScale(
			{ scale.x, scale.y, scale.z }, TransformWriteReason::Script);
	}

	void __stdcall Api_Transform_AddLocalPosition(ScriptObjectHandle handle, Float3 delta)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		object->Transform_().AddPosition(
			{ delta.x, delta.y, delta.z }, TransformWriteReason::Script);
	}

	void __stdcall Api_Transform_AddLocalRotation(ScriptObjectHandle handle, Float4 delta)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		object->Transform_().AddRotation(
			{ delta.x, delta.y, delta.z, delta.w }, TransformWriteReason::Script);
	}

	void __stdcall Api_Transform_SetWorldPosition(ScriptObjectHandle handle, Float3 position)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		EnsureResolved(object);

		object->Transform_().SetWorldPosition(
			{ position.x, position.y, position.z }, TransformWriteReason::Script);
	}

	Float4 __stdcall Api_Transform_GetWorldRotation(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 0.f, 0.f, 0.f, 1.f };
		}

		EnsureResolved(object);

		const math::quaternion q = object->Transform_().GetWorldQuaternion();
		return { q.x, q.y, q.z, q.w };
	}

	void __stdcall Api_Transform_SetWorldRotation(ScriptObjectHandle handle, Float4 rotation)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		EnsureResolved(object);

		object->Transform_().SetWorldRotation(
			{ rotation.x, rotation.y, rotation.z, rotation.w },
			TransformWriteReason::Script);
	}

	Float3 __stdcall Api_Transform_GetWorldScale(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 1.f, 1.f, 1.f };
		}

		EnsureResolved(object);

		const math::vector3 s = object->Transform_().GetWorldScale();
		return { s.x, s.y, s.z };
	}

	void __stdcall Api_Transform_SetWorldScale(ScriptObjectHandle handle, Float3 scale)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return;
		}

		EnsureResolved(object);

		object->Transform_().SetWorldScale(
			{ scale.x, scale.y, scale.z }, TransformWriteReason::Script);
	}

	Float3 __stdcall Api_Transform_GetForward(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 0.f, 0.f, 1.f };
		}

		EnsureResolved(object);

		const math::vector3 v = object->Transform_().GetForward();
		return { v.x, v.y, v.z };
	}

	Float3 __stdcall Api_Transform_GetRight(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 1.f, 0.f, 0.f };
		}

		EnsureResolved(object);

		const math::vector3 v = object->Transform_().GetRight();
		return { v.x, v.y, v.z };
	}

	Float3 __stdcall Api_Transform_GetUp(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object)
		{
			return { 0.f, 1.f, 0.f };
		}

		EnsureResolved(object);

		const math::vector3 v = object->Transform_().GetUp();
		return { v.x, v.y, v.z };
	}

}

namespace ce::script::binding
{
    void RegisterTransformApi(ScriptApiTable& table)
    {
        table.Transform_Exists = &Api_Transform_Exists;
        table.Transform_GetLocalPosition = &Api_Transform_GetLocalPosition;
        table.Transform_SetLocalPosition = &Api_Transform_SetLocalPosition;
        table.Transform_GetWorldPosition = &Api_Transform_GetWorldPosition;
        table.Transform_GetLocalRotation = &Api_Transform_GetLocalRotation;
        table.Transform_SetLocalRotation = &Api_Transform_SetLocalRotation;
        table.Transform_GetLocalScale = &Api_Transform_GetLocalScale;
        table.Transform_SetLocalScale = &Api_Transform_SetLocalScale;
        table.Transform_AddLocalPosition = &Api_Transform_AddLocalPosition;
        table.Transform_AddLocalRotation = &Api_Transform_AddLocalRotation;
        table.Transform_SetWorldPosition = &Api_Transform_SetWorldPosition;
        table.Transform_GetWorldRotation = &Api_Transform_GetWorldRotation;
        table.Transform_SetWorldRotation = &Api_Transform_SetWorldRotation;
        table.Transform_GetWorldScale = &Api_Transform_GetWorldScale;
        table.Transform_SetWorldScale = &Api_Transform_SetWorldScale;
        table.Transform_GetForward = &Api_Transform_GetForward;
        table.Transform_GetRight = &Api_Transform_GetRight;
        table.Transform_GetUp = &Api_Transform_GetUp;
    }
}
