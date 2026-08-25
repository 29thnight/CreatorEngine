#include "ClrHost.h"
#include "PathFinder.h"
#include "Entity.h"
#include "Transform.h"
#include "SceneManager.h"
#include "Scene.h"
#include "PrefabUtility.h"
#include "SoundComponent.h"
#include "Animator.h"
#include "ConditionParameter.h"
#include "CharacterControllerComponent.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "Canvas.h"
#include "UIButton.h"
#include "CameraComponent.h"
#include "CameraSystem.h"
#include "MeshRenderer.h"
#include "../RenderEngine/Material.h"
#include "../RenderEngine/MathematicsInterop.h"
#include "InputManager.h"
#include "PhysicsManager.h"
#include "RigidBodyComponent.h"
#include "SphereColliderComponent.h"
#include "BoxColliderComponent.h"
#include "CapsuleColliderComponent.h"

#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

#include <windows.h>
#include <cstring>

// nethost.lib는 get_hostfxr_path 하나만 제공하는 얇은 import 라이브러리다.
// 실제 팩 경로는 EngineOutput.props의 DotNetHostPackDir 한 곳에서 관리한다.
#pragma comment(lib, "nethost.lib")

namespace
{
	// ── 관리 코드에 넘기는 엔진 API 표 ──
	//
	// C# ScriptApiTable과 필드 순서·타입이 정확히 같아야 한다.
	// 어긋나면 엉뚱한 함수를 호출하게 되므로 버전과 크기를 함께 넘겨 초기화 때 검사한다.
	// 필드를 추가하면 kApiVersion을 반드시 올린다.
	constexpr int kApiVersion = 19;

	struct Float3 { float x, y, z; };

	// 물리 질의 결과 하나. 관리 측 RaycastHit과 배치가 같아야 한다.
	//
	// 개수가 정해지지 않은 결과는 호출자가 준 버퍼에 채운다 — 경계 너머로 컨테이너를
	// 넘기지 않고, 관리 측이 스택 버퍼를 쓰면 할당도 없다.
	struct ScriptHitResult
	{
		ScriptObjectHandle object;
		unsigned int layer;
		Float3 point;
		Float3 normal;
		float distance;
	};

	// 쿼터니언 전용. Mathf::Vector4와 배치가 같아 그대로 오간다.
	struct Float4 { float x, y, z, w; };

	// UI 좌표·화면 크기용. Mathf::Vector2와 배치가 같다.
	struct Float2 { float x, y; };

	struct ScriptApiTable
	{
		int version;
		int structSize;

		void (__stdcall* Log)(int level, const char* message);

		ScriptObjectHandle (__stdcall* Entity_FindByName)(const char* name);
		int  (__stdcall* Entity_IsAlive)(ScriptObjectHandle handle);
		int  (__stdcall* Entity_GetName)(ScriptObjectHandle handle, char* buffer, int capacity);
		void (__stdcall* Entity_SetEnabled)(ScriptObjectHandle handle, int enabled);

		// 계층 접근. 실측에서 m_childrenIndices 76 · Entity::FindIndex 100 ·
		// m_parentIndex 32로, 남은 어떤 컴포넌트 래퍼보다 큰 표면이다.
		int  (__stdcall* Entity_GetChildCount)(ScriptObjectHandle handle);
		ScriptObjectHandle (__stdcall* Entity_GetChild)(ScriptObjectHandle handle, int index);
		ScriptObjectHandle (__stdcall* Entity_GetParent)(ScriptObjectHandle handle);
		ScriptObjectHandle (__stdcall* Entity_FindByIndex)(int index);
		int  (__stdcall* Entity_GetIndex)(ScriptObjectHandle handle);

		Float3 (__stdcall* Transform_GetLocalPosition)(ScriptObjectHandle handle);
		void   (__stdcall* Transform_SetLocalPosition)(ScriptObjectHandle handle, Float3 position);
		Float3 (__stdcall* Transform_GetWorldPosition)(ScriptObjectHandle handle);

		// 회전·스케일·방향축. 실측에서 Transform 획득이 154회로 전체 1위인데
		// 위치만 열려 있어 이동·조준 코드가 통째로 막혀 있었다.
		Float4 (__stdcall* Transform_GetLocalRotation)(ScriptObjectHandle handle);
		void   (__stdcall* Transform_SetLocalRotation)(ScriptObjectHandle handle, Float4 rotation);
		Float3 (__stdcall* Transform_GetLocalScale)(ScriptObjectHandle handle);
		void   (__stdcall* Transform_SetLocalScale)(ScriptObjectHandle handle, Float3 scale);

		void   (__stdcall* Transform_AddLocalPosition)(ScriptObjectHandle handle, Float3 delta);
		void   (__stdcall* Transform_AddLocalRotation)(ScriptObjectHandle handle, Float4 delta);

		void   (__stdcall* Transform_SetWorldPosition)(ScriptObjectHandle handle, Float3 position);
		Float4 (__stdcall* Transform_GetWorldRotation)(ScriptObjectHandle handle);
		void   (__stdcall* Transform_SetWorldRotation)(ScriptObjectHandle handle, Float4 rotation);
		Float3 (__stdcall* Transform_GetWorldScale)(ScriptObjectHandle handle);
		void   (__stdcall* Transform_SetWorldScale)(ScriptObjectHandle handle, Float3 scale);

		Float3 (__stdcall* Transform_GetForward)(ScriptObjectHandle handle);
		Float3 (__stdcall* Transform_GetRight)(ScriptObjectHandle handle);
		Float3 (__stdcall* Transform_GetUp)(ScriptObjectHandle handle);

		int  (__stdcall* Prefab_Exists)(const char* name);
		ScriptObjectHandle (__stdcall* Prefab_Instantiate)(const char* prefabName, const char* instanceName);
		void (__stdcall* Entity_Destroy)(ScriptObjectHandle handle);

		unsigned long long (__stdcall* Engine_GetFrameCount)();

		int  (__stdcall* Sound_Exists)(ScriptObjectHandle handle);
		void (__stdcall* Sound_Play)(ScriptObjectHandle handle);
		void (__stdcall* Sound_Stop)(ScriptObjectHandle handle);
		void (__stdcall* Sound_Pause)(ScriptObjectHandle handle, int pause);
		int  (__stdcall* Sound_IsPlaying)(ScriptObjectHandle handle);
		void (__stdcall* Sound_PlayOneShot)(ScriptObjectHandle handle);
		int  (__stdcall* Sound_GetClipKey)(ScriptObjectHandle handle, char* buffer, int capacity);
		void (__stdcall* Sound_SetClipKey)(ScriptObjectHandle handle, const char* value);
		float (__stdcall* Sound_GetVolume)(ScriptObjectHandle handle);
		void (__stdcall* Sound_SetVolume)(ScriptObjectHandle handle, float value);
		float (__stdcall* Sound_GetPitch)(ScriptObjectHandle handle);
		void (__stdcall* Sound_SetPitch)(ScriptObjectHandle handle, float value);

		// Animator (실측 122회 — 그 중 SetParameter만 94회다)
		int   (__stdcall* Animator_Exists)(ScriptObjectHandle handle);
		int   (__stdcall* Animator_HasParameter)(ScriptObjectHandle handle, const char* name);
		void  (__stdcall* Animator_SetBool)(ScriptObjectHandle handle, const char* name, int value);
		void  (__stdcall* Animator_SetFloat)(ScriptObjectHandle handle, const char* name, float value);
		void  (__stdcall* Animator_SetInt)(ScriptObjectHandle handle, const char* name, int value);
		void  (__stdcall* Animator_SetTrigger)(ScriptObjectHandle handle, const char* name);
		void  (__stdcall* Animator_ResetTrigger)(ScriptObjectHandle handle, const char* name);
		int   (__stdcall* Animator_GetBool)(ScriptObjectHandle handle, const char* name);
		float (__stdcall* Animator_GetFloat)(ScriptObjectHandle handle, const char* name);
		int   (__stdcall* Animator_GetInt)(ScriptObjectHandle handle, const char* name);
		void  (__stdcall* Animator_SetUseLayer)(ScriptObjectHandle handle, int layerIndex, int useLayer);
		void  (__stdcall* Animator_StopAnimation)(ScriptObjectHandle handle, float duration);

		// CharacterControllerComponent (획득 47회 · 호출 약 60회 — 이동의 뼈대)
		int   (__stdcall* Cct_Exists)(ScriptObjectHandle handle);
		void  (__stdcall* Cct_Move)(ScriptObjectHandle handle, float inputX, float inputY);
		void  (__stdcall* Cct_TriggerForcedMove)(ScriptObjectHandle handle, Float3 velocity, float duration);
		void  (__stdcall* Cct_StopForcedMove)(ScriptObjectHandle handle);
		int   (__stdcall* Cct_IsInForcedMove)(ScriptObjectHandle handle);
		void  (__stdcall* Cct_SetAutomaticRotation)(ScriptObjectHandle handle, int useAuto);
		void  (__stdcall* Cct_SetLookDirection)(ScriptObjectHandle handle, Float3 direction);
		void  (__stdcall* Cct_ClearLookDirection)(ScriptObjectHandle handle);
		void  (__stdcall* Cct_ForcedSetPosition)(ScriptObjectHandle handle, Float3 position);
		float (__stdcall* Cct_GetBaseSpeed)(ScriptObjectHandle handle);
		void  (__stdcall* Cct_SetBaseSpeed)(ScriptObjectHandle handle, float speed);
		int   (__stdcall* Cct_IsOnMove)(ScriptObjectHandle handle);
		void  (__stdcall* Cct_SetOnMove)(ScriptObjectHandle handle, int isMove);
		int   (__stdcall* Cct_IsFalling)(ScriptObjectHandle handle);
		float (__stdcall* Cct_GetRadius)(ScriptObjectHandle handle);
		float (__stdcall* Cct_GetHeight)(ScriptObjectHandle handle);
		unsigned int (__stdcall* Cct_GetId)(ScriptObjectHandle handle);

		// RectTransformComponent (획득 28회 · SetAnchoredPosition만 39회)
		int    (__stdcall* Rect_Exists)(ScriptObjectHandle handle);
		Float2 (__stdcall* Rect_GetAnchoredPosition)(ScriptObjectHandle handle);
		void   (__stdcall* Rect_SetAnchoredPosition)(ScriptObjectHandle handle, Float2 position);
		Float2 (__stdcall* Rect_GetSizeDelta)(ScriptObjectHandle handle);
		void   (__stdcall* Rect_SetSizeDelta)(ScriptObjectHandle handle, Float2 size);
		Float2 (__stdcall* Rect_GetPivot)(ScriptObjectHandle handle);
		void   (__stdcall* Rect_SetPivot)(ScriptObjectHandle handle, Float2 pivot);

		// ImageComponent (획득 48회 · SetTexture 39 · color 46)
		int    (__stdcall* Image_Exists)(ScriptObjectHandle handle);
		void   (__stdcall* Image_SetTexture)(ScriptObjectHandle handle, int index);
		int    (__stdcall* Image_GetTextureCount)(ScriptObjectHandle handle);
		Float4 (__stdcall* Image_GetColor)(ScriptObjectHandle handle);
		void   (__stdcall* Image_SetColor)(ScriptObjectHandle handle, Float4 color);
		float  (__stdcall* Image_GetClipPercent)(ScriptObjectHandle handle);
		void   (__stdcall* Image_SetClipPercent)(ScriptObjectHandle handle, float percent);
		void   (__stdcall* Image_SetNativeSize)(ScriptObjectHandle handle);

		// 카메라. 월드→스크린 변환을 손으로 하는 파일이 11개나 되어 한 번에 접는다.
		int    (__stdcall* Camera_Exists)();
		Float2 (__stdcall* Camera_GetScreenSize)();
		Float3 (__stdcall* Camera_WorldToScreenPoint)(Float3 world);

		// MeshRenderer + Material. 스크립트가 만지는 것은 사실상 셋뿐이다 —
		// 재질 사본 만들기(6회) · 셰이더 상수 넣기(24회) · 베이스 색 알파.
		int   (__stdcall* Mesh_Exists)(ScriptObjectHandle handle);
		void  (__stdcall* Mesh_InstantiateMaterial)(ScriptObjectHandle handle, const char* newName);
		int   (__stdcall* Mesh_GetMaterialName)(ScriptObjectHandle handle, char* buffer, int capacity);
		int   (__stdcall* Mesh_SetMaterialFloat)(ScriptObjectHandle handle, const char* buffer, const char* name, float value);
		int   (__stdcall* Mesh_SetMaterialInt)(ScriptObjectHandle handle, const char* buffer, const char* name, int value);
		Float4 (__stdcall* Mesh_GetBaseColor)(ScriptObjectHandle handle);
		void  (__stdcall* Mesh_SetBaseColor)(ScriptObjectHandle handle, Float4 color);

		// 입력. 실측 43회인데 스크립트에서 아무것도 받을 수 없던 표면이다.
		//
		// 개별 술어(Down/Pressed/Released)를 따로 열지 않고 상태값을 통째로 넘긴다.
		// 경계 왕복이 한 번으로 줄고, 무엇보다 엔진의 Pressed가 첫 프레임을 제외한다는
		// 함정을 C# 쪽에서 정확히 조합해 감출 수 있다.
		int   (__stdcall* Input_GetKeyState)(int key);
		int   (__stdcall* Input_GetMouseButtonState)(int button);
		int   (__stdcall* Input_GetControllerButtonState)(int index, int button);
		int   (__stdcall* Input_IsAnyKeyPressed)();

		Float2 (__stdcall* Input_GetMousePosition)();
		Float2 (__stdcall* Input_GetMouseDelta)();
		int   (__stdcall* Input_GetWheelDelta)();
		void  (__stdcall* Input_SetCursorVisible)(int visible);

		int   (__stdcall* Input_IsControllerConnected)(int index);
		int   (__stdcall* Input_IsControllerTriggerL)(int index);
		int   (__stdcall* Input_IsControllerTriggerR)(int index);
		Float2 (__stdcall* Input_GetControllerThumbL)(int index);
		Float2 (__stdcall* Input_GetControllerThumbR)(int index);

		// 물리 질의 (Raycast 19 · SphereOverlap 16 — 타격 판정과 탐지의 뼈대)
		int (__stdcall* Physics_Raycast)(Float3 origin, Float3 direction, float distance,
			unsigned int layerMask, ScriptHitResult* hit);
		int (__stdcall* Physics_RaycastAll)(Float3 origin, Float3 direction, float distance,
			unsigned int layerMask, ScriptHitResult* buffer, int capacity);
		int (__stdcall* Physics_OverlapSphere)(Float3 position, float radius,
			unsigned int layerMask, ScriptHitResult* buffer, int capacity);

