#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "TransformStore.h"
#include <mathematics/quaternion.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <optional>
#include <memory>
#include <cstdint>
#include <string_view>

class RenderScene;
class InspectorWindow;
class Entity;
class Scene;
namespace Meta { enum class PropertyChangeSource : std::uint8_t; }

// TransformUpdatePlan X1: local/world authored write가 어느 길목에서 들어왔는지
// 계측하는 분류다. X1에서는 카운터만 올렸고, X2부터 같은 publication이 Scene의
// spatial domain epoch도 올린다.
enum class TransformWriteReason : std::uint8_t
{
	CppSetter,
	Script,
	Inspector,
	Reflection,
	Prefab,
	Physics,
	Socket,
	Gizmo,
	Animator,
	ModelImport,
	Hierarchy,
	Reset,
	Count
};

inline constexpr size_t kTransformWriteReasonCount =
	static_cast<size_t>(TransformWriteReason::Count);
const char* TransformWriteReasonName(TransformWriteReason reason);

// SceneGraphRedesignPlan.md §4 트랙 S, S1-b — Transform이 진짜 Component가 됐다.
//
// position/rotation/scale/m_parentID는 여전히 물리 멤버이고, 직렬화되지 않던
// 나머지 여섯 필드(로컬/월드 행렬·dirty·월드 캐시 3종)는 Scene 소유
// TransformStore(SoA)에 있다(S1) — 이 부분은 이번 슬라이스로 바뀌지 않는다.
//
// ★ 이번 슬라이스(2026-08-18)가 바꾼 것: struct Transform → class Transform :
// meta::identity<Transform, Component>. Entity::m_transform 값 멤버가
// 사라지고 Entity::m_components 안의 슬롯으로 옮겨갔다(Entity.h/.cpp).
// 기반 필드 4종(Object의 m_name·m_instanceID·m_isEnabled + Component의
// m_FileID)이 이제 저장 시 Transform 블록에 함께 방출된다 — 의도된 형상
// 변경이고, 리플렉션 골든 재기준선은 통합 담당 소관이다.
//
// ★ 남은 호환성 부채: `obj->Transform_().Foo()` 형태의 직접 접근부가 엔진 144·
// Dynamic_CPP 297곳 있었다. 값 멤버가 없어졌으니 전부 깨진다. 참조 멤버로
// `m_transform`이라는 이름 자체를 살리는 안은 기각했다 — GameObject의 이동
// 생성자(Entity.h)를 깨뜨린다는 것이 선행 조사로 확정됐다. 대신
// GameObject가 `Transform_()` 접근자(및 기존 `GetComponent<Transform>()`,
// 이제 O(1) 캐시 조회)를 제공한다. 이 슬라이스가 소유한 파일(Transform.cpp·
// Entity.cpp) 안의 호출부는 이미 고쳤다 — 나머지는 이 슬라이스 최종
// 보고의 "통합 시 필요한 배선" 목록 참고(파일 밖 편집 금지 규칙 때문에
// 여기서 고칠 수 없었다).
class Transform : public meta::identity<Transform, Component>
{
   public:
   static consteval auto reflect()
   {
       using Self = Transform;
       return meta::schema<Self>(
           meta::field<&Self::position>,
           meta::field<&Self::rotation>,
           meta::field<&Self::scale>,
           meta::field<&Self::m_parentID>);
   }
public:
	void CaptureSceneTransferState();
	void RestoreSceneTransferState();
    Transform() = default;
    ~Transform() = default;

	// 복사/이동 4종 삭제 (S1-b). Component가 상속하는 Object의 복사 생성자가
	// 삭제돼 있어(Object.h — instanceID 복제가 유령 GUID를 만든다) 의미가
	// 이미 깨져 있었다. 값 복사가 필요했던 호출부(Dynamic_CPP 5곳)는 전부
	// 읽기 전용 스냅샷이었다 — 레인 3이 GetComponent<Transform>() 참조나
	// 개별 필드 복사로 바꾼다.
	Transform(const Transform&) = delete;
	Transform(Transform&&) = delete;
	Transform& operator=(const Transform&) = delete;
	Transform& operator=(Transform&&) = delete;

