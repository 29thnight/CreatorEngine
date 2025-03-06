#pragma once
#include "Core.Minimal.h"

struct Transform
{
public:
	Mathf::xVector position;
	Mathf::xVector rotation;
	Mathf::xVector scale;

	Transform& SetScale(Mathf::Vector3 scale);
	Transform& SetPosition(Mathf::Vector3 pos);
	Transform& AddPosition(Mathf::Vector3 pos);
	Transform& SetRotation(Mathf::Vector3 eulerAngles);

	Mathf::xMatrix GetLocalMatrix();
	Mathf::xMatrix GetWorldMatrix() const;
	Mathf::xMatrix GetInverseMatrix() const;

	void SetLocalMatrix(const Mathf::xMatrix& matrix);
	void SetAndDecomposeMatrix(const Mathf::xMatrix& matrix);

private:
	friend class Scene;
	bool32 m_dirty{};
	Mathf::xMatrix m_worldMatrix{};
	Mathf::xMatrix m_localMatrix{};
	Mathf::xMatrix m_inverseMatrix{};

	Mathf::xVector m_worldScale{};
	Mathf::xVector m_worldQuaternion{};
	Mathf::xVector m_worldPosition{};
};