		// RigidBodyComponent (실측 34회)
		int   (__stdcall* Rigid_Exists)(ScriptObjectHandle handle);
		Float3 (__stdcall* Rigid_GetLinearVelocity)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetLinearVelocity)(ScriptObjectHandle handle, Float3 velocity);
		void  (__stdcall* Rigid_AddLinearVelocity)(ScriptObjectHandle handle, Float3 velocity);
		Float3 (__stdcall* Rigid_GetAngularVelocity)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetAngularVelocity)(ScriptObjectHandle handle, Float3 velocity);
		void  (__stdcall* Rigid_AddForce)(ScriptObjectHandle handle, Float3 force, int forceMode);
		void  (__stdcall* Rigid_SetBodyType)(ScriptObjectHandle handle, int bodyType);
		int   (__stdcall* Rigid_IsKinematic)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetKinematic)(ScriptObjectHandle handle, int kinematic);
		int   (__stdcall* Rigid_IsTrigger)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetIsTrigger)(ScriptObjectHandle handle, int isTrigger);
		int   (__stdcall* Rigid_IsColliderEnabled)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetColliderEnabled)(ScriptObjectHandle handle, int enabled);
		int   (__stdcall* Rigid_IsUsingGravity)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_UseGravity)(ScriptObjectHandle handle, int useGravity);
		float (__stdcall* Rigid_GetMass)(ScriptObjectHandle handle);
		void  (__stdcall* Rigid_SetMass)(ScriptObjectHandle handle, float mass);
		void  (__stdcall* Rigid_SetLinearDamping)(ScriptObjectHandle handle, float damping);
		void  (__stdcall* Rigid_SetAngularDamping)(ScriptObjectHandle handle, float damping);
		void  (__stdcall* Rigid_SetScale)(ScriptObjectHandle handle, Float3 scale);
		void  (__stdcall* Rigid_SetLockLinear)(ScriptObjectHandle handle, int x, int y, int z);
		void  (__stdcall* Rigid_SetLockAngular)(ScriptObjectHandle handle, int x, int y, int z);

		// 콜라이더 3종. 표면이 거의 같아 종류를 인자로 받아 디스패치한다
		// (kind: 0=Sphere 1=Box 2=Capsule).
		int   (__stdcall* Collider_Exists)(ScriptObjectHandle handle, int kind);
		float (__stdcall* Collider_GetRadius)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetRadius)(ScriptObjectHandle handle, int kind, float radius);
		float (__stdcall* Collider_GetHeight)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetHeight)(ScriptObjectHandle handle, int kind, float height);
		Float3 (__stdcall* Collider_GetExtents)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetExtents)(ScriptObjectHandle handle, int kind, Float3 extents);
		Float3 (__stdcall* Collider_GetPositionOffset)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetPositionOffset)(ScriptObjectHandle handle, int kind, Float3 offset);
		float (__stdcall* Collider_GetRestitution)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetRestitution)(ScriptObjectHandle handle, int kind, float value);
		float (__stdcall* Collider_GetStaticFriction)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetStaticFriction)(ScriptObjectHandle handle, int kind, float value);
		float (__stdcall* Collider_GetDynamicFriction)(ScriptObjectHandle handle, int kind);
		void  (__stdcall* Collider_SetDynamicFriction)(ScriptObjectHandle handle, int kind, float value);

		// TextComponent (SetMessage 16 · SetAlpha 6)
		int   (__stdcall* Text_Exists)(ScriptObjectHandle handle);
		int   (__stdcall* Text_GetMessage)(ScriptObjectHandle handle, char* buffer, int capacity);
		void  (__stdcall* Text_SetMessage)(ScriptObjectHandle handle, const char* message);
		Float4 (__stdcall* Text_GetColor)(ScriptObjectHandle handle);
		void  (__stdcall* Text_SetColor)(ScriptObjectHandle handle, Float4 color);
		float (__stdcall* Text_GetAlpha)(ScriptObjectHandle handle);
		void  (__stdcall* Text_SetAlpha)(ScriptObjectHandle handle, float alpha);
		float (__stdcall* Text_GetFontSize)(ScriptObjectHandle handle);
		void  (__stdcall* Text_SetFontSize)(ScriptObjectHandle handle, float size);
		Float2 (__stdcall* Text_GetRelativePosition)(ScriptObjectHandle handle);
		void  (__stdcall* Text_SetRelativePosition)(ScriptObjectHandle handle, Float2 position);

		// UIComponent 공통. Image·Text가 함께 쓰므로 기반 타입으로 찾는다.
		int   (__stdcall* Ui_GetOrder)(ScriptObjectHandle handle);
		void  (__stdcall* Ui_SetOrder)(ScriptObjectHandle handle, int order);

		// Canvas
		int   (__stdcall* Canvas_Exists)(ScriptObjectHandle handle);
		int   (__stdcall* Canvas_GetOrder)(ScriptObjectHandle handle);
		void  (__stdcall* Canvas_SetOrder)(ScriptObjectHandle handle, int order);
		int   (__stdcall* Canvas_GetName)(ScriptObjectHandle handle, char* buffer, int capacity);
		void  (__stdcall* Canvas_SetName)(ScriptObjectHandle handle, const char* name);

		// UI 내비게이션·버튼·Image 잔여 (IsNavigationThis 5 · SetNavLock 5 · curindex 5 · rotate 4)
		int   (__stdcall* Ui_IsSelected)(ScriptObjectHandle handle);
		int   (__stdcall* Ui_IsNavLocked)(ScriptObjectHandle handle);
		void  (__stdcall* Ui_SetNavLock)(ScriptObjectHandle handle, int locked);
		ScriptObjectHandle (__stdcall* UiNav_GetSelected)();
		void  (__stdcall* UiNav_SetSelected)(ScriptObjectHandle handle);

		int   (__stdcall* Button_Exists)(ScriptObjectHandle handle);
		int   (__stdcall* Button_ConsumeClicked)(ScriptObjectHandle handle);

		int   (__stdcall* Image_GetTextureIndex)(ScriptObjectHandle handle);
		float (__stdcall* Image_GetRotation)(ScriptObjectHandle handle);
		void  (__stdcall* Image_SetRotation)(ScriptObjectHandle handle, float rotation);

		// 레이아웃 검증용 — 계산된 최종 사각형(x, y, width, height)을 그대로 읽는다.
		Float4 (__stdcall* Rect_GetWorldRect)(ScriptObjectHandle handle);
		// Camera.WorldToScreenPoint와 같은 좌상단 원점 화면 픽셀 좌표.
		Float2 (__stdcall* Rect_GetScreenPosition)(ScriptObjectHandle handle);
		void   (__stdcall* Rect_SetScreenPosition)(ScriptObjectHandle handle, Float2 position);
	};

	ScriptApiTable g_apiTable{};

	// ── API 구현 ──
	// 전부 게임 스레드에서만 불린다(관리 코드 호출을 게임 스레드로 한정했으므로).

	void __stdcall Api_Log(int level, const char* message)
	{
		if (nullptr == message) return;

		switch (level)
		{
		case 0:  Debug->LogDebug(message);   break;
		case 2:  Debug->LogWarning(message); break;
		case 3:  Debug->LogError(message);   break;
		default: Debug->Log(message);        break;
		}
	}

	ScriptObjectHandle __stdcall Api_Entity_FindByName(const char* name)
	{
		if (nullptr == name) return {};

		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene) return {};

		auto object = scene->GetEntity(name);
		if (!object) return {};

		return ScriptObjectRegistry::Get().Register(object);
	}

	int __stdcall Api_Entity_IsAlive(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object && !object->IsDestroyMark()) ? 1 : 0;
	}

	int __stdcall Api_Entity_GetName(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return 0;

		const std::string name = object->m_name.ToString();
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

	/// 월드 값을 읽기 전에 캐시를 맞춰 둔다.
	///
	/// m_worldPosition·m_worldQuaternion·m_worldMatrix는 프레임당 한 번
	/// Scene::AllUpdateWorldMatrix가 채우는 캐시다. 그런데 스크립트에서는
	/// "방금 내 Transform을 고치고 바로 월드 값을 읽는" 흐름이 흔하고,
	/// 그대로 두면 한 프레임 전 값을 보게 된다(실측으로 확인한 문제).
	/// 조상까지 훑어 하나라도 더러우면 갱신한다 — 깨끗하면 bool 몇 번 읽고 끝난다.
	void EnsureWorldMatrix(Entity* object)
	{
		if (nullptr == object) return;

		// 계층이 꼬여 순환이 생기면 여기서 멈춘다(엔진 재귀도 같은 위험이 있다).
		constexpr int kMaxDepth = 64;

		bool dirty = false;
		Entity* node = object;
		for (int depth = 0; nullptr != node && depth < kMaxDepth; ++depth)
		{
			if (node->Transform_().IsDirty())
			{
				dirty = true;
				break;
			}

			const Entity::Index parentIndex = node->GetParentIndex();
			if (Entity::INVALID_INDEX == parentIndex) break;

			node = node->OwnerSceneFindIndex(parentIndex);
		}

		if (!dirty) return;

		// UpdateWorldMatrix가 조상까지 거슬러 올라가며 갱신한다. 다만 부모가 없는
		// 오브젝트는 월드 행렬만 세우고 분해는 하지 않으므로 여기서 마저 시킨다
		// (행렬이 그대로면 SetAndDecomposeMatrix가 곧바로 빠져나간다).
		Transform& transform = object->Transform_();
		transform.SetAndDecomposeMatrix(transform.UpdateWorldMatrix());
	}

	// ── 계층 ──
	//
	// 엔진은 오브젝트를 인덱스로 잇는다(m_childrenIndices·m_parentIndex).
	// 관리 측에는 인덱스 대신 세대 핸들로 바꿔 넘긴다 — 인덱스는 슬롯이 재사용되면
	// 다른 오브젝트를 가리키게 되지만 핸들은 세대 비교로 걸러진다.

	int __stdcall Api_Entity_GetChildCount(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? static_cast<int>(object->GetChildrenIndices().size()) : 0;
	}

	ScriptObjectHandle __stdcall Api_Entity_GetChild(ScriptObjectHandle handle, int index)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return {};

		const auto& children = object->GetChildrenIndices();
		if (index < 0 || static_cast<size_t>(index) >= children.size()) return {};

		Entity* child = object->OwnerSceneFindIndex(children[index]);
		return (nullptr != child) ? ScriptObjectRegistry::Get().Register(child) : ScriptObjectHandle{};
	}

	ScriptObjectHandle __stdcall Api_Entity_GetParent(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return {};

		const Entity::Index parentIndex = object->GetParentIndex();
		if (Entity::INVALID_INDEX == parentIndex) return {};

		Entity* parent = object->OwnerSceneFindIndex(parentIndex);
		return (nullptr != parent) ? ScriptObjectRegistry::Get().Register(parent) : ScriptObjectHandle{};
	}

	ScriptObjectHandle __stdcall Api_Entity_FindByIndex(int index)
	{
		if (index < 0) return {};

		Entity* object = Entity::FindIndex(index);
		return (nullptr != object) ? ScriptObjectRegistry::Get().Register(object) : ScriptObjectHandle{};
	}

	int __stdcall Api_Entity_GetIndex(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? static_cast<int>(object->m_index) : -1;
	}

	Float3 __stdcall Api_Transform_GetLocalPosition(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return {};

		const auto& p = object->Transform_().position;
		return { p.x, p.y, p.z };
	}

	void __stdcall Api_Transform_SetLocalPosition(ScriptObjectHandle handle, Float3 position)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		object->Transform_().SetPosition({ position.x, position.y, position.z });
	}

	Float3 __stdcall Api_Transform_GetWorldPosition(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return {};

		EnsureWorldMatrix(object);

		const math::vector3 world = object->Transform_().GetWorldPosition();
		return { world.x, world.y, world.z };
	}

	// 아래 Transform API는 전부 같은 모양이다 — 핸들을 풀고, 없으면 무해한 기본값.
	// 엔진 setter들이 내부에서 SetDirty를 부르므로 여기서 따로 표시할 것은 없다.


	Float4 __stdcall Api_Transform_GetLocalRotation(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 0.f, 0.f, 0.f, 1.f };   // 단위 쿼터니언

		const auto& r = object->Transform_().rotation;
		return { r.x, r.y, r.z, r.w };
	}

	void __stdcall Api_Transform_SetLocalRotation(ScriptObjectHandle handle, Float4 rotation)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		object->Transform_().SetRotation({ rotation.x, rotation.y, rotation.z, rotation.w });
	}

	Float3 __stdcall Api_Transform_GetLocalScale(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 1.f, 1.f, 1.f };

		const auto& s = object->Transform_().scale;
		return { s.x, s.y, s.z };
	}

	void __stdcall Api_Transform_SetLocalScale(ScriptObjectHandle handle, Float3 scale)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		object->Transform_().SetScale({ scale.x, scale.y, scale.z });
	}

	void __stdcall Api_Transform_AddLocalPosition(ScriptObjectHandle handle, Float3 delta)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		object->Transform_().AddPosition({ delta.x, delta.y, delta.z });
	}

	void __stdcall Api_Transform_AddLocalRotation(ScriptObjectHandle handle, Float4 delta)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		object->Transform_().AddRotation({ delta.x, delta.y, delta.z, delta.w });
	}

	void __stdcall Api_Transform_SetWorldPosition(ScriptObjectHandle handle, Float3 position)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		EnsureWorldMatrix(object);

		object->Transform_().SetWorldPosition({ position.x, position.y, position.z });
	}

	Float4 __stdcall Api_Transform_GetWorldRotation(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 0.f, 0.f, 0.f, 1.f };

		EnsureWorldMatrix(object);

		const math::quaternion q = object->Transform_().GetWorldQuaternion();
		return { q.x, q.y, q.z, q.w };
	}

	void __stdcall Api_Transform_SetWorldRotation(ScriptObjectHandle handle, Float4 rotation)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		EnsureWorldMatrix(object);

		object->Transform_().SetWorldRotation({ rotation.x, rotation.y, rotation.z, rotation.w });
	}

	Float3 __stdcall Api_Transform_GetWorldScale(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 1.f, 1.f, 1.f };

		EnsureWorldMatrix(object);

		const math::vector3 s = object->Transform_().GetWorldScale();
		return { s.x, s.y, s.z };
	}

	void __stdcall Api_Transform_SetWorldScale(ScriptObjectHandle handle, Float3 scale)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		EnsureWorldMatrix(object);

		object->Transform_().SetWorldScale({ scale.x, scale.y, scale.z });
	}

	Float3 __stdcall Api_Transform_GetForward(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 0.f, 0.f, 1.f };

		EnsureWorldMatrix(object);

		const math::vector3 v = object->Transform_().GetForward();
		return { v.x, v.y, v.z };
	}

	Float3 __stdcall Api_Transform_GetRight(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 1.f, 0.f, 0.f };

		EnsureWorldMatrix(object);

		const math::vector3 v = object->Transform_().GetRight();
		return { v.x, v.y, v.z };
	}

	Float3 __stdcall Api_Transform_GetUp(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return { 0.f, 1.f, 0.f };

		EnsureWorldMatrix(object);

		const math::vector3 v = object->Transform_().GetUp();
		return { v.x, v.y, v.z };
	}

	int __stdcall Api_Prefab_Exists(const char* name)
	{
		if (nullptr == name) return 0;
		return (nullptr != PrefabUtilitys->LoadPrefab(name)) ? 1 : 0;
	}

	ScriptObjectHandle __stdcall Api_Prefab_Instantiate(const char* prefabName, const char* instanceName)
	{
		if (nullptr == prefabName) return {};

		Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName);
		if (nullptr == prefab)
		{
			Debug->LogWarning(std::string("[스크립트] 프리팹을 찾을 수 없습니다: ") + prefabName);
			return {};
		}

		const std::string name = (nullptr != instanceName) ? instanceName : "";
		Entity* instance = PrefabUtilitys->InstantiatePrefab(prefab, name);
		if (nullptr == instance)
		{
			Debug->LogWarning(std::string("[스크립트] 프리팹 인스턴스 생성 실패: ") + prefabName);
			return {};
		}

		// 이 호출이 반환되기 전에 새 오브젝트의 스크립트를 깨운다.
		//
		// 그러지 않으면 다음 프레임의 드레인까지 밀려서, 스폰 직후 대상을 초기화하는
		// 흔한 패턴이 한 프레임 늦게 동작한다(실측으로 확인한 문제).
		// DrainPendingLifecycle은 이미 깨운 컴포넌트를 플래그로 건너뛰므로 전체를 다시
		// 돌아도 안전하고, 프리팹이 자식을 여럿 두더라도 한 번에 처리된다.
		if (Scene* scene = SceneManagers->GetActiveScene())
		{
			scene->DrainPendingLifecycle();
		}

		return ScriptObjectRegistry::Get().Register(instance);
	}

	void __stdcall Api_Entity_Destroy(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		// 즉시 지우지 않는다. 엔진의 지연 파괴 규약을 그대로 따라야
		// 순회 중 컨테이너가 바뀌는 문제가 생기지 않는다.
		//
		// 핸들 무효화는 여기서 다시 부르지 않는다 — object->Destroy() 안에서
		// ScriptObjectRegistry::Unregister가 이미 불린다(Entity.cpp, 트랙 E4).
		// 예전에는 여기서 한 번 더 불렀는데, Destroy()가 스스로 Unregister하지
		// 않던 시절의 흔적이라 지금은 같은 객체를 두 번 Unregister하는 중복
		// 호출이었다(idempotent라 해는 없었지만 정본 지점이 둘로 보이는 문제였다).
		object->Destroy();
	}

	unsigned long long __stdcall Api_Engine_GetFrameCount()
	{
		return static_cast<unsigned long long>(Time->GetFrameCount());
	}

	// ── SoundComponent ──
	//
	// 컴포넌트에 별도 핸들을 주지 않고 오브젝트 핸들로 매번 찾는다.
	// 사운드 호출은 프레임당 수십 회 수준이라 이 조회가 병목이 되지 않고,
	// 컴포넌트용 슬롯 테이블을 따로 유지하는 쪽이 오히려 비싸다.
	SoundComponent* ResolveSound(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<SoundComponent>() : nullptr;
	}

	int __stdcall Api_Sound_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveSound(handle)) ? 1 : 0;
	}

	void __stdcall Api_Sound_Play(ScriptObjectHandle handle)
	{
		if (auto* sound = ResolveSound(handle)) sound->Play();
	}

	void __stdcall Api_Sound_Stop(ScriptObjectHandle handle)
	{
		if (auto* sound = ResolveSound(handle)) sound->Stop();
	}

	void __stdcall Api_Sound_Pause(ScriptObjectHandle handle, int pause)
	{
		if (auto* sound = ResolveSound(handle)) sound->Pause(0 != pause);
	}

	int __stdcall Api_Sound_IsPlaying(ScriptObjectHandle handle)
	{
		auto* sound = ResolveSound(handle);
		return (nullptr != sound && sound->IsPlaying()) ? 1 : 0;
	}

	void __stdcall Api_Sound_PlayOneShot(ScriptObjectHandle handle)
	{
		if (auto* sound = ResolveSound(handle)) sound->PlayOneShot();
	}

	int __stdcall Api_Sound_GetClipKey(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		auto* sound = ResolveSound(handle);
		if (nullptr == sound) return 0;

		const int length = static_cast<int>(std::min<size_t>(sound->clipKey.size(), static_cast<size_t>(capacity)));
		std::memcpy(buffer, sound->clipKey.data(), length);
		return length;
	}

	void __stdcall Api_Sound_SetClipKey(ScriptObjectHandle handle, const char* value)
	{
		if (nullptr == value) return;
		if (auto* sound = ResolveSound(handle)) sound->clipKey = value;
	}

	float __stdcall Api_Sound_GetVolume(ScriptObjectHandle handle)
	{
		auto* sound = ResolveSound(handle);
		return (nullptr != sound) ? sound->volume : 0.f;
	}

	void __stdcall Api_Sound_SetVolume(ScriptObjectHandle handle, float value)
	{
		if (auto* sound = ResolveSound(handle)) sound->volume = value;
	}

	float __stdcall Api_Sound_GetPitch(ScriptObjectHandle handle)
	{
		auto* sound = ResolveSound(handle);
		return (nullptr != sound) ? sound->pitch : 0.f;
	}

	void __stdcall Api_Sound_SetPitch(ScriptObjectHandle handle, float value)
	{
		if (auto* sound = ResolveSound(handle)) sound->pitch = value;
	}

	// ── Animator ──
	//
	// 파라미터는 이름으로 찾는다. 엔진이 Parameters를 선형 탐색하지만 개수가 십여 개
	// 수준이고, 인덱스를 캐시하려면 컨트롤러 교체·핫리로드마다 무효화 처리가 필요해서
	// 지금 단계에서는 값어치가 없다.

	Animator* ResolveAnimator(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<Animator>() : nullptr;
	}

	int __stdcall Api_Animator_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveAnimator(handle)) ? 1 : 0;
	}

	int __stdcall Api_Animator_HasParameter(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return 0;

		Animator* animator = ResolveAnimator(handle);
		return (nullptr != animator && nullptr != animator->FindParameter(name)) ? 1 : 0;
	}

	void __stdcall Api_Animator_SetBool(ScriptObjectHandle handle, const char* name, int value)
	{
		if (nullptr == name) return;
		if (auto* animator = ResolveAnimator(handle)) animator->SetParameter(name, 0 != value);
	}

	void __stdcall Api_Animator_SetFloat(ScriptObjectHandle handle, const char* name, float value)
	{
		if (nullptr == name) return;
		if (auto* animator = ResolveAnimator(handle)) animator->SetParameter(name, value);
	}

	void __stdcall Api_Animator_SetInt(ScriptObjectHandle handle, const char* name, int value)
	{
		if (nullptr == name) return;
		if (auto* animator = ResolveAnimator(handle)) animator->SetParameter(name, value);
	}

	void __stdcall Api_Animator_SetTrigger(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return;
		if (auto* animator = ResolveAnimator(handle)) animator->SetParameter(name, true);
	}

	void __stdcall Api_Animator_ResetTrigger(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return;

		Animator* animator = ResolveAnimator(handle);
		if (nullptr == animator) return;

		// ResetTrigger는 tValue만 되돌린다. SetParameter로 false를 넣는 것과 결과가
		// 같지만 트리거 전용 경로라 의도가 드러난다.
		if (ConditionParameter* parameter = animator->FindParameter(name)) parameter->ResetTrigger();
	}

	int __stdcall Api_Animator_GetBool(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return 0;

		Animator* animator = ResolveAnimator(handle);
		if (nullptr == animator) return 0;

		ConditionParameter* parameter = animator->FindParameter(name);
		return (nullptr != parameter && parameter->GetValue<bool>()) ? 1 : 0;
	}

	float __stdcall Api_Animator_GetFloat(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return 0.f;

		Animator* animator = ResolveAnimator(handle);
		if (nullptr == animator) return 0.f;

		ConditionParameter* parameter = animator->FindParameter(name);
		return (nullptr != parameter) ? parameter->GetValue<float>() : 0.f;
	}

	int __stdcall Api_Animator_GetInt(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return 0;

		Animator* animator = ResolveAnimator(handle);
		if (nullptr == animator) return 0;

		ConditionParameter* parameter = animator->FindParameter(name);
		return (nullptr != parameter) ? parameter->GetValue<int>() : 0;
	}

	void __stdcall Api_Animator_SetUseLayer(ScriptObjectHandle handle, int layerIndex, int useLayer)
	{
		if (auto* animator = ResolveAnimator(handle)) animator->SetUseLayer(layerIndex, 0 != useLayer);
	}

	void __stdcall Api_Animator_StopAnimation(ScriptObjectHandle handle, float duration)
	{
		if (auto* animator = ResolveAnimator(handle)) animator->StopAnimation(duration);
	}

	// ── CharacterControllerComponent ──
	//
	// 이동 입력은 Vector2를 그대로 넘기지 않고 float 둘로 편다 — 8바이트 구조체를
	// ABI마다 다르게 다루는 위험을 굳이 떠안을 이유가 없다.

	CharacterControllerComponent* ResolveCct(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<CharacterControllerComponent>() : nullptr;
	}

	int __stdcall Api_Cct_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveCct(handle)) ? 1 : 0;
	}

	void __stdcall Api_Cct_Move(ScriptObjectHandle handle, float inputX, float inputY)
	{
		if (auto* cct = ResolveCct(handle)) cct->Move({ inputX, inputY });
	}

	void __stdcall Api_Cct_TriggerForcedMove(ScriptObjectHandle handle, Float3 velocity, float duration)
	{
		// 이징 곡선 인자는 넘기지 않는다 — 게임 스크립트 8곳 어디도 쓰지 않아서
		// 엔진 기본값(None)으로 둔다. 필요해지면 enum을 C#에 미러링해 추가한다.
		if (auto* cct = ResolveCct(handle))
		{
			cct->TriggerForcedMove({ velocity.x, velocity.y, velocity.z }, duration);
		}
	}

	void __stdcall Api_Cct_StopForcedMove(ScriptObjectHandle handle)
	{
		if (auto* cct = ResolveCct(handle)) cct->StopForcedMove();
	}

	int __stdcall Api_Cct_IsInForcedMove(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct && cct->IsInForcedMove()) ? 1 : 0;
	}

	void __stdcall Api_Cct_SetAutomaticRotation(ScriptObjectHandle handle, int useAuto)
	{
		if (auto* cct = ResolveCct(handle)) cct->SetAutomaticRotation(0 != useAuto);
	}

	void __stdcall Api_Cct_SetLookDirection(ScriptObjectHandle handle, Float3 direction)
	{
		if (auto* cct = ResolveCct(handle)) cct->SetLookDirection({ direction.x, direction.y, direction.z });
	}

	void __stdcall Api_Cct_ClearLookDirection(ScriptObjectHandle handle)
	{
		if (auto* cct = ResolveCct(handle)) cct->ClearLookDirection();
	}

	void __stdcall Api_Cct_ForcedSetPosition(ScriptObjectHandle handle, Float3 position)
	{
		if (auto* cct = ResolveCct(handle)) cct->ForcedSetPosition({ position.x, position.y, position.z });
	}

	float __stdcall Api_Cct_GetBaseSpeed(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct) ? cct->GetBaseSpeed() : 0.f;
	}

	void __stdcall Api_Cct_SetBaseSpeed(ScriptObjectHandle handle, float speed)
	{
		if (auto* cct = ResolveCct(handle)) cct->SetBaseSpeed(speed);
	}

	int __stdcall Api_Cct_IsOnMove(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct && cct->IsOnMove()) ? 1 : 0;
	}

	void __stdcall Api_Cct_SetOnMove(ScriptObjectHandle handle, int isMove)
	{
		if (auto* cct = ResolveCct(handle)) cct->SetOnMove(0 != isMove);
	}

	int __stdcall Api_Cct_IsFalling(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct && cct->IsFalling()) ? 1 : 0;
	}

	// 아래 셋은 CharacterControllerInfo 구조체를 통째로 넘기는 대신 필드만 꺼낸다.
	// 스크립트가 실제로 읽는 것은 radius(3회)와 id(2회)뿐이라, 구조체를 경계에
	// 노출하면 필드가 하나 바뀔 때마다 양쪽 배치를 맞춰야 하는 부담만 남는다.

	float __stdcall Api_Cct_GetRadius(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct) ? cct->GetControllerInfo().radius : 0.f;
	}

	float __stdcall Api_Cct_GetHeight(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct) ? cct->GetControllerInfo().height : 0.f;
	}

	unsigned int __stdcall Api_Cct_GetId(ScriptObjectHandle handle)
	{
		auto* cct = ResolveCct(handle);
		return (nullptr != cct) ? cct->GetControllerInfo().id : 0u;
	}

	// ── RectTransformComponent ──

	RectTransformComponent* ResolveRect(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<RectTransformComponent>() : nullptr;
	}

	int __stdcall Api_Rect_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveRect(handle)) ? 1 : 0;
	}

	Float2 __stdcall Api_Rect_GetAnchoredPosition(ScriptObjectHandle handle)
	{
		auto* rect = ResolveRect(handle);
		if (nullptr == rect) return {};

		const auto p = rect->GetAnchoredPosition();
		return { p.x, p.y };
	}

	void __stdcall Api_Rect_SetAnchoredPosition(ScriptObjectHandle handle, Float2 position)
	{
		if (auto* rect = ResolveRect(handle)) rect->SetAnchoredPosition({ position.x, position.y });
	}

	Float2 __stdcall Api_Rect_GetSizeDelta(ScriptObjectHandle handle)
	{
		auto* rect = ResolveRect(handle);
		if (nullptr == rect) return {};

		const auto& s = rect->GetSizeDelta();
		return { s.x, s.y };
	}

	void __stdcall Api_Rect_SetSizeDelta(ScriptObjectHandle handle, Float2 size)
	{
		if (auto* rect = ResolveRect(handle)) rect->SetSizeDelta({ size.x, size.y });
	}

	Float2 __stdcall Api_Rect_GetPivot(ScriptObjectHandle handle)
	{
		auto* rect = ResolveRect(handle);
		if (nullptr == rect) return {};

		const auto& p = rect->GetPivot();
		return { p.x, p.y };
	}

	void __stdcall Api_Rect_SetPivot(ScriptObjectHandle handle, Float2 pivot)
	{
		if (auto* rect = ResolveRect(handle)) rect->SetPivot({ pivot.x, pivot.y });
	}

	// ── ImageComponent ──

	ImageComponent* ResolveImage(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<ImageComponent>() : nullptr;
	}

	int __stdcall Api_Image_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveImage(handle)) ? 1 : 0;
	}

	void __stdcall Api_Image_SetTexture(ScriptObjectHandle handle, int index)
	{
		if (auto* image = ResolveImage(handle)) image->SetTexture(index);
	}

	// 텍스처 목록 자체는 넘기지 않는다 — 스크립트가 쓰는 것은 개수(범위 검사)뿐이다.
	int __stdcall Api_Image_GetTextureCount(ScriptObjectHandle handle)
	{
		auto* image = ResolveImage(handle);
		return (nullptr != image) ? static_cast<int>(image->GetTextures().size()) : 0;
	}

	Float4 __stdcall Api_Image_GetColor(ScriptObjectHandle handle)
	{
		auto* image = ResolveImage(handle);
		if (nullptr == image) return { 1.f, 1.f, 1.f, 1.f };

		const auto& c = image->color;
		return { c.x, c.y, c.z, c.w };
	}

	void __stdcall Api_Image_SetColor(ScriptObjectHandle handle, Float4 color)
	{
		if (auto* image = ResolveImage(handle)) image->color = { color.x, color.y, color.z, color.w };
	}

	float __stdcall Api_Image_GetClipPercent(ScriptObjectHandle handle)
	{
		auto* image = ResolveImage(handle);
		return (nullptr != image) ? image->clipPercent : 0.f;
	}

	void __stdcall Api_Image_SetClipPercent(ScriptObjectHandle handle, float percent)
	{
		if (auto* image = ResolveImage(handle)) image->clipPercent = percent;
	}

	// uGUI의 SetNativeSize. 예전에는 Awake가 자동으로 했고, 그래서 스크립트가
	// sizeDelta를 정해도 다음 Awake에 지워졌다(분석 문서 F-7). 이제 부를 때만 동작한다.
	void __stdcall Api_Image_SetNativeSize(ScriptObjectHandle handle)
	{
		if (auto* image = ResolveImage(handle)) image->SetNativeSize();
	}

	// ── 카메라 ──
	//
	// 월드 좌표를 화면 픽셀로 바꾸는 코드가 게임 스크립트 11개 파일에 그대로 복제돼 있다
	// (뷰·투영 행렬을 곱하고 w로 나누고 NDC를 화면 크기로 펴는 20줄짜리가 매번 반복된다).
	// 행렬 타입을 경계에 노출하는 대신 결과만 넘긴다 — 호출도 한 번으로 줄어든다.
	CameraComponent* ResolvePrimaryCamera()
	{
		Scene* scene = SceneManagers->GetActiveScene();
		return (nullptr != scene) ? scene->Cameras().GetPrimaryCamera() : nullptr;
	}

	int __stdcall Api_Camera_Exists()
	{
		return ResolvePrimaryCamera() ? 1 : 0;
	}

	Float2 __stdcall Api_Camera_GetScreenSize()
	{
		CameraComponent* camera = ResolvePrimaryCamera();
		if (!camera) return {};

		const auto size = camera->GetCamera()->GetScreenSize();
		return { size.width, size.height };
	}

	Float3 __stdcall Api_Camera_WorldToScreenPoint(Float3 world)
	{
		CameraComponent* camera = ResolvePrimaryCamera();
		if (!camera) return { 0.f, 0.f, 0.f };

		const FrameCameraSnapshot snapshot = camera->CaptureFrameSnapshot();
		const DirectX::XMMATRIX viewProj =
			MathematicsInterop::ToDirectX(snapshot.view * snapshot.projection);

		const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
			DirectX::XMVectorSet(world.x, world.y, world.z, 1.f), viewProj);

		const float w = DirectX::XMVectorGetW(clip);

		// z(=w)는 카메라 앞쪽 거리다. 0 이하면 카메라 뒤라 화면 좌표가 의미 없다 —
		// 호출부가 z <= 0으로 걸러 내도록 그대로 넘긴다(Unity와 같은 규약).
		if (w <= 0.f) return { 0.f, 0.f, w };

		const float ndcX = DirectX::XMVectorGetX(clip) / w;
		const float ndcY = DirectX::XMVectorGetY(clip) / w;

		const auto size = camera->GetCamera()->GetScreenSize();
		return { (ndcX + 1.f) * 0.5f * size.width, (1.f - ndcY) * 0.5f * size.height, w };
	}

	// ── MeshRenderer · Material ──
	//
	// Material에 별도 핸들을 주지 않고 MeshRenderer를 통해서만 만진다.
	// 재질은 GameObject가 아니라 세대 핸들을 걸 자리가 없고, 스크립트가 하는 일도
	// "이 오브젝트의 재질을 손본다"가 전부라 소유자 핸들 하나면 충분하다.

	MeshRenderer* ResolveMesh(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<MeshRenderer>() : nullptr;
	}

	Material* ResolveMaterial(ScriptObjectHandle handle)
	{
		MeshRenderer* mesh = ResolveMesh(handle);
		return (nullptr != mesh) ? mesh->m_Material.get() : nullptr;
	}

	int __stdcall Api_Mesh_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveMesh(handle)) ? 1 : 0;
	}

	// 재질은 기본적으로 여러 오브젝트가 공유한다. 한 오브젝트만 색이나 상수를 바꾸려면
	// 먼저 사본을 만들어야 하고, 그러지 않으면 같은 재질을 쓰는 것들이 전부 함께 변한다.
	void __stdcall Api_Mesh_InstantiateMaterial(ScriptObjectHandle handle, const char* newName)
	{
		MeshRenderer* mesh = ResolveMesh(handle);
		if (nullptr == mesh || !mesh->m_Material) return;

		mesh->m_Material = Material::InstantiateShared(mesh->m_Material.get(),
			(nullptr != newName) ? std::string_view{ newName } : std::string_view{});
	}

	int __stdcall Api_Mesh_GetMaterialName(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		Material* material = ResolveMaterial(handle);
		if (nullptr == material) return 0;

		// Material::m_name은 GameObject와 달리 std::string이다.
		const std::string& name = material->m_name;
		const int length = static_cast<int>(std::min<size_t>(name.size(), static_cast<size_t>(capacity)));
		std::memcpy(buffer, name.data(), length);
		return length;
	}

	// 셰이더 상수 버퍼에 값을 넣는다. 이름이 틀리면 엔진이 조용히 실패하므로
	// 결과를 그대로 돌려준다 — 오타를 삼키지 않으려면 호출부가 봐야 한다.
	int __stdcall Api_Mesh_SetMaterialFloat(ScriptObjectHandle handle, const char* buffer, const char* name, float value)
	{
		if (nullptr == buffer || nullptr == name) return 0;

		Material* material = ResolveMaterial(handle);
		if (nullptr == material) return 0;

		return material->TrySetValue(buffer, name, &value, sizeof(value)) ? 1 : 0;
	}

	int __stdcall Api_Mesh_SetMaterialInt(ScriptObjectHandle handle, const char* buffer, const char* name, int value)
	{
		if (nullptr == buffer || nullptr == name) return 0;

		Material* material = ResolveMaterial(handle);
		if (nullptr == material) return 0;

		return material->TrySetValue(buffer, name, &value, sizeof(value)) ? 1 : 0;
	}

	Float4 __stdcall Api_Mesh_GetBaseColor(ScriptObjectHandle handle)
	{
		Material* material = ResolveMaterial(handle);
		if (nullptr == material) return { 1.f, 1.f, 1.f, 1.f };

		const auto& c = material->m_materialInfo.m_baseColor;
		return { c.x, c.y, c.z, c.w };
	}

	void __stdcall Api_Mesh_SetBaseColor(ScriptObjectHandle handle, Float4 color)
	{
		if (Material* material = ResolveMaterial(handle))
		{
			material->m_materialInfo.m_baseColor = { color.x, color.y, color.z, color.w };
		}
	}

	// ── 입력 ──
	//
	// 엔진의 KeyState 전이는 Idle → Down(첫 프레임) → Pressed(유지) → Released(뗀 프레임)다.
	// Pressed가 첫 프레임을 포함하지 않는 것이 함정이라, 상태를 그대로 넘겨
	// "눌려 있는가"를 C#에서 Down|Pressed로 정확히 조합하게 한다.

	int __stdcall Api_Input_GetKeyState(int key)
	{
		if (key < 0 || key >= KEYBOARD_COUNT) return static_cast<int>(KeyState::Idle);
		return static_cast<int>(InputManagement->m_keyboardState.GetKeyState(static_cast<size_t>(key)));
	}

	int __stdcall Api_Input_GetMouseButtonState(int button)
	{
		if (button < 0 || button >= static_cast<int>(MouseKey::MAX)) return static_cast<int>(KeyState::Idle);

		// 마우스 상태는 private이라 술어 셋으로 되짚는다.
		const MouseKey key = static_cast<MouseKey>(button);
		if (InputManagement->IsMouseButtonDown(key))     return static_cast<int>(KeyState::Down);
		if (InputManagement->IsMouseButtonPressed(key))  return static_cast<int>(KeyState::Pressed);
		if (InputManagement->IsMouseButtonReleased(key)) return static_cast<int>(KeyState::Released);
		return static_cast<int>(KeyState::Idle);
	}

	int __stdcall Api_Input_GetControllerButtonState(int index, int button)
	{
		if (index < 0 || button < 0 || button >= static_cast<int>(ControllerButton::MAX))
		{
			return static_cast<int>(KeyState::Idle);
		}

		const DWORD pad = static_cast<DWORD>(index);
		const ControllerButton btn = static_cast<ControllerButton>(button);

		if (InputManagement->IsControllerButtonDown(pad, btn))     return static_cast<int>(KeyState::Down);
		if (InputManagement->IsControllerButtonPressed(pad, btn))  return static_cast<int>(KeyState::Pressed);
		if (InputManagement->IsControllerButtonReleased(pad, btn)) return static_cast<int>(KeyState::Released);
		return static_cast<int>(KeyState::Idle);
	}

	int __stdcall Api_Input_IsAnyKeyPressed()
	{
		return InputManagement->IsAnyKeyPressed() ? 1 : 0;
	}

	Float2 __stdcall Api_Input_GetMousePosition()
	{
		const auto p = InputManagement->GetMousePos();
		return { p.x, p.y };
	}

	Float2 __stdcall Api_Input_GetMouseDelta()
	{
		const auto d = InputManagement->GetMouseDelta();
		return { d.x, d.y };
	}

	int __stdcall Api_Input_GetWheelDelta()
	{
		return static_cast<int>(InputManagement->GetWheelDelta());
	}

	void __stdcall Api_Input_SetCursorVisible(int visible)
	{
		if (0 != visible) InputManagement->ShowCursor();
		else              InputManagement->HideCursor();
	}

	int __stdcall Api_Input_IsControllerConnected(int index)
	{
		if (index < 0) return 0;
		return InputManagement->IsControllerConnected(static_cast<DWORD>(index)) ? 1 : 0;
	}

	int __stdcall Api_Input_IsControllerTriggerL(int index)
	{
		if (index < 0) return 0;
		return InputManagement->IsControllerTriggerL(static_cast<DWORD>(index)) ? 1 : 0;
	}

	int __stdcall Api_Input_IsControllerTriggerR(int index)
	{
		if (index < 0) return 0;
		return InputManagement->IsControllerTriggerR(static_cast<DWORD>(index)) ? 1 : 0;
	}

	Float2 __stdcall Api_Input_GetControllerThumbL(int index)
	{
		if (index < 0) return {};
		const auto v = InputManagement->GetControllerThumbL(static_cast<DWORD>(index));
		return { v.x, v.y };
	}

	Float2 __stdcall Api_Input_GetControllerThumbR(int index)
	{
		if (index < 0) return {};
		const auto v = InputManagement->GetControllerThumbR(static_cast<DWORD>(index));
		return { v.x, v.y };
	}

	// ── 물리 질의 ──
	//
	// 결과 개수가 정해지지 않아 호출자가 준 버퍼에 채우고 개수를 돌려준다.
	// 반환값은 "실제로 맞은 개수"라 capacity보다 클 수 있다 — 잘렸는지 호출부가 알아야
	// 버퍼를 늘릴지 판단할 수 있기 때문이다(버퍼에는 capacity까지만 쓴다).

	void FillHitResult(ScriptHitResult& out, Entity* object, unsigned int layer,
		const Mathf::Vector3& point, const Mathf::Vector3& normal, float distance)
	{
		out.object = (nullptr != object) ? ScriptObjectRegistry::Get().Register(object) : ScriptObjectHandle{};
		out.layer = layer;
		out.point = { point.x, point.y, point.z };
		out.normal = { normal.x, normal.y, normal.z };
		out.distance = distance;
	}

	int __stdcall Api_Physics_Raycast(Float3 origin, Float3 direction, float distance,
		unsigned int layerMask, ScriptHitResult* hit)
	{
		if (nullptr == hit) return 0;

		RayEvent rayEvent{};
		rayEvent.origin = { origin.x, origin.y, origin.z };
		rayEvent.direction = { direction.x, direction.y, direction.z };
		rayEvent.distance = distance;
		rayEvent.layerMask = layerMask;

		RaycastHit result{};
		if (!PhysicsManagers->Raycast(rayEvent, result)) return 0;

		FillHitResult(*hit, result.hitObject, result.hitObjectLayer,
			result.hitPoint, result.hitNormal, distance);
		return 1;
	}

	int __stdcall Api_Physics_RaycastAll(Float3 origin, Float3 direction, float distance,
		unsigned int layerMask, ScriptHitResult* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		RayEvent rayEvent{};
		rayEvent.origin = { origin.x, origin.y, origin.z };
		rayEvent.direction = { direction.x, direction.y, direction.z };
		rayEvent.distance = distance;
		rayEvent.layerMask = layerMask;

		std::vector<RaycastHit> hits;
		const int count = PhysicsManagers->Raycast(rayEvent, hits);

		const int written = std::min(static_cast<int>(hits.size()), capacity);
		for (int i = 0; i < written; ++i)
		{
			FillHitResult(buffer[i], hits[i].hitObject, hits[i].hitObjectLayer,
				hits[i].hitPoint, hits[i].hitNormal, distance);
		}

		return count;
	}

	int __stdcall Api_Physics_OverlapSphere(Float3 position, float radius,
		unsigned int layerMask, ScriptHitResult* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		OverlapInput input{};
		input.position = { position.x, position.y, position.z };
		input.rotation = DirectX::SimpleMath::Quaternion::Identity;
		input.layerMask = layerMask;

		std::vector<HitResult> hits;
		const int count = PhysicsManagers->SphereOverlap(input, radius, hits);

		const int written = std::min(static_cast<int>(hits.size()), capacity);
		for (int i = 0; i < written; ++i)
		{
			FillHitResult(buffer[i], hits[i].gameObject, hits[i].layer,
				hits[i].point, hits[i].normal, hits[i].distance);
		}

		return count;
	}

	// ── RigidBodyComponent ──

	RigidBodyComponent* ResolveRigid(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<RigidBodyComponent>() : nullptr;
	}

	int __stdcall Api_Rigid_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveRigid(handle)) ? 1 : 0;
	}

	Float3 __stdcall Api_Rigid_GetLinearVelocity(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		if (nullptr == rigid) return {};
		const auto v = rigid->GetLinearVelocity();
		return { v.x, v.y, v.z };
	}

	void __stdcall Api_Rigid_SetLinearVelocity(ScriptObjectHandle handle, Float3 velocity)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetLinearVelocity({ velocity.x, velocity.y, velocity.z });
	}

	void __stdcall Api_Rigid_AddLinearVelocity(ScriptObjectHandle handle, Float3 velocity)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->AddLinearVelocity({ velocity.x, velocity.y, velocity.z });
	}

	Float3 __stdcall Api_Rigid_GetAngularVelocity(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		if (nullptr == rigid) return {};
		const auto v = rigid->GetAngularVelocity();
		return { v.x, v.y, v.z };
	}

	void __stdcall Api_Rigid_SetAngularVelocity(ScriptObjectHandle handle, Float3 velocity)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetAngularVelocity({ velocity.x, velocity.y, velocity.z });
	}

	void __stdcall Api_Rigid_AddForce(ScriptObjectHandle handle, Float3 force, int forceMode)
	{
		if (auto* rigid = ResolveRigid(handle))
		{
			rigid->AddForce({ force.x, force.y, force.z }, static_cast<EForceMode>(forceMode));
		}
	}

	void __stdcall Api_Rigid_SetBodyType(ScriptObjectHandle handle, int bodyType)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetBodyType(static_cast<EBodyType>(bodyType));
	}

	int __stdcall Api_Rigid_IsKinematic(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		return (nullptr != rigid && rigid->IsKinematic()) ? 1 : 0;
	}

	void __stdcall Api_Rigid_SetKinematic(ScriptObjectHandle handle, int kinematic)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetKinematic(0 != kinematic);
	}

	int __stdcall Api_Rigid_IsTrigger(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		return (nullptr != rigid && rigid->IsTrigger()) ? 1 : 0;
	}

	void __stdcall Api_Rigid_SetIsTrigger(ScriptObjectHandle handle, int isTrigger)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetIsTrigger(0 != isTrigger);
	}

	int __stdcall Api_Rigid_IsColliderEnabled(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		return (nullptr != rigid && rigid->IsColliderEnabled()) ? 1 : 0;
	}

	void __stdcall Api_Rigid_SetColliderEnabled(ScriptObjectHandle handle, int enabled)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetColliderEnabled(0 != enabled);
	}

	int __stdcall Api_Rigid_IsUsingGravity(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		return (nullptr != rigid && rigid->IsUsingGravity()) ? 1 : 0;
	}

	void __stdcall Api_Rigid_UseGravity(ScriptObjectHandle handle, int useGravity)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->UseGravity(0 != useGravity);
	}

	float __stdcall Api_Rigid_GetMass(ScriptObjectHandle handle)
	{
		auto* rigid = ResolveRigid(handle);
		return (nullptr != rigid) ? rigid->GetMass() : 0.f;
	}

	void __stdcall Api_Rigid_SetMass(ScriptObjectHandle handle, float mass)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetMass(mass);
	}

	void __stdcall Api_Rigid_SetLinearDamping(ScriptObjectHandle handle, float damping)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetLinearDamping(damping);
	}

	void __stdcall Api_Rigid_SetAngularDamping(ScriptObjectHandle handle, float damping)
	{
		if (auto* rigid = ResolveRigid(handle)) rigid->SetAngularDamping(damping);
	}

	void __stdcall Api_Rigid_SetScale(ScriptObjectHandle handle, Float3 scale)
	{
		auto* rigid = ResolveRigid(handle);
		if (nullptr == rigid) return;

		// 엔진이 비상수 참조를 받아 임시 값을 넘길 수 없다.
		Mathf::Vector3 value{ scale.x, scale.y, scale.z };
		rigid->SetScale(value);
	}

	void __stdcall Api_Rigid_SetLockLinear(ScriptObjectHandle handle, int x, int y, int z)
	{
		auto* rigid = ResolveRigid(handle);
		if (nullptr == rigid) return;

		rigid->SetLockLinearX(0 != x);
		rigid->SetLockLinearY(0 != y);
		rigid->SetLockLinearZ(0 != z);
	}

	void __stdcall Api_Rigid_SetLockAngular(ScriptObjectHandle handle, int x, int y, int z)
	{
		auto* rigid = ResolveRigid(handle);
		if (nullptr == rigid) return;

		rigid->SetLockAngularX(0 != x);
		rigid->SetLockAngularY(0 != y);
		rigid->SetLockAngularZ(0 != z);
	}

	// ── 콜라이더 3종 ──
	//
	// Sphere·Box·Capsule은 표면이 거의 같은데 공통 기반(ICollider)에는 크기·마찰이
	// 올라와 있지 않다. 타입마다 함수를 세 벌 만드는 대신 종류를 인자로 받아 디스패치한다.
	// 없는 조합(구의 높이 등)은 조용히 기본값으로 빠진다.

	enum class ColliderKind : int { Sphere = 0, Box = 1, Capsule = 2 };

	template <typename TCollider>
	TCollider* ResolveColliderAs(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<TCollider>() : nullptr;
	}

	int __stdcall Api_Collider_Exists(ScriptObjectHandle handle, int kind)
	{
		switch (static_cast<ColliderKind>(kind))
		{
		case ColliderKind::Sphere:  return (nullptr != ResolveColliderAs<SphereColliderComponent>(handle)) ? 1 : 0;
		case ColliderKind::Box:     return (nullptr != ResolveColliderAs<BoxColliderComponent>(handle)) ? 1 : 0;
		case ColliderKind::Capsule: return (nullptr != ResolveColliderAs<CapsuleColliderComponent>(handle)) ? 1 : 0;
		default: return 0;
		}
	}

	float __stdcall Api_Collider_GetRadius(ScriptObjectHandle handle, int kind)
	{
		if (ColliderKind::Sphere == static_cast<ColliderKind>(kind))
		{
			auto* c = ResolveColliderAs<SphereColliderComponent>(handle);
			return (nullptr != c) ? c->GetRadius() : 0.f;
		}
		if (ColliderKind::Capsule == static_cast<ColliderKind>(kind))
		{
			auto* c = ResolveColliderAs<CapsuleColliderComponent>(handle);
			return (nullptr != c) ? c->GetRadius() : 0.f;
		}
		return 0.f;
	}

	void __stdcall Api_Collider_SetRadius(ScriptObjectHandle handle, int kind, float radius)
	{
		if (ColliderKind::Sphere == static_cast<ColliderKind>(kind))
		{
			if (auto* c = ResolveColliderAs<SphereColliderComponent>(handle)) c->SetRadius(radius);
		}
		else if (ColliderKind::Capsule == static_cast<ColliderKind>(kind))
		{
			if (auto* c = ResolveColliderAs<CapsuleColliderComponent>(handle)) c->SetRadius(radius);
		}
	}

	float __stdcall Api_Collider_GetHeight(ScriptObjectHandle handle, int kind)
	{
		if (ColliderKind::Capsule != static_cast<ColliderKind>(kind)) return 0.f;

		auto* c = ResolveColliderAs<CapsuleColliderComponent>(handle);
		return (nullptr != c) ? c->GetHeight() : 0.f;
	}

	void __stdcall Api_Collider_SetHeight(ScriptObjectHandle handle, int kind, float height)
	{
		if (ColliderKind::Capsule != static_cast<ColliderKind>(kind)) return;
		if (auto* c = ResolveColliderAs<CapsuleColliderComponent>(handle)) c->SetHeight(height);
	}

	Float3 __stdcall Api_Collider_GetExtents(ScriptObjectHandle handle, int kind)
	{
		if (ColliderKind::Box != static_cast<ColliderKind>(kind)) return {};

		auto* c = ResolveColliderAs<BoxColliderComponent>(handle);
		if (nullptr == c) return {};

		const auto e = c->GetExtents();
		return { e.x, e.y, e.z };
	}

	void __stdcall Api_Collider_SetExtents(ScriptObjectHandle handle, int kind, Float3 extents)
	{
		if (ColliderKind::Box != static_cast<ColliderKind>(kind)) return;
		if (auto* c = ResolveColliderAs<BoxColliderComponent>(handle))
		{
			c->SetExtents({ extents.x, extents.y, extents.z });
		}
	}

	// 아래 공통 항목들은 ICollider에 올라와 있지 않아 종류별로 갈라야 한다.
	// 반복을 줄이려고 람다에 세 타입을 태워 돌린다.
	template <typename TFunc>
	auto WithCollider(ScriptObjectHandle handle, int kind, TFunc&& func)
	{
		using Result = decltype(func(std::declval<SphereColliderComponent&>()));

		switch (static_cast<ColliderKind>(kind))
		{
		case ColliderKind::Sphere:
			if (auto* c = ResolveColliderAs<SphereColliderComponent>(handle)) return func(*c);
			break;
		case ColliderKind::Box:
			if (auto* c = ResolveColliderAs<BoxColliderComponent>(handle)) return func(*c);
			break;
		case ColliderKind::Capsule:
			if (auto* c = ResolveColliderAs<CapsuleColliderComponent>(handle)) return func(*c);
			break;
		default: break;
		}
		return Result{};
	}

	Float3 __stdcall Api_Collider_GetPositionOffset(ScriptObjectHandle handle, int kind)
	{
		return WithCollider(handle, kind, [](auto& c) -> Float3
		{
			const auto p = c.GetPositionOffset();
			return { p.x, p.y, p.z };
		});
	}

	void __stdcall Api_Collider_SetPositionOffset(ScriptObjectHandle handle, int kind, Float3 offset)
	{
		WithCollider(handle, kind, [&](auto& c) -> int
		{
			c.SetPositionOffset({ offset.x, offset.y, offset.z });
			return 0;
		});
	}

	float __stdcall Api_Collider_GetRestitution(ScriptObjectHandle handle, int kind)
	{
		return WithCollider(handle, kind, [](auto& c) { return c.GetRestitution(); });
	}

	void __stdcall Api_Collider_SetRestitution(ScriptObjectHandle handle, int kind, float value)
	{
		WithCollider(handle, kind, [&](auto& c) -> int { c.SetRestitution(value); return 0; });
	}

	float __stdcall Api_Collider_GetStaticFriction(ScriptObjectHandle handle, int kind)
	{
		return WithCollider(handle, kind, [](auto& c) { return c.GetStaticFriction(); });
	}

	void __stdcall Api_Collider_SetStaticFriction(ScriptObjectHandle handle, int kind, float value)
	{
		WithCollider(handle, kind, [&](auto& c) -> int { c.SetStaticFriction(value); return 0; });
	}

	float __stdcall Api_Collider_GetDynamicFriction(ScriptObjectHandle handle, int kind)
	{
		return WithCollider(handle, kind, [](auto& c) { return c.GetDynamicFriction(); });
	}

	void __stdcall Api_Collider_SetDynamicFriction(ScriptObjectHandle handle, int kind, float value)
	{
		WithCollider(handle, kind, [&](auto& c) -> int { c.SetDynamicFriction(value); return 0; });
	}

	// ── TextComponent · UIComponent · Canvas ──

	TextComponent* ResolveText(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<TextComponent>() : nullptr;
	}

	int __stdcall Api_Text_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveText(handle)) ? 1 : 0;
	}

	int __stdcall Api_Text_GetMessage(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		auto* text = ResolveText(handle);
		if (nullptr == text) return 0;

		const std::string message = text->GetTextMessage();
		const int length = static_cast<int>(std::min<size_t>(message.size(), static_cast<size_t>(capacity)));
		std::memcpy(buffer, message.data(), length);
		return length;
	}

	void __stdcall Api_Text_SetMessage(ScriptObjectHandle handle, const char* message)
	{
		if (nullptr == message) return;
		if (auto* text = ResolveText(handle)) text->SetMessage(message);
	}

	Float4 __stdcall Api_Text_GetColor(ScriptObjectHandle handle)
	{
		auto* text = ResolveText(handle);
		if (nullptr == text) return { 1.f, 1.f, 1.f, 1.f };

		const auto c = text->GetColor();
		return { c.x, c.y, c.z, c.w };
	}

	void __stdcall Api_Text_SetColor(ScriptObjectHandle handle, Float4 color)
	{
		if (auto* text = ResolveText(handle)) text->SetColor({ color.x, color.y, color.z, color.w });
	}

	// 알파만 따로 여는 이유: 페이드가 흔해서 실측 6회로 SetMessage 다음이다.
	// 색 전체를 읽고-고쳐-쓰면 경계를 두 번 넘는다.
	float __stdcall Api_Text_GetAlpha(ScriptObjectHandle handle)
	{
		auto* text = ResolveText(handle);
		return (nullptr != text) ? text->GetAlpha() : 0.f;
	}

	void __stdcall Api_Text_SetAlpha(ScriptObjectHandle handle, float alpha)
	{
		if (auto* text = ResolveText(handle)) text->SetAlpha(alpha);
	}

	float __stdcall Api_Text_GetFontSize(ScriptObjectHandle handle)
	{
		auto* text = ResolveText(handle);
		return (nullptr != text) ? text->GetFontSize() : 0.f;
	}

	void __stdcall Api_Text_SetFontSize(ScriptObjectHandle handle, float size)
	{
		if (auto* text = ResolveText(handle)) text->SetFontSize(size);
	}

	Float2 __stdcall Api_Text_GetRelativePosition(ScriptObjectHandle handle)
	{
		auto* text = ResolveText(handle);
		if (nullptr == text) return {};

		const auto p = text->GetRelativePosition();
		return { p.x, p.y };
	}

	void __stdcall Api_Text_SetRelativePosition(ScriptObjectHandle handle, Float2 position)
	{
		if (auto* text = ResolveText(handle)) text->SetRelativePosition({ position.x, position.y });
	}

	// 그리기 순서는 UIComponent에 있어 Image·Text가 함께 쓴다.
	// 정확한 타입을 모르므로 기반 타입으로 찾는다(GetComponent는 정확 일치라 못 쓴다).
	UIComponent* ResolveUi(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponentDynamicCast<UIComponent>() : nullptr;
	}

	int __stdcall Api_Ui_GetOrder(ScriptObjectHandle handle)
	{
		auto* ui = ResolveUi(handle);
		return (nullptr != ui) ? ui->GetLayerOrder() : 0;
	}

	void __stdcall Api_Ui_SetOrder(ScriptObjectHandle handle, int order)
	{
		if (auto* ui = ResolveUi(handle)) ui->SetOrder(order);
	}

	Canvas* ResolveCanvas(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<Canvas>() : nullptr;
	}

	int __stdcall Api_Canvas_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveCanvas(handle)) ? 1 : 0;
	}

	int __stdcall Api_Canvas_GetOrder(ScriptObjectHandle handle)
	{
		auto* canvas = ResolveCanvas(handle);
		return (nullptr != canvas) ? canvas->GetCanvasOrder() : 0;
	}

	void __stdcall Api_Canvas_SetOrder(ScriptObjectHandle handle, int order)
	{
		if (auto* canvas = ResolveCanvas(handle)) canvas->SetCanvasOrder(order);
	}

	int __stdcall Api_Canvas_GetName(ScriptObjectHandle handle, char* buffer, int capacity)
	{
		if (nullptr == buffer || capacity <= 0) return 0;

		auto* canvas = ResolveCanvas(handle);
		if (nullptr == canvas) return 0;

		const std::string& name = canvas->CanvasName;
		const int length = static_cast<int>(std::min<size_t>(name.size(), static_cast<size_t>(capacity)));
		std::memcpy(buffer, name.data(), length);
		return length;
	}

	void __stdcall Api_Canvas_SetName(ScriptObjectHandle handle, const char* name)
	{
		if (nullptr == name) return;
		if (auto* canvas = ResolveCanvas(handle)) canvas->SetCanvasName(name);
	}

	// ── UI 내비게이션·버튼·Image 잔여 ──
	//
	// 게임의 메뉴 버튼은 UIButton보다 "ImageComponent + 선택 확인 + 입력" 조합이
	// 대세다(ImageButton.cpp 패턴). 그래서 선택·잠금은 UIComponent 공통으로 열고,
	// UIButton 클릭은 콜백 대신 틱 폴링(ConsumeClicked)으로 전달한다.

	int __stdcall Api_Ui_IsSelected(ScriptObjectHandle handle)
	{
		auto* ui = ResolveUi(handle);
		return (nullptr != ui && ui->IsNavigationThis()) ? 1 : 0;
	}

	int __stdcall Api_Ui_IsNavLocked(ScriptObjectHandle handle)
	{
		auto* ui = ResolveUi(handle);
		return (nullptr != ui && ui->IsNavLock()) ? 1 : 0;
	}

	void __stdcall Api_Ui_SetNavLock(ScriptObjectHandle handle, int locked)
	{
		if (auto* ui = ResolveUi(handle)) ui->SetNavLock(0 != locked);
	}

	ScriptObjectHandle __stdcall Api_UiNav_GetSelected()
	{
		Entity* selected = UIManagers->GetSelectUI();
		return (nullptr != selected) ? ScriptObjectRegistry::Get().Register(selected) : ScriptObjectHandle{};
	}

	void __stdcall Api_UiNav_SetSelected(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		if (nullptr == object) return;

		UIManagers->SetSelectUI(object);
	}

	UIButton* ResolveButton(ScriptObjectHandle handle)
	{
		Entity* object = ScriptObjectRegistry::Get().Resolve(handle);
		return (nullptr != object) ? object->GetComponent<UIButton>() : nullptr;
	}

	int __stdcall Api_Button_Exists(ScriptObjectHandle handle)
	{
		return (nullptr != ResolveButton(handle)) ? 1 : 0;
	}

	int __stdcall Api_Button_ConsumeClicked(ScriptObjectHandle handle)
	{
		auto* button = ResolveButton(handle);
		return (nullptr != button && button->ConsumeClicked()) ? 1 : 0;
	}

	int __stdcall Api_Image_GetTextureIndex(ScriptObjectHandle handle)
	{
		auto* image = ResolveImage(handle);
		return (nullptr != image) ? image->curindex : 0;
	}

	float __stdcall Api_Image_GetRotation(ScriptObjectHandle handle)
	{
		auto* image = ResolveImage(handle);
		return (nullptr != image) ? image->rotate : 0.f;
	}

	void __stdcall Api_Image_SetRotation(ScriptObjectHandle handle, float rotation)
	{
		if (auto* image = ResolveImage(handle)) image->rotate = rotation;
	}

	Float4 __stdcall Api_Rect_GetWorldRect(ScriptObjectHandle handle)
	{
		auto* rect = ResolveRect(handle);
		if (nullptr == rect) return {};

		const auto& worldRect = rect->GetWorldRect();
		return { worldRect.x, worldRect.y, worldRect.width, worldRect.height };
	}

	Float2 __stdcall Api_Rect_GetScreenPosition(ScriptObjectHandle handle)
	{
		auto* rect = ResolveRect(handle);
		if (nullptr == rect) return {};

		const auto position = rect->GetScreenPosition();
		return { position.x, position.y };
	}

	void __stdcall Api_Rect_SetScreenPosition(ScriptObjectHandle handle, Float2 position)
	{
		if (auto* rect = ResolveRect(handle))
			rect->SetScreenPosition({ position.x, position.y });
	}

	void FillApiTable()
	{
		g_apiTable.version    = kApiVersion;
		g_apiTable.structSize = static_cast<int>(sizeof(ScriptApiTable));

		g_apiTable.Log                         = &Api_Log;
		g_apiTable.Entity_FindByName       = &Api_Entity_FindByName;
		g_apiTable.Entity_IsAlive          = &Api_Entity_IsAlive;
		g_apiTable.Entity_GetName          = &Api_Entity_GetName;
		g_apiTable.Entity_SetEnabled       = &Api_Entity_SetEnabled;
		g_apiTable.Entity_GetChildCount    = &Api_Entity_GetChildCount;
		g_apiTable.Entity_GetChild         = &Api_Entity_GetChild;
		g_apiTable.Entity_GetParent        = &Api_Entity_GetParent;
		g_apiTable.Entity_FindByIndex      = &Api_Entity_FindByIndex;
		g_apiTable.Entity_GetIndex         = &Api_Entity_GetIndex;
		g_apiTable.Transform_GetLocalPosition  = &Api_Transform_GetLocalPosition;
		g_apiTable.Transform_SetLocalPosition  = &Api_Transform_SetLocalPosition;
		g_apiTable.Transform_GetWorldPosition  = &Api_Transform_GetWorldPosition;
		g_apiTable.Transform_GetLocalRotation  = &Api_Transform_GetLocalRotation;
		g_apiTable.Transform_SetLocalRotation  = &Api_Transform_SetLocalRotation;
		g_apiTable.Transform_GetLocalScale     = &Api_Transform_GetLocalScale;
		g_apiTable.Transform_SetLocalScale     = &Api_Transform_SetLocalScale;
		g_apiTable.Transform_AddLocalPosition  = &Api_Transform_AddLocalPosition;
		g_apiTable.Transform_AddLocalRotation  = &Api_Transform_AddLocalRotation;
		g_apiTable.Transform_SetWorldPosition  = &Api_Transform_SetWorldPosition;
		g_apiTable.Transform_GetWorldRotation  = &Api_Transform_GetWorldRotation;
		g_apiTable.Transform_SetWorldRotation  = &Api_Transform_SetWorldRotation;
		g_apiTable.Transform_GetWorldScale     = &Api_Transform_GetWorldScale;
		g_apiTable.Transform_SetWorldScale     = &Api_Transform_SetWorldScale;
		g_apiTable.Transform_GetForward        = &Api_Transform_GetForward;
		g_apiTable.Transform_GetRight          = &Api_Transform_GetRight;
		g_apiTable.Transform_GetUp             = &Api_Transform_GetUp;
		g_apiTable.Prefab_Exists               = &Api_Prefab_Exists;
		g_apiTable.Prefab_Instantiate          = &Api_Prefab_Instantiate;
		g_apiTable.Entity_Destroy          = &Api_Entity_Destroy;
		g_apiTable.Engine_GetFrameCount        = &Api_Engine_GetFrameCount;

		g_apiTable.Sound_Exists                = &Api_Sound_Exists;
		g_apiTable.Sound_Play                  = &Api_Sound_Play;
		g_apiTable.Sound_Stop                  = &Api_Sound_Stop;
		g_apiTable.Sound_Pause                 = &Api_Sound_Pause;
		g_apiTable.Sound_IsPlaying             = &Api_Sound_IsPlaying;
		g_apiTable.Sound_PlayOneShot           = &Api_Sound_PlayOneShot;
		g_apiTable.Sound_GetClipKey            = &Api_Sound_GetClipKey;
		g_apiTable.Sound_SetClipKey            = &Api_Sound_SetClipKey;
		g_apiTable.Sound_GetVolume             = &Api_Sound_GetVolume;
		g_apiTable.Sound_SetVolume             = &Api_Sound_SetVolume;
		g_apiTable.Sound_GetPitch              = &Api_Sound_GetPitch;
		g_apiTable.Sound_SetPitch              = &Api_Sound_SetPitch;

		g_apiTable.Animator_Exists             = &Api_Animator_Exists;
		g_apiTable.Animator_HasParameter       = &Api_Animator_HasParameter;
		g_apiTable.Animator_SetBool            = &Api_Animator_SetBool;
		g_apiTable.Animator_SetFloat           = &Api_Animator_SetFloat;
		g_apiTable.Animator_SetInt             = &Api_Animator_SetInt;
		g_apiTable.Animator_SetTrigger         = &Api_Animator_SetTrigger;
		g_apiTable.Animator_ResetTrigger       = &Api_Animator_ResetTrigger;
		g_apiTable.Animator_GetBool            = &Api_Animator_GetBool;
		g_apiTable.Animator_GetFloat           = &Api_Animator_GetFloat;
		g_apiTable.Animator_GetInt             = &Api_Animator_GetInt;
		g_apiTable.Animator_SetUseLayer        = &Api_Animator_SetUseLayer;
		g_apiTable.Animator_StopAnimation      = &Api_Animator_StopAnimation;

		g_apiTable.Cct_Exists                  = &Api_Cct_Exists;
		g_apiTable.Cct_Move                    = &Api_Cct_Move;
		g_apiTable.Cct_TriggerForcedMove       = &Api_Cct_TriggerForcedMove;
		g_apiTable.Cct_StopForcedMove          = &Api_Cct_StopForcedMove;
		g_apiTable.Cct_IsInForcedMove          = &Api_Cct_IsInForcedMove;
		g_apiTable.Cct_SetAutomaticRotation    = &Api_Cct_SetAutomaticRotation;
		g_apiTable.Cct_SetLookDirection        = &Api_Cct_SetLookDirection;
		g_apiTable.Cct_ClearLookDirection      = &Api_Cct_ClearLookDirection;
		g_apiTable.Cct_ForcedSetPosition       = &Api_Cct_ForcedSetPosition;
		g_apiTable.Cct_GetBaseSpeed            = &Api_Cct_GetBaseSpeed;
		g_apiTable.Cct_SetBaseSpeed            = &Api_Cct_SetBaseSpeed;
		g_apiTable.Cct_IsOnMove                = &Api_Cct_IsOnMove;
		g_apiTable.Cct_SetOnMove               = &Api_Cct_SetOnMove;
		g_apiTable.Cct_IsFalling               = &Api_Cct_IsFalling;
		g_apiTable.Cct_GetRadius               = &Api_Cct_GetRadius;
		g_apiTable.Cct_GetHeight               = &Api_Cct_GetHeight;
		g_apiTable.Cct_GetId                   = &Api_Cct_GetId;

		g_apiTable.Rect_Exists                 = &Api_Rect_Exists;
		g_apiTable.Rect_GetAnchoredPosition    = &Api_Rect_GetAnchoredPosition;
		g_apiTable.Rect_SetAnchoredPosition    = &Api_Rect_SetAnchoredPosition;
		g_apiTable.Rect_GetSizeDelta           = &Api_Rect_GetSizeDelta;
		g_apiTable.Rect_SetSizeDelta           = &Api_Rect_SetSizeDelta;
		g_apiTable.Rect_GetPivot               = &Api_Rect_GetPivot;
		g_apiTable.Rect_SetPivot               = &Api_Rect_SetPivot;

		g_apiTable.Image_Exists                = &Api_Image_Exists;
		g_apiTable.Image_SetTexture            = &Api_Image_SetTexture;
		g_apiTable.Image_GetTextureCount       = &Api_Image_GetTextureCount;
		g_apiTable.Image_GetColor              = &Api_Image_GetColor;
		g_apiTable.Image_SetColor              = &Api_Image_SetColor;
		g_apiTable.Image_GetClipPercent        = &Api_Image_GetClipPercent;
		g_apiTable.Image_SetClipPercent        = &Api_Image_SetClipPercent;
		g_apiTable.Image_SetNativeSize         = &Api_Image_SetNativeSize;

		g_apiTable.Camera_Exists               = &Api_Camera_Exists;
		g_apiTable.Camera_GetScreenSize        = &Api_Camera_GetScreenSize;
		g_apiTable.Camera_WorldToScreenPoint   = &Api_Camera_WorldToScreenPoint;

		g_apiTable.Mesh_Exists                 = &Api_Mesh_Exists;
		g_apiTable.Mesh_InstantiateMaterial    = &Api_Mesh_InstantiateMaterial;
		g_apiTable.Mesh_GetMaterialName        = &Api_Mesh_GetMaterialName;
		g_apiTable.Mesh_SetMaterialFloat       = &Api_Mesh_SetMaterialFloat;
		g_apiTable.Mesh_SetMaterialInt         = &Api_Mesh_SetMaterialInt;
		g_apiTable.Mesh_GetBaseColor           = &Api_Mesh_GetBaseColor;
		g_apiTable.Mesh_SetBaseColor           = &Api_Mesh_SetBaseColor;

		g_apiTable.Input_GetKeyState              = &Api_Input_GetKeyState;
		g_apiTable.Input_GetMouseButtonState      = &Api_Input_GetMouseButtonState;
		g_apiTable.Input_GetControllerButtonState = &Api_Input_GetControllerButtonState;
		g_apiTable.Input_IsAnyKeyPressed          = &Api_Input_IsAnyKeyPressed;
		g_apiTable.Input_GetMousePosition         = &Api_Input_GetMousePosition;
		g_apiTable.Input_GetMouseDelta            = &Api_Input_GetMouseDelta;
		g_apiTable.Input_GetWheelDelta            = &Api_Input_GetWheelDelta;
		g_apiTable.Input_SetCursorVisible         = &Api_Input_SetCursorVisible;
		g_apiTable.Input_IsControllerConnected    = &Api_Input_IsControllerConnected;
		g_apiTable.Input_IsControllerTriggerL     = &Api_Input_IsControllerTriggerL;
		g_apiTable.Input_IsControllerTriggerR     = &Api_Input_IsControllerTriggerR;
		g_apiTable.Input_GetControllerThumbL      = &Api_Input_GetControllerThumbL;
		g_apiTable.Input_GetControllerThumbR      = &Api_Input_GetControllerThumbR;

		g_apiTable.Physics_Raycast                = &Api_Physics_Raycast;
		g_apiTable.Physics_RaycastAll             = &Api_Physics_RaycastAll;
		g_apiTable.Physics_OverlapSphere          = &Api_Physics_OverlapSphere;

		g_apiTable.Rigid_Exists                = &Api_Rigid_Exists;
		g_apiTable.Rigid_GetLinearVelocity     = &Api_Rigid_GetLinearVelocity;
		g_apiTable.Rigid_SetLinearVelocity     = &Api_Rigid_SetLinearVelocity;
		g_apiTable.Rigid_AddLinearVelocity     = &Api_Rigid_AddLinearVelocity;
		g_apiTable.Rigid_GetAngularVelocity    = &Api_Rigid_GetAngularVelocity;
		g_apiTable.Rigid_SetAngularVelocity    = &Api_Rigid_SetAngularVelocity;
		g_apiTable.Rigid_AddForce              = &Api_Rigid_AddForce;
		g_apiTable.Rigid_SetBodyType           = &Api_Rigid_SetBodyType;
		g_apiTable.Rigid_IsKinematic           = &Api_Rigid_IsKinematic;
		g_apiTable.Rigid_SetKinematic          = &Api_Rigid_SetKinematic;
		g_apiTable.Rigid_IsTrigger             = &Api_Rigid_IsTrigger;
		g_apiTable.Rigid_SetIsTrigger          = &Api_Rigid_SetIsTrigger;
		g_apiTable.Rigid_IsColliderEnabled     = &Api_Rigid_IsColliderEnabled;
		g_apiTable.Rigid_SetColliderEnabled    = &Api_Rigid_SetColliderEnabled;
		g_apiTable.Rigid_IsUsingGravity        = &Api_Rigid_IsUsingGravity;
		g_apiTable.Rigid_UseGravity            = &Api_Rigid_UseGravity;
		g_apiTable.Rigid_GetMass               = &Api_Rigid_GetMass;
		g_apiTable.Rigid_SetMass               = &Api_Rigid_SetMass;
		g_apiTable.Rigid_SetLinearDamping      = &Api_Rigid_SetLinearDamping;
		g_apiTable.Rigid_SetAngularDamping     = &Api_Rigid_SetAngularDamping;
		g_apiTable.Rigid_SetScale              = &Api_Rigid_SetScale;
		g_apiTable.Rigid_SetLockLinear         = &Api_Rigid_SetLockLinear;
		g_apiTable.Rigid_SetLockAngular        = &Api_Rigid_SetLockAngular;

		g_apiTable.Collider_Exists             = &Api_Collider_Exists;
		g_apiTable.Collider_GetRadius          = &Api_Collider_GetRadius;
		g_apiTable.Collider_SetRadius          = &Api_Collider_SetRadius;
		g_apiTable.Collider_GetHeight          = &Api_Collider_GetHeight;
		g_apiTable.Collider_SetHeight          = &Api_Collider_SetHeight;
		g_apiTable.Collider_GetExtents         = &Api_Collider_GetExtents;
		g_apiTable.Collider_SetExtents         = &Api_Collider_SetExtents;
		g_apiTable.Collider_GetPositionOffset  = &Api_Collider_GetPositionOffset;
		g_apiTable.Collider_SetPositionOffset  = &Api_Collider_SetPositionOffset;
		g_apiTable.Collider_GetRestitution     = &Api_Collider_GetRestitution;
		g_apiTable.Collider_SetRestitution     = &Api_Collider_SetRestitution;
		g_apiTable.Collider_GetStaticFriction  = &Api_Collider_GetStaticFriction;
		g_apiTable.Collider_SetStaticFriction  = &Api_Collider_SetStaticFriction;
		g_apiTable.Collider_GetDynamicFriction = &Api_Collider_GetDynamicFriction;
		g_apiTable.Collider_SetDynamicFriction = &Api_Collider_SetDynamicFriction;

		g_apiTable.Text_Exists                 = &Api_Text_Exists;
		g_apiTable.Text_GetMessage             = &Api_Text_GetMessage;
		g_apiTable.Text_SetMessage             = &Api_Text_SetMessage;
		g_apiTable.Text_GetColor               = &Api_Text_GetColor;
		g_apiTable.Text_SetColor               = &Api_Text_SetColor;
		g_apiTable.Text_GetAlpha               = &Api_Text_GetAlpha;
		g_apiTable.Text_SetAlpha               = &Api_Text_SetAlpha;
		g_apiTable.Text_GetFontSize            = &Api_Text_GetFontSize;
		g_apiTable.Text_SetFontSize            = &Api_Text_SetFontSize;
		g_apiTable.Text_GetRelativePosition    = &Api_Text_GetRelativePosition;
		g_apiTable.Text_SetRelativePosition    = &Api_Text_SetRelativePosition;

		g_apiTable.Ui_GetOrder                 = &Api_Ui_GetOrder;
		g_apiTable.Ui_SetOrder                 = &Api_Ui_SetOrder;

		g_apiTable.Canvas_Exists               = &Api_Canvas_Exists;
		g_apiTable.Canvas_GetOrder             = &Api_Canvas_GetOrder;
		g_apiTable.Canvas_SetOrder             = &Api_Canvas_SetOrder;
		g_apiTable.Canvas_GetName              = &Api_Canvas_GetName;
		g_apiTable.Canvas_SetName              = &Api_Canvas_SetName;

		g_apiTable.Ui_IsSelected               = &Api_Ui_IsSelected;
		g_apiTable.Ui_IsNavLocked              = &Api_Ui_IsNavLocked;
		g_apiTable.Ui_SetNavLock               = &Api_Ui_SetNavLock;
		g_apiTable.UiNav_GetSelected           = &Api_UiNav_GetSelected;
		g_apiTable.UiNav_SetSelected           = &Api_UiNav_SetSelected;

		g_apiTable.Button_Exists               = &Api_Button_Exists;
		g_apiTable.Button_ConsumeClicked       = &Api_Button_ConsumeClicked;

		g_apiTable.Image_GetTextureIndex       = &Api_Image_GetTextureIndex;
		g_apiTable.Image_GetRotation           = &Api_Image_GetRotation;
		g_apiTable.Image_SetRotation           = &Api_Image_SetRotation;
		g_apiTable.Rect_GetWorldRect           = &Api_Rect_GetWorldRect;
		g_apiTable.Rect_GetScreenPosition      = &Api_Rect_GetScreenPosition;
		g_apiTable.Rect_SetScreenPosition      = &Api_Rect_SetScreenPosition;
	}

	// ── hostfxr ──
	hostfxr_initialize_for_runtime_config_fn g_hostInit{ nullptr };
	hostfxr_get_runtime_delegate_fn          g_hostGetDelegate{ nullptr };
	hostfxr_close_fn                         g_hostClose{ nullptr };

	load_assembly_and_get_function_pointer_fn g_loadAssembly{ nullptr };
}