	math::vector3 GetPosition() const;
	math::quaternion GetRotation() const;
	math::vector3 GetScale() const;
	const math::vector4& GetPositionValue() const { return position; }
	const math::vector4& GetRotationValue() const { return rotation; }
	const math::vector4& GetScaleValue() const { return scale; }

	Transform& SetScale(math::vector3 scale,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetPosition(math::vector3 pos,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& AddPosition(math::vector3 pos,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetRotation(math::quaternion quaternion,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& AddRotation(math::quaternion quaternion,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetPositionValue(const math::vector4& value,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetRotationValue(const math::vector4& value,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetScaleValue(const math::vector4& value,
		TransformWriteReason reason = TransformWriteReason::CppSetter);

	Transform& SetWorldPosition(math::vector3 pos,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetWorldRotation(math::quaternion quaternion,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	Transform& SetWorldScale(math::vector3 scale,
		TransformWriteReason reason = TransformWriteReason::CppSetter);

	// Component::SetOwner가 이미 virtual이다(직전 커밋) — override로 두 경로
	// (리플렉션 로드 vs 템플릿 AddComponent<T>())를 합류시킨다. GetOwner()는
	// 기반(Component)이 주는 것과 완전히 같아져서 제거했다 — m_pOwner가 유일한
	// 소유자 저장소다(아래 m_owner 필드 소멸).
	void SetOwner(Entity* owner) override;

	math::matrix4x4 GetLocalMatrix();
	math::matrix4x4 GetWorldMatrix() const;
	math::matrix4x4 GetWorldMatrix_NoScale() const; //add joker1092 :: need for physics

	void UpdateLocalMatrix();
	math::matrix4x4 UpdateWorldMatrix();
	void SetLocalMatrix(const math::matrix4x4& matrix,
		TransformWriteReason reason = TransformWriteReason::CppSetter);
	void SetAndDecomposeMatrix(const math::matrix4x4& matrix, bool setLocal = false,
		TransformWriteReason reason = TransformWriteReason::CppSetter);

	math::vector3 GetWorldPosition() const;
	math::vector3 GetWorldScale() const;
	math::quaternion GetWorldQuaternion() const;

	math::vector3 GetForward();
	math::vector3 GetRight();
	math::vector3 GetUp();
	void SetDirty();
	bool IsDirty() const;

	// S2(dirty push / lazy pull) — 월드 행렬이 SetAndDecomposeMatrix로 마지막
	// 소비 이후 실제로 다시 쓰였는지 "읽고 내린다"(pull 소비, 원자적). dirty와
	// 독립인 이유·왜 순회가 이걸로 자식 전파를 결정해야 하는지는 TransformStore.h
	// worldChanged 필드 주석 참고. Scene::UpdateModelRecursive 전용 — 다른
	// 소비자가 먼저 불러 값을 가로채면 그 순회가 자식 전파를 놓친다.
	bool ConsumeWorldChanged();

	void TransformReset(
		TransformWriteReason reason = TransformWriteReason::Reset);
	// typed reflection이 실제 존재하는 필드를 쓴 직후 호출한다. detach 상태면
	// reason 하나를 보류했다가 owner/scene 부착 뒤 publish한다.
	void OnPropertyChanged(std::string_view propertyName,
		Meta::PropertyChangeSource source);
	void FlushPendingLocalWrite();
	[[deprecated]]
	void UpdateDirty();

private:
	friend class RenderScene;
	friend class InspectorWindow;
	friend class Scene;
	// 부모 ID는 Entity::SetParentIndex를 통해서만 바뀐다. 여기를 열어두면
	// m_parentIndex와 짝이 어긋난 채로 컴파일이 통과한다.
	friend class Entity;

	void SetParentID(uint32 id);
	bool PublishLocalWrite(TransformWriteReason reason);
	math::matrix4x4 ComposeAuthoredLocalMatrix() const
	{
		return math::compose(
			math::vector3{ scale.x, scale.y, scale.z },
			math::quaternion{ rotation.x, rotation.y, rotation.z, rotation.w },
			math::vector3{ position.x, position.y, position.z });
	}

	// 디스크 스키마는 계속 x/y/z/w 네 필드다. reflect()는 클래스 내부에서
	// private member pointer를 만들므로 공개 필드가 아니어도 기존 키가 유지된다.
	math::vector4 position{ 0.f, 0.f, 0.f, 1.f };
	math::vector4 rotation{ 0.f, 0.f, 0.f, 1.f };
	math::vector4 scale{ 1.f, 1.f, 1.f, 1.f };
	bool m_hasPendingLocalWrite = false;
	TransformWriteReason m_pendingLocalWriteReason = TransformWriteReason::Reflection;

	// ── 스토어 슬롯 해석 (SceneGraphRedesignPlan §4 트랙 S, S1) ──
	//
	// 파생 데이터(행렬 2개·dirty·월드 캐시 3개)의 정본은 owner의 씬이 들고 있는
	// TransformStore다. 슬롯 번호는 owner->m_index를 그대로 쓰지만, 그것만으로는
	// 부족하다 — 기본 생성자로 만들어진 임시 오브젝트(리플렉션 팩토리, 프리팹
	// 편집 중간 상태 등)가 m_ownerScene을 활성 씬으로, m_index를 0(관례상 씬
	// 루트 슬롯)으로 남긴 채 떠 있을 수 있기 때문이다(Entity.cpp 기본
	// 생성자 — m_index(0)). 그 상태로 그냥 스토어에 접근하면 활성 씬의 루트
	// 트랜스폼을 오염시킨다. 그래서 매번 "그 슬롯의 진짜 점유자가 나 자신인가"
	// 까지 확인한다(Scene::GetEntityRaw) — 실패하면 로컬 폴백으로 내려간다.
	// 비용은 포인터 비교 하나 — 트래버설 경로의 캐시(재해석 생략)는 S2 소관.
	struct StoreSlot
	{
		TransformStore* store;
		size_t slot;
	};
	std::optional<StoreSlot> ResolveStore() const;

	// 스토어에 붙을 수 없을 때(씬 없는 GameObject 등)의 폴백 저장소. 드물게
	// 쓰이므로 lazy 포인터로 미룬다 — 상시 인라인 멤버로 두면 스토어 도입의
	// 실익(Entity 밖으로 데이터가 나가는 것 — sizeof(Transform) 축소)이
	// 없어진다.
	struct LocalFallback
	{
		math::matrix4x4 localMatrix{ math::matrix4x4::identity() };
		math::matrix4x4 worldMatrix{ math::matrix4x4::identity() };
		bool           dirty{ true };
		math::vector4  worldScale{ 1.f, 1.f, 1.f, 1.f };
		math::vector4  worldQuaternion{ 0.f, 0.f, 0.f, 1.f };
		math::vector4  worldPosition{ 0.f, 0.f, 0.f, 1.f };
		// worldChanged(S2)의 폴백 저장소 — TransformStore.h 주석 참고.
		bool           worldChanged{ true };
	};
	LocalFallback& Fallback() const;

	math::matrix4x4 GetStoredLocalMatrix() const;
	void SetStoredLocalMatrix(const math::matrix4x4& m);
	math::matrix4x4 GetStoredWorldMatrix() const;
	void SetStoredWorldMatrix(const math::matrix4x4& m);
	bool GetStoredDirty() const;
	void SetStoredDirty(bool value);
	bool GetStoredWorldChanged() const;
	void SetStoredWorldChanged(bool value);
	math::vector4 GetStoredWorldScale() const;
	void SetStoredWorldScale(const math::vector4& v);
	math::vector4 GetStoredWorldQuaternion() const;
	void SetStoredWorldQuaternion(const math::vector4& v);
	math::vector4 GetStoredWorldPosition() const;
	void SetStoredWorldPosition(const math::vector4& v);

	// m_owner 필드 소멸(S1-b) — Component::m_pOwner(protected, 기반 제공)가
	// 유일한 소유자 저장소다. Transform 자신의 멤버 함수는 이름 은닉 없이
	// m_pOwner를 직접 쓴다(Transform은 템플릿이 아니라 CRTP 베이스 조회
	// 규칙의 영향을 받지 않는다).
	uint32 m_parentID{ 0 };

	mutable std::unique_ptr<LocalFallback> m_fallback;
};
