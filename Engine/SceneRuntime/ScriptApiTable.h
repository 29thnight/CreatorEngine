#pragma once

#include <cstddef>
#include <cstdint>
#include "../Utility_Framework/Runtime/ScriptApiVersion.h"
#include "ScriptBindingValues.h"
#include "ScriptPhysicsValues.h"
#include "SimulationExchangeABI.h"

namespace ce::script::binding
{
	using Float3 = ce::script::vector3;

	// 쿼터니언 전용. math::vector4와 배치가 같아 그대로 오간다.
	struct Float4 { float x, y, z, w; };

	// UI 좌표·화면 크기용. math::vector2와 배치가 같다.
	struct Float2 { float x, y; };

    struct AudioAssetId
    {
        std::uint64_t first;
        std::uint64_t second;
    };
    struct AudioPlaySettings
    {
        float volume;
        float pitch;
        int priority;
        int loop;
        int bus;
        float spatialBlend;
        float minimumDistance;
        float maximumDistance;
        int rolloff;
        int useReverbSend;
        float reverbSendDecibels;
        int ownerPolicy;
        std::uint32_t concurrencyGroup;
        int preemptSameClip;
        int allowVirtualization;
    };
    struct AudioParameter
    {
        const char* name;
        int type;
        int integer;
        float number;
        const char* text;
    };
    static_assert(sizeof(AudioParameter) == 32u);
    struct AudioPlaybackCompletion
    {
        std::uint64_t playback;
        std::uint64_t scope;
        std::uint64_t ownerId;
        int reason;
    };
    static_assert(sizeof(AudioAssetId) == 16u);
    static_assert(sizeof(AudioPlaySettings) == 60u);

	struct ScriptApiTable
	{
		int version;
		int structSize;

		void (__stdcall* PrintLog)(int level, const char* message,
			const char* file, int line, const char* member);

		ScriptObjectHandle (__stdcall* Entity_FindByName)(const char* name);
		int  (__stdcall* Entity_IsAlive)(ScriptObjectHandle handle);
		int  (__stdcall* Entity_GetName)(ScriptObjectHandle handle, char* buffer, int capacity);
		void (__stdcall* Entity_SetEnabled)(ScriptObjectHandle handle, int enabled);

		// 스크립트 컴포넌트 하나의 활성 상태. 오브젝트 전체(Entity_SetEnabled)와 달리
		// 이 스크립트만 켜고 끈다 — 관리 측 Component.Enabled의 setter가 이것을 부르고,
		// OnEnable/OnDisable 훅은 그 결과로 네이티브가 되돌려 준다(드라이버 단일화).
		// owner를 함께 받는 이유: instanceId만으로는 어느 Entity의 컴포넌트인지 알 수
		// 없고, 역맵을 두면 그 맵이 dangling의 새 원천이 된다.
		int  (__stdcall* Script_SetEnabled)(ScriptObjectHandle owner, int instanceId, int enabled);

		// 계층 접근. 실측에서 m_childrenIndices 76 · Entity::FindIndex 100 ·
		// m_parentIndex 32로, 남은 어떤 컴포넌트 래퍼보다 큰 표면이다.
		int  (__stdcall* Entity_GetChildCount)(ScriptObjectHandle handle);
		ScriptObjectHandle (__stdcall* Entity_GetChild)(ScriptObjectHandle handle, int index);
		ScriptObjectHandle (__stdcall* Entity_GetParent)(ScriptObjectHandle handle);
		ScriptObjectHandle (__stdcall* Entity_FindByIndex)(int index);
		int  (__stdcall* Entity_GetIndex)(ScriptObjectHandle handle);

		// ★ S3부터 Transform은 없을 수 있다 — UI/Canvas는 RectTransformComponent만 갖는다.
		//
		// 이 하나가 없으면 관리 측은 부재를 표현할 방법이 아예 없다. 그러면 C#이
		// Transform을 잡아 값을 쓰는 순간 Entity::Transform_()가 공유 더미로 떨어지고
		// (Entity.cpp MissingTransformFallback) 오브젝트 이름당 로그 한 줄만 남긴 채
		// 값이 사라진다 — 크래시도 예외도 없어 스크립트 쪽에서는 성공과 구분되지 않는다.
		// 다른 컴포넌트의 *_Exists와 같은 역할이고, 그것들과 달리 늦게 생긴 이유는
		// 승격(S1-b) 전에는 Transform이 GameObject의 값 멤버라 부재가 없었기 때문이다.
		int    (__stdcall* Transform_Exists)(ScriptObjectHandle handle);

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