ClrHost& ClrHost::Get()
{
	static ClrHost instance;
	return instance;
}

bool ClrHost::LoadHostfxr()
{
	wchar_t path[MAX_PATH]{};
	size_t size = MAX_PATH;

	if (0 != get_hostfxr_path(path, &size, nullptr))
	{
		Debug->LogWarning("[CLR] hostfxr를 찾을 수 없습니다. 스크립트 계층이 비활성화됩니다.");
		return false;
	}

	HMODULE module = ::LoadLibraryW(path);
	if (nullptr == module)
	{
		Debug->LogWarning("[CLR] hostfxr 로드 실패");
		return false;
	}

	g_hostInit = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
		::GetProcAddress(module, "hostfxr_initialize_for_runtime_config"));
	g_hostGetDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
		::GetProcAddress(module, "hostfxr_get_runtime_delegate"));
	g_hostClose = reinterpret_cast<hostfxr_close_fn>(
		::GetProcAddress(module, "hostfxr_close"));

	return nullptr != g_hostInit && nullptr != g_hostGetDelegate && nullptr != g_hostClose;
}

bool ClrHost::BindEntryPoints(const file::path& assemblyPath)
{
	const std::wstring assembly = assemblyPath.wstring();
	const wchar_t* typeName = L"CreatorEngine.Bootstrap, ScriptCore";

	auto bindFrom = [&](const wchar_t* owningType, const wchar_t* method, void** out) -> bool
	{
		// UNMANAGEDCALLERSONLY_METHOD를 주면 델리게이트 마샬링 없이
		// 관리 메서드를 직접 가리키는 함수 포인터를 받는다.
		const int rc = g_loadAssembly(assembly.c_str(), owningType, method,
			UNMANAGEDCALLERSONLY_METHOD, nullptr, out);

		if (0 != rc || nullptr == *out)
		{
			char buffer[256]{};
			std::snprintf(buffer, sizeof(buffer), "[CLR] 진입점 바인딩 실패 (rc=0x%X)", rc);
			Debug->LogError(buffer);
			return false;
		}
		return true;
	};

	auto bind = [&](const wchar_t* method, void** out) -> bool
	{
		return bindFrom(typeName, method, out);
	};

	void* fn = nullptr;
	if (!bind(L"Initialize", &fn))       return false;  m_fnInitialize = reinterpret_cast<InitializeFn>(fn);
	if (!bind(L"Shutdown", &fn))         return false;  m_fnShutdown = reinterpret_cast<ShutdownFn>(fn);
	if (!bind(L"FlushRegistrations", &fn)) return false;  m_fnFlushRegistrations = reinterpret_cast<AwakeFn>(fn);
	if (!bind(L"PrePhysicsTick", &fn))   return false;  m_fnPrePhysicsTick = reinterpret_cast<TickFn>(fn);
	if (!bind(L"PostPhysicsTick", &fn))  return false;  m_fnPostPhysicsTick = reinterpret_cast<TickFn>(fn);
	if (!bind(L"OnSceneUnload", &fn))    return false;  m_fnSceneUnload = reinterpret_cast<AwakeFn>(fn);
	if (!bind(L"FlushPhysicsEvents", &fn)) return false;  m_fnFlushPhysicsEvents = reinterpret_cast<FlushPhysicsFn>(fn);
	if (!bind(L"CreateBehaviour", &fn))  return false;  m_fnCreateBehaviour = reinterpret_cast<CreateFn>(fn);

	// 선택 바인딩 — 구 ScriptCore 어셈블리에는 없을 수 있다. 실패해도 계속 간다
	// (해당 기능만 조용히 꺼진다: 목록 UI는 빈 목록, 메시지는 전달되지 않음).
	if (bind(L"GetBehaviourTypeNames", &fn))    m_fnGetBehaviourTypeNames    = reinterpret_cast<TypeNamesFn>(fn);
	if (bind(L"GetAniBehaviourTypeNames", &fn)) m_fnGetAniBehaviourTypeNames = reinterpret_cast<TypeNamesFn>(fn);
	if (bind(L"FlushScriptMessages", &fn))      m_fnFlushScriptMessages      = reinterpret_cast<FlushMessageFn>(fn);

	// 행동 트리(9-8). 트리는 관리 측이 소유하고 네이티브는 인스턴스 id만 든다.
	if (bind(L"CreateBehaviorTree", &fn))  m_fnCreateBehaviorTree  = reinterpret_cast<CreateBTFn>(fn);
	if (bind(L"DestroyBehaviorTree", &fn)) m_fnDestroyBehaviorTree = reinterpret_cast<DestroyBTFn>(fn);
	if (bind(L"FlushAITicks", &fn))        m_fnFlushAITicks        = reinterpret_cast<FlushAITickFn>(fn);
	if (bind(L"GetBTNodeTypeNames", &fn))  m_fnGetBTNodeTypeNames  = reinterpret_cast<BTTypeNamesFn>(fn);
	if (bind(L"HasBTNodeType", &fn))       m_fnHasBTNodeType       = reinterpret_cast<HasBTNodeFn>(fn);
	if (bind(L"GetBTStats", &fn))          m_fnGetBTStats          = reinterpret_cast<BTStatsFn>(fn);
	if (bind(L"ResetBTStats", &fn))        m_fnResetBTStats        = reinterpret_cast<AwakeFn>(fn);

	// 관리 힙 제어·계측(9-6·9-7). Bootstrap이 아니라 GcControl에 있다 —
	// 성격이 스크립트 수명이 아니라 런타임 정책이라 진입점 묶음을 나눴다.
	// 선택 바인딩이라 이것이 없는 어셈블리에서는 GC 연동만 조용히 꺼진다.
	{
		const wchar_t* gcType = L"CreatorEngine.GcControl, ScriptCore";
		if (bindFrom(gcType, L"CollectNow", &fn))     m_fnGcCollectNow     = reinterpret_cast<AwakeFn>(fn);
		if (bindFrom(gcType, L"SetLatencyMode", &fn)) m_fnGcSetLatencyMode = reinterpret_cast<GcLatencyFn>(fn);
		if (bindFrom(gcType, L"GetStats", &fn))       m_fnGcGetStats       = reinterpret_cast<GcStatsFn>(fn);
	}

	// 애니메이션 상태 스크립트
	if (!bind(L"HasAniBehaviour", &fn))     return false;  m_fnHasAniBehaviour     = reinterpret_cast<HasAniFn>(fn);
	if (!bind(L"CreateAniBehaviour", &fn))  return false;  m_fnCreateAniBehaviour  = reinterpret_cast<CreateAniFn>(fn);
	if (!bind(L"DestroyAniBehaviour", &fn)) return false;  m_fnDestroyAniBehaviour = reinterpret_cast<DestroyAniFn>(fn);
	if (!bind(L"FlushAniEvents", &fn))      return false;  m_fnFlushAniEvents      = reinterpret_cast<FlushAniFn>(fn);
	if (!bind(L"DestroyBehaviour", &fn)) return false;  m_fnDestroyBehaviour = reinterpret_cast<DestroyFn>(fn);
	if (!bind(L"DispatchLifecycle", &fn)) return false;  m_fnDispatchLifecycle = reinterpret_cast<LifecycleFn>(fn);

	// 스크립트 어셈블리 로드·핫리로드
	if (!bind(L"LoadScripts", &fn))            return false;  m_fnLoadScripts = reinterpret_cast<LoadScriptsFn>(fn);
	if (!bind(L"ReloadScripts", &fn))          return false;  m_fnReloadScripts = reinterpret_cast<ReloadFn>(fn);
	if (!bind(L"IsPreviousContextAlive", &fn)) return false;  m_fnIsPreviousContextAlive = reinterpret_cast<ReloadFn>(fn);

	// 노출 필드 접근자 (소스 제너레이터 생성물로 연결된다)
	if (!bind(L"GetFieldCount", &fn))  return false;  m_fnGetFieldCount = reinterpret_cast<FieldCountFn>(fn);
	if (!bind(L"GetFieldName", &fn))   return false;  m_fnGetFieldName  = reinterpret_cast<FieldNameFn>(fn);
	if (!bind(L"GetFieldType", &fn))   return false;  m_fnGetFieldType  = reinterpret_cast<FieldTypeFn>(fn);
	if (!bind(L"GetFieldFloat", &fn))  return false;  m_fnGetFieldFloat = reinterpret_cast<GetFloatFn>(fn);
	if (!bind(L"SetFieldFloat", &fn))  return false;  m_fnSetFieldFloat = reinterpret_cast<SetFloatFn>(fn);
	if (!bind(L"GetFieldInt32", &fn))  return false;  m_fnGetFieldInt32 = reinterpret_cast<GetIntFn>(fn);
	if (!bind(L"SetFieldInt32", &fn))  return false;  m_fnSetFieldInt32 = reinterpret_cast<SetIntFn>(fn);
	if (!bind(L"GetFieldBool", &fn))   return false;  m_fnGetFieldBool  = reinterpret_cast<GetIntFn>(fn);
	if (!bind(L"SetFieldBool", &fn))   return false;  m_fnSetFieldBool  = reinterpret_cast<SetIntFn>(fn);

	if (!bind(L"GetFieldString", &fn)) return false;  m_fnGetFieldString = reinterpret_cast<GetStringFn>(fn);
	if (!bind(L"SetFieldString", &fn)) return false;  m_fnSetFieldString = reinterpret_cast<SetStringFn>(fn);
	if (!bind(L"GetFieldFloat2", &fn)) return false;  m_fnGetFieldFloat2 = reinterpret_cast<GetFloat2Fn>(fn);
	if (!bind(L"SetFieldFloat2", &fn)) return false;  m_fnSetFieldFloat2 = reinterpret_cast<SetFloat2Fn>(fn);
	if (!bind(L"GetFieldFloat3", &fn)) return false;  m_fnGetFieldFloat3 = reinterpret_cast<GetFloat3Fn>(fn);
	if (!bind(L"SetFieldFloat3", &fn)) return false;  m_fnSetFieldFloat3 = reinterpret_cast<SetFloat3Fn>(fn);
	if (!bind(L"GetFieldObject", &fn)) return false;  m_fnGetFieldObject = reinterpret_cast<GetObjectFn>(fn);
	if (!bind(L"SetFieldObject", &fn)) return false;  m_fnSetFieldObject = reinterpret_cast<SetObjectFn>(fn);

	return true;
}

