#pragma once
#include "Core.Minimal.h"

struct Transform
{
public:
	Mathf::xVector position{ 0.f, 0.f, 0.f, 1.f };
	Mathf::xVector rotation{ 0.f, 0.f, 0.f, 1.f };
	Mathf::xVector scale{ 1.f, 1.f, 1.f, 1.f };

	Transform& SetScale(Mathf::Vector3 scale);
	Transform& SetPosition(Mathf::Vector3 pos);
	Transform& AddPosition(Mathf::Vector3 pos);
	Transform& SetRotation(Mathf::Quaternion quaternion);
	Transform& AddRotation(Mathf::Quaternion quaternion);

	Mathf::xMatrix GetLocalMatrix();
	Mathf::xMatrix GetWorldMatrix() const;
	Mathf::xMatrix GetInverseMatrix() const;

	void SetLocalMatrix(const Mathf::xMatrix& matrix);
	void SetAndDecomposeMatrix(const Mathf::xMatrix& matrix);

	Mathf::xVector GetWorldPosition() const;
	Mathf::xVector GetWorldScale() const;
	Mathf::xVector GetWorldQuaternion() const;

private:
	friend class Scene;
	bool32 m_dirty{};
	Mathf::xMatrix m_worldMatrix{ XMMatrixIdentity() };
	Mathf::xMatrix m_localMatrix{ XMMatrixIdentity() };
	Mathf::xMatrix m_inverseMatrix{ XMMatrixIdentity() };

	Mathf::xVector m_worldScale{ 1.f, 1.f, 1.f, 1.f };
	Mathf::xVector m_worldQuaternion{ 0.f, 0.f, 0.f, 1.f };
	Mathf::xVector m_worldPosition{ 0.f, 0.f, 0.f, 1.f };
};