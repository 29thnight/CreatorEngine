#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Transform.generated.h"

class RenderScene;
class InspectorWindow;
class GameObject;
struct Transform
{
public:
   ReflectTransform
    [[Serializable]]
    Transform() = default;
    ~Transform() = default;

	Transform(const Transform& other);
	Transform(Transform&& other) noexcept;

	Transform& operator=(const Transform& rhs);
	Transform& operator=(Transform&& rhs) noexcept;

    [[Property]]
	Mathf::Vector4 position{ 0.f, 0.f, 0.f, 1.f };
    [[Property]]
	Mathf::Vector4 rotation{ 0.f, 0.f, 0.f, 1.f };
    [[Property]]
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

	Mathf::Matrix GetLocalMatrix();
	Mathf::Matrix GetWorldMatrix() const;
	Mathf::Matrix GetInverseMatrix() const;

	void UpdateLocalMatrix();
	Mathf::Matrix UpdateWorldMatrix();
	void SetLocalMatrix(const Mathf::Matrix& matrix);
	void SetAndDecomposeMatrix(const Mathf::Matrix& matrix, bool setLocal = false);

	Mathf::xVector GetWorldPosition() const;
	Mathf::xVector GetWorldScale() const;
	Mathf::xVector GetWorldQuaternion() const;

	Mathf::Vector4 ASGetWorldPosition() const;
	Mathf::Vector4 ASGetWorldScale() const;
	Mathf::Vector4 ASGetWorldQuaternion() const;

	Mathf::Vector3 GetForward();
	Mathf::Vector3 GetRight();
	Mathf::Vector3 GetUp();
	void SetDirty();
	bool IsDirty() const;

	void SetParentID(uint32 id);

	void TransformReset();

private:
	friend class RenderScene;
	friend class InspectorWindow;

	GameObject* m_owner{ nullptr };
	[[Property]]
	uint32 m_parentID{ 0 };

	[[Property]]
	bool32 m_dirty{ true };
	Mathf::xMatrix m_worldMatrix{ XMMatrixIdentity() };
	Mathf::xMatrix m_localMatrix{ XMMatrixIdentity() };
	Mathf::xMatrix m_inverseMatrix{ XMMatrixIdentity() };

	Mathf::xVector m_worldScale{ 1.f, 1.f, 1.f, 1.f };
	Mathf::xVector m_worldQuaternion{ 0.f, 0.f, 0.f, 1.f };
	Mathf::xVector m_worldPosition{ 0.f, 0.f, 0.f, 1.f };
};