bool ClrHost::Initialize()
{
	if (m_ready) return true;

	// 관리 어셈블리 위치는 Host가 EnginePaths로 결정한다. 개발 빌드는 Editor와
	// Player가 구성별 공통 Managed를 쓰고, 패키지는 Player 옆 Managed를 쓴다.
	const file::path baseDir = PathFinder::ManagedPath();

	const file::path assemblyPath = baseDir / L"ScriptCore.dll";
	const file::path configPath   = baseDir / L"ScriptCore.runtimeconfig.json";

	if (!file::exists(assemblyPath) || !file::exists(configPath))
	{
		// 스크립트 없이도 에디터는 떠야 한다. 경고만 남기고 비활성 상태로 둔다.
		Debug->LogWarning("[CLR] ScriptCore.dll을 찾을 수 없어 스크립트 계층을 건너뜁니다: "
			+ assemblyPath.string());
		return false;
	}

	if (!LoadHostfxr()) return false;

	hostfxr_handle context = nullptr;
	int rc = g_hostInit(configPath.c_str(), nullptr, &context);
	// 0 = 성공, 1 = 이미 초기화됨(둘 다 정상)
	if ((0 != rc && 1 != rc) || nullptr == context)
	{
		char buffer[256]{};
		std::snprintf(buffer, sizeof(buffer), "[CLR] 런타임 초기화 실패 (rc=0x%X)", rc);
		Debug->LogError(buffer);
		if (nullptr != context) g_hostClose(context);
		return false;
	}

	void* loader = nullptr;
	rc = g_hostGetDelegate(context, hdt_load_assembly_and_get_function_pointer, &loader);
	g_hostClose(context);

	if (0 != rc || nullptr == loader)
	{
		Debug->LogError("[CLR] 로더 델리게이트 획득 실패");
		return false;
	}
	g_loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loader);

	if (!BindEntryPoints(assemblyPath)) return false;

	FillApiTable();
	const int initResult = m_fnInitialize(&g_apiTable);
	if (0 != initResult)
	{
		char buffer[256]{};
		std::snprintf(buffer, sizeof(buffer),
			"[CLR] 관리 초기화 실패 (result=%d) — API 표 버전이 어긋났을 수 있습니다", initResult);
		Debug->LogError(buffer);
		return false;
	}

	// 게임 스크립트 어셈블리를 언로드 가능한 컨텍스트에 올린다.
	// 없어도 엔진은 동작한다 — 스크립트만 비어 있을 뿐이다.
	const file::path scriptsPath = baseDir / L"Scripts" / L"GameScripts.dll";
	if (file::exists(scriptsPath))
	{
		const std::string utf8 = scriptsPath.string();
		if (0 != m_fnLoadScripts(utf8.c_str()))
		{
			Debug->LogWarning("[CLR] 게임 스크립트 어셈블리 로드 실패");
		}
	}
	else
	{
		Debug->LogWarning("[CLR] GameScripts.dll이 없어 스크립트 없이 시작합니다: " + scriptsPath.string());
	}

	m_ready = true;
	Debug->Log("[CLR] CoreCLR 스크립트 계층 준비 완료");
	return true;
}