		// 카메라 — 주 카메라 기준 질의. 월드→스크린 변환을 손으로 하는 파일이
		// 11개나 되어 한 번에 접는다. 이 셋은 핸들을 받지 않는다(전역 접근점).
		int    (__stdcall* Camera_PrimaryExists)();
		Float2 (__stdcall* Camera_GetScreenSize)();
		Float3 (__stdcall* Camera_WorldToScreenPoint)(Float3 world);

		// CameraComponent — 오브젝트에 붙은 카메라 하나를 가리킨다(저작 20건).
		//
		// LightComponent와 달리 dirty를 발행하지 않는다. 카메라는 렌더 프록시를
		// 쓰지 않고(Scene.cpp의 Kind 열거에 Camera가 없다) 매 프레임
		// CaptureFrameSnapshot으로 읽히므로, 값을 바꾸면 다음 프레임에 그대로 반영된다.
		//
		// 위치·방향은 여기 없다 — CameraComponent::ResolveCamera가 Transform에서
		// 매번 유도한다. 카메라를 움직이려면 Transform을 옮기는 것이 정본이다.
		int    (__stdcall* Camera_Exists)(ScriptObjectHandle handle);
		float  (__stdcall* Camera_GetFov)(ScriptObjectHandle handle);
		void   (__stdcall* Camera_SetFov)(ScriptObjectHandle handle, float degrees);
		float  (__stdcall* Camera_GetNearPlane)(ScriptObjectHandle handle);
		void   (__stdcall* Camera_SetNearPlane)(ScriptObjectHandle handle, float value);
		float  (__stdcall* Camera_GetFarPlane)(ScriptObjectHandle handle);
		void   (__stdcall* Camera_SetFarPlane)(ScriptObjectHandle handle, float value);
		int    (__stdcall* Camera_IsPrimary)(ScriptObjectHandle handle);
		void   (__stdcall* Camera_SetPrimary)(ScriptObjectHandle handle, int primary);
		ScriptObjectHandle (__stdcall* Camera_GetPrimaryHandle)();

