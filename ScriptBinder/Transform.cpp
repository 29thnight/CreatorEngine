#include "Transform.h"

Transform& Transform::SetScale(Mathf::Vector3 scale)
{
	m_dirty = true;
	this->scale = scale;

	return *this;
}

Transform& Transform::SetPosition(Mathf::Vector3 pos)
{
	m_dirty = true;
	position = pos;

	return *this;
}

Transform& Transform::AddPosition(Mathf::Vector3 pos)
{
	m_dirty = true;
	position = DirectX::XMVectorAdd(position, pos);

	return *this;
}

Transform& Transform::SetRotation(Mathf::Vector3 eulerAngles)
{
	m_dirty = true;
	rotation = DirectX::XMVectorAdd(rotation, eulerAngles);

	return *this;
}

Mathf::xMatrix Transform::GetLocalMatrix()
{
	if (m_dirty)
	{
		m_localMatrix = DirectX::XMMatrixScalingFromVector(scale);
		m_localMatrix *= DirectX::XMMatrixRotationRollPitchYawFromVector(rotation);
		m_localMatrix *= DirectX::XMMatrixTranslationFromVector(position);
		m_dirty = false;
	}

	return m_localMatrix;
}

Mathf::xMatrix Transform::GetWorldMatrix() const
{
	return m_worldMatrix;
}

Mathf::xMatrix Transform::GetInverseMatrix() const
{
	return m_inverseMatrix;
}

void Transform::SetLocalMatrix(const Mathf::xMatrix& matrix)
{
	m_localMatrix = matrix;
	DirectX::XMMatrixDecompose(&scale, &rotation, &position, m_localMatrix);
	m_dirty = false;
}

void Transform::SetAndDecomposeMatrix(const Mathf::xMatrix& matrix)
{
	m_worldMatrix = matrix;
	XMMatrixDecompose(&m_worldScale, &m_worldQuaternion, &m_worldPosition, m_worldMatrix);
	Mathf::xVector determinant{};
	if (XMVectorGetX(determinant) != 0.0f)
	{
		m_inverseMatrix = XMMatrixInverse(&determinant, m_worldMatrix);
	}
	else
	{
		//throw std::exception("Inverse Matrix does not exist");
	}
}

Mathf::xVector Transform::GetWorldPosition() const
{
	return m_worldPosition;
}

Mathf::xVector Transform::GetWorldScale() const
{
	return m_worldScale;
}

Mathf::xVector Transform::GetWorldQuaternion() const
{
	return m_worldQuaternion;
}