bool ClrHost::ReloadScripts()
{
	if (!m_ready || nullptr == m_fnReloadScripts) return false;
	return 0 == m_fnReloadScripts();
}

bool ClrHost::IsPreviousContextAlive()
{
	if (!m_ready || nullptr == m_fnIsPreviousContextAlive) return false;
	return 0 != m_fnIsPreviousContextAlive();
}

void ClrHost::Shutdown()
{
	if (!m_ready) return;

	if (nullptr != m_fnShutdown) m_fnShutdown();

	m_ready = false;
	ScriptObjectRegistry::Get().Clear();

	// hostfxr는 언로드하지 않는다. CoreCLR은 프로세스당 한 번만 올릴 수 있고,
	// 종료 직전이라 정리 이득도 없다.
}

bool ClrHost::HasAniBehaviour(std::string_view typeName)
{
	if (!m_ready || nullptr == m_fnHasAniBehaviour) return false;

	const std::string name(typeName);
	return 0 != m_fnHasAniBehaviour(name.c_str());
}

int ClrHost::CreateAniBehaviour(std::string_view typeName)
{
	if (!m_ready || nullptr == m_fnCreateAniBehaviour) return -1;

	const std::string name(typeName);
	return m_fnCreateAniBehaviour(name.c_str());
}