		// LightComponent. 저작 자산에 30개가 있는데 스크립트가 만질 길이 없었다.
		//
		// setter는 전부 LightComponent의 writer를 거친다 — 필드에 직접 대입하면
		// dirty가 서지 않아 LightRenderProxy가 낡은 채 남는다(LightComponent.h 주석).
		int    (__stdcall* Light_Exists)(ScriptObjectHandle handle);
		Float4 (__stdcall* Light_GetColor)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetColor)(ScriptObjectHandle handle, Float4 color);
		float  (__stdcall* Light_GetIntensity)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetIntensity)(ScriptObjectHandle handle, float intensity);
		float  (__stdcall* Light_GetRange)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetRange)(ScriptObjectHandle handle, float range);
		float  (__stdcall* Light_GetSpotAngle)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetSpotAngle)(ScriptObjectHandle handle, float degrees);
		int    (__stdcall* Light_GetLightType)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetLightType)(ScriptObjectHandle handle, int type);
		int    (__stdcall* Light_GetLightStatus)(ScriptObjectHandle handle);
		void   (__stdcall* Light_SetLightStatus)(ScriptObjectHandle handle, int status);

		// MeshRenderer + Material. 스크립트가 만지는 것은 사실상 셋뿐이다 —
		// 재질 사본 만들기(6회) · 셰이더 상수 넣기(24회) · 베이스 색 알파.
		int   (__stdcall* Mesh_Exists)(ScriptObjectHandle handle);
		void  (__stdcall* Mesh_InstantiateMaterial)(ScriptObjectHandle handle, const char* newName);
		int   (__stdcall* Mesh_GetMaterialName)(ScriptObjectHandle handle, char* buffer, int capacity);
		int   (__stdcall* Mesh_SetMaterialFloat)(ScriptObjectHandle handle, const char* name, float value);
		int   (__stdcall* Mesh_SetMaterialInt)(ScriptObjectHandle handle, const char* name, int value);
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

        std::uint64_t (__stdcall* Body_Find)(ScriptObjectHandle owner);
        std::uint64_t (__stdcall* Character_Find)(ScriptObjectHandle owner);
        int (__stdcall* Character_Read)(ScriptObjectHandle owner, std::uint64_t instance, ce::script::physics_character_state* state);
        int (__stdcall* Character_Velocity)(ScriptObjectHandle owner, std::uint64_t instance, Float3 velocity);
        int (__stdcall* Character_Teleport)(ScriptObjectHandle owner, std::uint64_t instance, Float3 position);
        int (__stdcall* Character_Jump)(ScriptObjectHandle owner, std::uint64_t instance);
        int (__stdcall* Character_Force)(ScriptObjectHandle owner, std::uint64_t instance, Float3 velocity, double seconds);
        int (__stdcall* Character_CancelForce)(ScriptObjectHandle owner, std::uint64_t instance);
        int (__stdcall* Body_Read)(ScriptObjectHandle owner, std::uint64_t instance, ce::script::physics_body_state* state);
        int (__stdcall* Body_Velocity)(ScriptObjectHandle owner, std::uint64_t instance, Float3 linear, Float3 angular);
        int (__stdcall* Body_Force)(ScriptObjectHandle owner, std::uint64_t instance, Float3 linear, Float3 angular, int mode);
        int (__stdcall* Body_ShapeCount)(ScriptObjectHandle owner, std::uint64_t instance, int* count);
        int (__stdcall* Body_ShapeRead)(ScriptObjectHandle owner, std::uint64_t instance, int index, ce::script::physics_shape_state* shape);
        int (__stdcall* Body_ShapeFlags)(ScriptObjectHandle owner, std::uint64_t instance, unsigned int shape, int sensor, int queryEnabled);
        int (__stdcall* Physics_Query)(ScriptObjectHandle sceneAnchor, int kind, Float3 origin, Float3 direction,
            float distanceOrRadius, unsigned int layers, int includeSensors, ce::script::physics_hit* hits,
            int capacity, ce::script::physics_query_result* result);

        int (__stdcall* Physics_QueryBatch)(ScriptObjectHandle anchor, const ce::script::physics_query_request* requests,
            int count, ce::script::physics_hit* hits, int capacity, ce::script::physics_batch_result* results);

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

		std::uint32_t (__stdcall* Profiler_Register)(const char* name);
		void (__stdcall* Profiler_Begin)(std::uint32_t marker);
		void (__stdcall* Profiler_End)();
        std::uint64_t (__stdcall* Audio_GetScope)(int kind);
        std::uint64_t (__stdcall* Audio_Play)(std::uint64_t scope, AudioAssetId asset, int sourceKind, int mode, Float3 position, ScriptObjectHandle owner, const AudioPlaySettings* settings, const AudioParameter* parameters, int parameterCount);
        void (__stdcall* Audio_Control)(std::uint64_t playback, int operation, float gain, float pitch);
        int (__stdcall* Audio_State)(std::uint64_t playback);
        void (__stdcall* Audio_SetTransform)(std::uint64_t playback, Float3 position, Float3 velocity);
        int (__stdcall* Audio_SetParameter)(std::uint64_t playback, const char* name, int type, int integer, float number, const char* text);
        int (__stdcall* Audio_TakeCompletion)(AudioPlaybackCompletion* completion);
        std::uint64_t (__stdcall* Sound_PlayInstance)(ScriptObjectHandle owner, int oneShot);
        void (__stdcall* Sound_GetSettings)(ScriptObjectHandle owner, AudioPlaySettings* settings);
        void (__stdcall* Sound_SetSettings)(ScriptObjectHandle owner, const AudioPlaySettings* settings);
        int (__stdcall* Sound_GetSource)(ScriptObjectHandle owner, AudioAssetId* asset);
        void (__stdcall* Sound_SetSource)(ScriptObjectHandle owner, AudioAssetId asset, int kind);
        int (__stdcall* AudioListener_Exists)(ScriptObjectHandle owner);
        int (__stdcall* AudioListener_GetActive)(ScriptObjectHandle owner);
        void (__stdcall* AudioListener_SetActive)(ScriptObjectHandle owner, int active);
        void (__stdcall* Audio_SetBusVolume)(int bus, float gain);
        int (__stdcall* Audio_LastError)(char* buffer, int capacity);
        int (__stdcall* Audio_Configure)(std::uint32_t id, int cap, int policy, int group);
        int (__stdcall* Audio_SetReverbPreset)(int preset);
        // Append-only v34 AssetDepot ABI. All ownership stays in this module.
        int (__stdcall* Asset_Request)(const ScriptAssetLink* link, const ScriptTextureAssetVariant* variant, int residentOnly, ScriptAssetToken* token);
        int (__stdcall* Asset_Snapshot)(ScriptAssetToken token, ScriptAssetRequestSnapshot* snapshot, char* message, int capacity);
        int (__stdcall* Asset_AcquireResult)(ScriptAssetToken request, ScriptAssetToken* owner);
        int (__stdcall* Asset_Cancel)(ScriptAssetToken request);
        int (__stdcall* Asset_Release)(ScriptAssetToken token);
        int (__stdcall* Asset_ReadTexture)(ScriptAssetToken token, ScriptTextureDescriptor* descriptor);
        int (__stdcall* Asset_ListRoots)(std::uint64_t mount, std::uint32_t kind, ScriptAssetLink* links, int capacity, int* count);
        // Append-only v35: concrete-view selection never changes serialized links.
        int (__stdcall* Asset_RequestTyped)(const ScriptAssetLink* link, std::uint32_t concreteType, const ScriptTextureAssetVariant* variant, ScriptAssetToken* token);
        int (__stdcall* Asset_TryAcquireTyped)(const ScriptAssetLink* link, std::uint32_t concreteType, const ScriptTextureAssetVariant* variant, ScriptAssetToken* token);
        // Preserve the v40 physics entries before appending new slots.
        int (__stdcall* Body_Remove)(ScriptObjectHandle owner, std::uint64_t instance);
        int (__stdcall* Body_ShapeRole)(ScriptObjectHandle owner, std::uint64_t instance, unsigned int shape, const char* role);
        // Append-only v41: explicit same-camera discontinuities.
        void (__stdcall* Camera_NotifyCameraCut)(ScriptObjectHandle handle);
        int (__stdcall* Simulation_Status)(Runtime::TickStatus* status);
        int (__stdcall* Simulation_Submit)(std::uint64_t epoch, std::uint64_t tick, std::uint32_t kind, const std::byte* payload, int bytes, std::uint64_t* sequence);
        int (__stdcall* Simulation_Read)(std::uint64_t epoch, std::uint64_t after, Runtime::TickSnapshotHeader* header, std::byte* payload, int capacity);
        int (__stdcall* ExtraComponent_Exists)(ScriptObjectHandle handle, int kind);
        int (__stdcall* ExtraComponent_GetEnabled)(ScriptObjectHandle handle, int kind);
        void (__stdcall* ExtraComponent_SetEnabled)(ScriptObjectHandle handle, int kind, int enabled);
        int (__stdcall* Bone_GetPinned)(ScriptObjectHandle handle);
        void (__stdcall* Bone_SetPinned)(ScriptObjectHandle handle, int pinned);
        int (__stdcall* Bone_GetResolvedIndex)(ScriptObjectHandle handle);
        int (__stdcall* Decal_GetInt)(ScriptObjectHandle handle, int field);
        void (__stdcall* Decal_SetInt)(ScriptObjectHandle handle, int field, int value);
        float (__stdcall* Decal_GetFloat)(ScriptObjectHandle handle, int field);
        void (__stdcall* Decal_SetFloat)(ScriptObjectHandle handle, int field, float value);

        // Append-only v48. Execute exactly once, read a retained UTF-8 response,
        // then release it. The generic protocol walks live reflgen descriptors.
        std::uint64_t (__stdcall* NativeReflection_Execute)(ScriptObjectHandle owner, const char* request, int bytes);
        int (__stdcall* NativeReflection_Read)(std::uint64_t ticket, char* output, int capacity);
        void (__stdcall* NativeReflection_Release)(std::uint64_t ticket);

	};

    using ScriptApiBinding [[reflgen::table("ScriptApi", "version", "structSize", CreatorScriptApiVersion)]] = ScriptApiTable;

    static_assert(offsetof(ScriptApiTable, Simulation_Status)
        == offsetof(ScriptApiTable, Camera_NotifyCameraCut) + sizeof(decltype(ScriptApiTable::Camera_NotifyCameraCut)));
}
