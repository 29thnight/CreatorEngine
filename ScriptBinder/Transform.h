#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Core.Minimal.h"
#include "TransformStore.h"
#include <optional>
#include <memory>

class RenderScene;
class InspectorWindow;
class GameObject;

// SceneGraphRedesignPlan.md §4 트랙 S, S1 — Transform은 스토어 슬롯의 뷰다.
//
// position/rotation/scale/m_parentID는 여전히 물리 [[Property]] 멤버다 —
// ScriptBinder.vcxproj의 PreBuildEvent(HeaderTool\MetaGenerator.exe, 소스
// 미보유 사전빌드 툴)가 이 태그들을 물리 멤버 pointer-to-member로만
// 리플렉션·직렬화하기 때문에(Utility_Framework/ReflectionFunction.h
// MakeProperty), 옮기면 직렬화 왕복이 끊긴다 — Utility_Framework는 이 슬라이스의
// 편집 범위 밖이다(실측 근거, 코드로 판정). 대신 직렬화되지 않던 나머지 여섯
// 필드(로컬/월드 행렬·dirty·월드 캐시 3종)를 Scene 소유 TransformStore(SoA)로
// 옮긴다 — "값 멤버 소멸"의 실질(데이터가 GameObject 밖으로)은 이 여섯 필드에서
// 성립한다.
struct Transform
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
    Transform() = default;
    ~Transform() = default;

	Transform(const Transform& other);
	Transform(Transform&& other) noexcept;

	Transform& operator=(const Transform& rhs);
	Transform& operator=(Transform&& rhs) noexcept;

	Mathf::Vector4 position{ 0.f, 0.f, 0.f, 1.f };
	Mathf::Vector4 rotation{ 0.f, 0.f, 0.f, 1.f };
	Mathf::Vector4 scale{ 1.f, 1.f, 1.f, 1.f };

	Transform& SetScale(Mathf::Vector3 scale);
	Transform& SetPosition(Mathf::Vector3 pos);
	Transform& AddPosition(Mathf::Vector3 pos);
	Transform& SetRotation(Mathf::Quaternion quaternion);
	Transform& AddRotation(Mathf::Quaternion quaternion);

	Transform& SetWorldPosition(Mathf::Vector3 pos);
	Transform& SetWorldRotation(Mathf::Quaternion quaternion);
	Transform& SetWorldScale(Mathf::Vector3 scale);

	void SetOwner(GameObject* owner);
	GameObject* GetOwner() const { return m_owner; }

	Mathf::xMatrix GetLocalMatrix();
	Mathf::xMatrix GetWorldMatrix() const;
	Mathf::xMatrix GetWorldMatrix_NoScale() const; //add joker1092 :: need for physics

	void UpdateLocalMatrix();
	Mathf::xMatrix UpdateWorldMatrix();
	void SetLocalMatrix(const Mathf::xMatrix& matrix);
	void SetAndDecomposeMatrix(const Mathf::xMatrix& matrix, bool setLocal = false);

	Mathf::xVector GetWorldPosition() const;
	Mathf::xVector GetWorldScale() const;
	Mathf::xVector GetWorldQuaternion() const;

	Mathf::Vector3 GetForward();
	Mathf::Vector3 GetRight();
	Mathf::Vector3 GetUp();
	void SetDirty();
	bool IsDirty() const;

	void TransformReset();
	[[deprecated]]
	void UpdateDirty();

private:
	friend class RenderScene;
	friend class InspectorWindow;
	// 부모 ID는 GameObject::SetParentIndex를 통해서만 바뀐다. 여기를 열어두면
	// m_parentIndex와 짝이 어긋난 채로 컴파일이 통과한다.
	friend class GameObject;

	void SetParentID(uint32 id);

	// ── 스토어 슬롯 해석 (SceneGraphRedesignPlan §4 트랙 S, S1) ──
	//
	// 파생 데이터(행렬 2개·dirty·월드 캐시 3개)의 정본은 owner의 씬이 들고 있는
	// TransformStore다. 슬롯 번호는 owner->m_index를 그대로 쓰지만, 그것만으로는
	// 부족하다 — 기본 생성자로 만들어진 임시 오브젝트(리플렉션 팩토리, 프리팹
	// 편집 중간 상태 등)가 m_ownerScene을 활성 씬으로, m_index를 0(관례상 씬
	// 루트 슬롯)으로 남긴 채 떠 있을 수 있기 때문이다(GameObject.cpp 기본
	// 생성자 — m_index(0)). 그 상태로 그냥 스토어에 접근하면 활성 씬의 루트
	// 트랜스폼을 오염시킨다. 그래서 매번 "그 슬롯의 진짜 점유자가 나 자신인가"
	// 까지 확인한다(Scene::GetGameObjectRaw) — 실패하면 로컬 폴백으로 내려간다.
	// 비용은 포인터 비교 하나 — 트래버설 경로의 캐시(재해석 생략)는 S2 소관.
	struct StoreSlot
	{
		TransformStore* store;
		size_t slot;
	};
	std::optional<StoreSlot> ResolveStore() const;

	// 스토어에 붙을 수 없을 때(씬 없는 GameObject 등)의 폴백 저장소. 드물게
	// 쓰이므로 lazy 포인터로 미룬다 — 상시 인라인 멤버로 두면 스토어 도입의
	// 실익(GameObject 밖으로 데이터가 나가는 것 — sizeof(Transform) 축소)이
	// 없어진다.
	struct LocalFallback
	{
		Mathf::Matrix  localMatrix{ Mathf::Matrix::Identity };
		Mathf::Matrix  worldMatrix{ Mathf::Matrix::Identity };
		bool           dirty{ true };
		Mathf::Vector4 worldScale{ 1.f, 1.f, 1.f, 1.f };
		Mathf::Vector4 worldQuaternion{ 0.f, 0.f, 0.f, 1.f };
		Mathf::Vector4 worldPosition{ 0.f, 0.f, 0.f, 1.f };
	};
	LocalFallback& Fallback() const;

	Mathf::xMatrix GetStoredLocalMatrix() const;
	void SetStoredLocalMatrix(const Mathf::xMatrix& m);
	Mathf::xMatrix GetStoredWorldMatrix() const;
	void SetStoredWorldMatrix(const Mathf::xMatrix& m);
	bool GetStoredDirty() const;
	void SetStoredDirty(bool value);
	Mathf::xVector GetStoredWorldScale() const;
	void SetStoredWorldScale(const Mathf::xVector& v);
	Mathf::xVector GetStoredWorldQuaternion() const;
	void SetStoredWorldQuaternion(const Mathf::xVector& v);
	Mathf::xVector GetStoredWorldPosition() const;
	void SetStoredWorldPosition(const Mathf::xVector& v);

	GameObject* m_owner{ nullptr };
	uint32 m_parentID{ 0 };

	mutable std::unique_ptr<LocalFallback> m_fallback;
};