void ClrHost::DestroyAniBehaviour(int instanceId)
{
	if (m_ready && nullptr != m_fnDestroyAniBehaviour && instanceId >= 0)
	{
		m_fnDestroyAniBehaviour(instanceId);
	}
}

void ClrHost::QueueAniEvent(int instanceId, AniEventKind kind, float deltaTime, Entity* owner)
{
	if (!m_ready || instanceId < 0) return;

	ScriptAniEvent event{};
	event.instanceId = instanceId;
	event.kind = static_cast<int>(kind);
	event.deltaTime = deltaTime;

	// 소유 오브젝트는 이벤트마다 실어 보낸다. 상태가 만들어질 때는 아직 컨트롤러가
	// 연결되지 않아 생성 시점에 정할 수 없다.
	event.owner = (nullptr != owner)
		? ScriptObjectRegistry::Get().Register(owner)
		: ScriptObjectHandle{};

	m_aniEvents.push_back(event);
}

void ClrHost::FlushAniEvents()
{
	if (!m_ready || nullptr == m_fnFlushAniEvents) { m_aniEvents.clear(); return; }
	if (m_aniEvents.empty()) return;

	// 전달 도중 새 이벤트가 쌓일 수 있으므로 비운 뒤 넘긴다.
	std::vector<ScriptAniEvent> batch;
	batch.swap(m_aniEvents);

	m_fnFlushAniEvents(batch.data(), static_cast<int>(batch.size()));
}

void ClrHost::QueuePhysicsEvent(int instanceId, PhysicsEventKind kind,
	Entity* other, const std::vector<Mathf::Vector3>& contactPoints)
{
	if (!m_ready || instanceId < 0) return;

	ScriptPhysicsEvent event{};
	event.instanceId = instanceId;
	event.kind = static_cast<int>(kind);

	// 상대는 핸들로만 넘긴다. 여기서 등록해 두면 관리 측이 세대 검사로
	// 이미 파괴된 상대를 걸러낼 수 있다.
	event.collision.other = (nullptr != other)
		? ScriptObjectRegistry::Get().Register(other)
		: ScriptObjectHandle{};

	event.collision.contactCount = static_cast<int>(contactPoints.size());
	if (!contactPoints.empty())
	{
		// 접점 배열 전체를 복사하지 않는다 — 대부분 대표 접점 하나면 충분하고,
		// 전부 필요해지면 별도 조회 API를 두는 편이 경계를 얇게 유지한다.
		const auto& first = contactPoints.front();
		event.collision.contact = { first.x, first.y, first.z };
	}

	m_physicsEvents.push_back(event);
}

void ClrHost::FlushPhysicsEvents()
{
	if (!m_ready || nullptr == m_fnFlushPhysicsEvents) { m_physicsEvents.clear(); return; }
	if (m_physicsEvents.empty()) return;

	// 배열 포인터 하나만 넘긴다. 충돌이 몇 건이든 경계 통과는 한 번이다.
	m_fnFlushPhysicsEvents(m_physicsEvents.data(), static_cast<int>(m_physicsEvents.size()));

	// 용량은 유지해 매 프레임 재할당이 일어나지 않게 한다.
	m_physicsEvents.clear();
}

// 물리·GameLogic 뒤, 이벤트 플러시 앞의 등록 반영 지점.
// 옛 이름은 TickAwake였으나 Awake 훅은 L3에서 사라졌다 — 여기서 남은 일은
// Scene::Update/LateUpdate가 만들거나 없앤 스크립트를 관리 측 _active에 반영하는
// 것뿐이고, 뒤따르는 물리·애니·메시지·BT 플러시가 그 최신 목록으로 배달된다.
void ClrHost::FlushRegistrations()
{
	if (m_ready && nullptr != m_fnFlushRegistrations) m_lastActiveCount = m_fnFlushRegistrations();
}

void ClrHost::TickPrePhysics(float deltaTime)
{
	if (m_ready && nullptr != m_fnPrePhysicsTick) m_lastActiveCount = m_fnPrePhysicsTick(deltaTime);
}

void ClrHost::TickPostPhysics(float deltaTime)
{
	if (m_ready && nullptr != m_fnPostPhysicsTick) m_fnPostPhysicsTick(deltaTime);
}

void ClrHost::NotifySceneUnload()
{
	if (m_ready && nullptr != m_fnSceneUnload) m_fnSceneUnload();

	ScriptObjectRegistry::Get().Clear();
}

void ClrHost::CollectManagedHeap()
{
	// 씬 경계 전용이다. 프레임 루프에서 부르면 그 프레임이 통째로 GC에 묶인다 —
	// CoreCLR의 블로킹 수집은 관리 스레드를 전부 세운다.
	if (!m_ready || nullptr == m_fnGcCollectNow) return;

	m_fnGcCollectNow();
}

void ClrHost::SetManagedLatencyMode(bool lowLatency)
{
	if (!m_ready || nullptr == m_fnGcSetLatencyMode) return;

	m_fnGcSetLatencyMode(lowLatency ? 1 : 0);
}

bool ClrHost::GetManagedGcStats(ScriptGcStats& outStats)
{
	if (!m_ready || nullptr == m_fnGcGetStats) return false;

	return 0 == m_fnGcGetStats(&outStats);
}

int ClrHost::CreateBehaviorTree(Entity* owner, const ScriptBTNodeDesc* nodes, int count,
	const ScriptBBEntry* entries, int entryCount)
{
	if (!m_ready || nullptr == m_fnCreateBehaviorTree) return -1;
	if (nullptr == owner || nullptr == nodes || count <= 0) return -1;

	// 그래프와 블랙보드가 배열 둘로 한 번에 건너간다 —
	// 노드·항목 수와 무관하게 크로싱은 1회다.
	const ScriptObjectHandle handle = ScriptObjectRegistry::Get().Register(owner);
	return m_fnCreateBehaviorTree(handle, nodes, count, entries, entryCount);
}

void ClrHost::QueueAITick(int instanceId, float deltaTime)
{
	if (!m_ready || instanceId < 0) return;

	// AI 갱신이 잡 스레드에서 도므로 담는 쪽을 보호한다(헤더 주석 참고).
	SpinLock lock(m_aiTickFlag);
	m_aiTicks.push_back(ScriptAITick{ instanceId, deltaTime });
}

void ClrHost::FlushAITicks()
{
	// 이 함수가 프레임당 한 번 불린다는 것이 '크로싱 1회'의 근거다. 호출 수를 먼저
	// 센다 — 아래 조기 반환 뒤에 두면 큐가 빈 프레임이 분모에서 빠져 비율이 부풀어
	// 오른다(항상 1.00이 나와 검사가 아무것도 말하지 않게 된다).
	++m_aiCrossings.flushCalls;

	// 전달 도중 새 틱이 쌓일 수 있으므로 비운 뒤 넘긴다(ScriptMessage와 같은 규약).
	// 스왑 구간만 잠근다 — 관리 측 호출까지 잠그면 잡 스레드가 그동안 막힌다.
	std::vector<ScriptAITick> batch;
	{
		SpinLock lock(m_aiTickFlag);
		batch.swap(m_aiTicks);
	}

	if (batch.empty()) return;
	if (!m_ready || nullptr == m_fnFlushAITicks) return;

	// 트리가 몇 개든 경계 통과는 한 번이다. 트리 안의 노드 순회는 전부 관리 측에서 끝난다.
	const uint64_t delivered = batch.size();
	++m_aiCrossings.crossings;
	m_aiCrossings.ticksDelivered += delivered;
	if (delivered > m_aiCrossings.maxBatch) m_aiCrossings.maxBatch = delivered;

	m_fnFlushAITicks(batch.data(), static_cast<int>(batch.size()));
}

bool ClrHost::GetBehaviorTreeStats(ScriptBTStats& outStats)
{
	if (!m_ready || nullptr == m_fnGetBTStats) return false;

	return 0 == m_fnGetBTStats(&outStats);
}

void ClrHost::ResetBehaviorTreeStats()
{
	if (!m_ready || nullptr == m_fnResetBTStats) return;

	m_fnResetBTStats();
}

bool ClrHost::DestroyBehaviorTree(int instanceId)
{
	if (!m_ready || nullptr == m_fnDestroyBehaviorTree) return false;
	return 0 == m_fnDestroyBehaviorTree(instanceId);
}

int ClrHost::CreateBehaviour(Entity* owner, std::string_view typeName)
{
	if (!m_ready || nullptr == m_fnCreateBehaviour || nullptr == owner) return -1;

	const ScriptObjectHandle handle = ScriptObjectRegistry::Get().Register(owner);
	const std::string name(typeName);
	return m_fnCreateBehaviour(handle, name.c_str());
}

bool ClrHost::DestroyBehaviour(int instanceId)
{
	if (!m_ready || nullptr == m_fnDestroyBehaviour) return false;
	return 0 == m_fnDestroyBehaviour(instanceId);
}

bool ClrHost::DispatchLifecycle(int instanceId, ScriptLifecyclePhase phase)
{
	if (!m_ready || nullptr == m_fnDispatchLifecycle) return false;
	if (instanceId < 0) return false;

	// 희귀 이벤트 크로싱이다 — 틱 경로가 아니므로 "틱당 1회 크로싱" 계약과
	// 무저촉이다(트랙 L2의 판정 승계).
	return 0 == m_fnDispatchLifecycle(instanceId, static_cast<int>(phase));
}

namespace
{
	// 관리 측은 타입 이름을 '\n'으로 이어 한 번에 준다 — 이름마다 경계를 넘지 않기 위해서다.
	std::vector<std::string> ReadTypeNameList(int(__stdcall* fn)(char*, int))
	{
		if (nullptr == fn) return {};

		// 스크립트 수백 개 규모까지 여유 있는 크기.
		std::vector<char> buffer(16 * 1024, '\0');
		const int length = fn(buffer.data(), static_cast<int>(buffer.size()));
		if (length <= 0) return {};

		std::vector<std::string> names;
		const std::string joined(buffer.data(), static_cast<size_t>(length));
		size_t begin = 0;
		while (begin < joined.size())
		{
			size_t end = joined.find('\n', begin);
			if (end == std::string::npos) end = joined.size();
			if (end > begin) names.emplace_back(joined.substr(begin, end - begin));
			begin = end + 1;
		}
		return names;
	}
}

std::vector<std::string> ClrHost::GetBehaviourTypeNames()
{
	return m_ready ? ReadTypeNameList(m_fnGetBehaviourTypeNames) : std::vector<std::string>{};
}

bool ClrHost::HasBTNodeType(std::string_view typeName)
{
	if (!m_ready || nullptr == m_fnHasBTNodeType || typeName.empty()) return false;

	const std::string name(typeName);
	return 0 != m_fnHasBTNodeType(name.c_str());
}

std::vector<std::string> ClrHost::GetBTNodeTypeNames(BTNodeKind kind)
{
	if (!m_ready || nullptr == m_fnGetBTNodeTypeNames) return {};

	// 이름을 개행으로 이어 한 번에 받는다(위 ReadTypeNameList와 같은 규약).
	// 갈래 인자가 하나 더 붙어 그 헬퍼를 그대로 쓰지 못하므로 여기서 푼다.
	std::vector<char> buffer(16 * 1024, '\0');
	const int length = m_fnGetBTNodeTypeNames(
		static_cast<int>(kind), buffer.data(), static_cast<int>(buffer.size()));
	if (length <= 0) return {};

	std::vector<std::string> names;
	const std::string joined(buffer.data(), static_cast<size_t>(length));
	size_t begin = 0;
	while (begin < joined.size())
	{
		size_t end = joined.find('\n', begin);
		if (end == std::string::npos) end = joined.size();
		if (end > begin) names.emplace_back(joined.substr(begin, end - begin));
		begin = end + 1;
	}
	return names;
}

std::vector<std::string> ClrHost::GetAniBehaviourTypeNames()
{
	return m_ready ? ReadTypeNameList(m_fnGetAniBehaviourTypeNames) : std::vector<std::string>{};
}

void ClrHost::QueueScriptMessage(int instanceId, std::string_view methodName)
{
	if (!m_ready || instanceId < 0 || methodName.empty()) return;

	// 이름이 버퍼를 넘으면 잘라 보내는 대신 버린다 — 잘린 이름은 관리 측에서
	// 엉뚱한 메서드를 부르거나(접두사 충돌) 조용히 실패한다. 둘 다 추적하기 어렵다.
	if (methodName.size() >= kScriptMessageNameCapacity)
	{
		Debug->LogWarning("[스크립트] 콜백 이름이 너무 깁니다: " + std::string(methodName));
		return;
	}

	ScriptMessage message{};
	message.instanceId = instanceId;
	std::memcpy(message.name, methodName.data(), methodName.size());
	message.name[methodName.size()] = '\0';

	// 애니메이션 잡이 스레드 풀에서 키프레임 이벤트를 쏟아내므로 담는 쪽은 보호한다.
	SpinLock lock(m_scriptMessageFlag);
	m_scriptMessages.push_back(message);
}

void ClrHost::FlushScriptMessages()
{
	// 전달 도중 새 메시지가 쌓일 수 있으므로 비운 뒤 넘긴다(AniEvent와 같은 규약).
	// 스왑 구간만 잠근다 — 관리 측 호출까지 잠그면 잡 스레드가 그동안 막힌다.
	std::vector<ScriptMessage> batch;
	{
		SpinLock lock(m_scriptMessageFlag);
		batch.swap(m_scriptMessages);
	}

	if (batch.empty()) return;
	if (!m_ready || nullptr == m_fnFlushScriptMessages) return;

	m_fnFlushScriptMessages(batch.data(), static_cast<int>(batch.size()));
}

int ClrHost::GetFieldCount(int instanceId)
{
	return (m_ready && nullptr != m_fnGetFieldCount) ? m_fnGetFieldCount(instanceId) : 0;
}

std::string ClrHost::GetFieldName(int instanceId, int index)
{
	if (!m_ready || nullptr == m_fnGetFieldName) return {};

	char buffer[128]{};
	const int length = m_fnGetFieldName(instanceId, index, buffer, static_cast<int>(sizeof(buffer)));
	return (length > 0) ? std::string(buffer, static_cast<size_t>(length)) : std::string{};
}

ClrHost::ScriptFieldType ClrHost::GetFieldType(int instanceId, int index)
{
	if (!m_ready || nullptr == m_fnGetFieldType) return ScriptFieldType::Unknown;
	return static_cast<ScriptFieldType>(m_fnGetFieldType(instanceId, index));
}

float ClrHost::GetFieldFloat(int instanceId, int index)
{
	return (m_ready && nullptr != m_fnGetFieldFloat) ? m_fnGetFieldFloat(instanceId, index) : 0.f;
}

void ClrHost::SetFieldFloat(int instanceId, int index, float value)
{
	if (m_ready && nullptr != m_fnSetFieldFloat) m_fnSetFieldFloat(instanceId, index, value);
}

int ClrHost::GetFieldInt32(int instanceId, int index)
{
	return (m_ready && nullptr != m_fnGetFieldInt32) ? m_fnGetFieldInt32(instanceId, index) : 0;
}

void ClrHost::SetFieldInt32(int instanceId, int index, int value)
{
	if (m_ready && nullptr != m_fnSetFieldInt32) m_fnSetFieldInt32(instanceId, index, value);
}

bool ClrHost::GetFieldBool(int instanceId, int index)
{
	return (m_ready && nullptr != m_fnGetFieldBool) ? (0 != m_fnGetFieldBool(instanceId, index)) : false;
}

void ClrHost::SetFieldBool(int instanceId, int index, bool value)
{
	if (m_ready && nullptr != m_fnSetFieldBool) m_fnSetFieldBool(instanceId, index, value ? 1 : 0);
}

std::string ClrHost::GetFieldString(int instanceId, int index)
{
	if (!m_ready || nullptr == m_fnGetFieldString) return {};

	char buffer[1024]{};
	const int length = m_fnGetFieldString(instanceId, index, buffer, static_cast<int>(sizeof(buffer)));
	return (length > 0) ? std::string(buffer, static_cast<size_t>(length)) : std::string{};
}

void ClrHost::SetFieldString(int instanceId, int index, const std::string& value)
{
	if (m_ready && nullptr != m_fnSetFieldString) m_fnSetFieldString(instanceId, index, value.c_str());
}

ClrHost::ScriptFloat2 ClrHost::GetFieldFloat2(int instanceId, int index)
{
	return (m_ready && nullptr != m_fnGetFieldFloat2) ? m_fnGetFieldFloat2(instanceId, index) : ScriptFloat2{};
}

void ClrHost::SetFieldFloat2(int instanceId, int index, ScriptFloat2 value)
{
	if (m_ready && nullptr != m_fnSetFieldFloat2) m_fnSetFieldFloat2(instanceId, index, value);
}

ClrHost::ScriptFloat3 ClrHost::GetFieldFloat3(int instanceId, int index)
{
	return (m_ready && nullptr != m_fnGetFieldFloat3) ? m_fnGetFieldFloat3(instanceId, index) : ScriptFloat3{};
}

void ClrHost::SetFieldFloat3(int instanceId, int index, ScriptFloat3 value)
{
	if (m_ready && nullptr != m_fnSetFieldFloat3) m_fnSetFieldFloat3(instanceId, index, value);
}

Entity* ClrHost::GetFieldObject(int instanceId, int index)
{
	if (!m_ready || nullptr == m_fnGetFieldObject) return nullptr;

	const ScriptObjectHandle handle = m_fnGetFieldObject(instanceId, index);
	return ScriptObjectRegistry::Get().Resolve(handle);
}

void ClrHost::SetFieldObject(int instanceId, int index, Entity* object)
{
	if (!m_ready || nullptr == m_fnSetFieldObject) return;

	// null을 넣으면 무효 핸들이 되어 관리 측에서 IsAlive == false로 보인다.
	const ScriptObjectHandle handle = (nullptr != object)
		? ScriptObjectRegistry::Get().Register(object)
		: ScriptObjectHandle{};

	m_fnSetFieldObject(instanceId, index, handle);
}

















