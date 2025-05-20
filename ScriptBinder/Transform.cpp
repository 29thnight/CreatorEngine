#include "Transform.h"
#include "GameObject.h"

Transform& Transform::SetScale(Mathf::Vector3 scale)
{
	m_dirty = true;
	this->scale = Mathf::Vector4(scale);

	return *this;
}

Transform& Transform::SetPosition(Mathf::Vector3 pos)
{
	m_dirty = true;
	position = Mathf::Vector4(pos);

	return *this;
}

Transform& Transform::AddPosition(Mathf::Vector3 pos)
{
	m_dirty = true;
	position = DirectX::XMVectorAdd(position, pos);

	return *this;
}

Transform& Transform::SetRotation(Mathf::Quaternion quaternion)
{
	m_dirty = true;
	rotation = quaternion;

	return *this;
}

Transform& Transform::AddRotation(Mathf::Quaternion quaternion)
{
	m_dirty = true;
	rotation = DirectX::XMQuaternionMultiply(quaternion, rotation);

	return *this;
}

Mathf::xMatrix Transform::GetLocalMatrix()
{
	if (m_dirty)
	{
		m_localMatrix = DirectX::XMMatrixScalingFromVector(scale);
		m_localMatrix *= DirectX::XMMatrixRotationQuaternion(rotation);
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
	Mathf::xVector _scale{}, _rotation{}, _position{};

	m_localMatrix = matrix;
	DirectX::XMMatrixDecompose(&_scale, &_rotation, &_position, m_localMatrix);

	XMStoreFloat4(&position, _position);
	XMStoreFloat4(&scale, _scale);
	XMStoreFloat4(&rotation, _rotation);

	m_dirty = false;
}

void Transform::SetAndDecomposeMatrix(const Mathf::xMatrix& matrix)
{
	m_worldMatrix = matrix;
	XMMatrixDecompose(&m_worldScale, &m_worldQuaternion, &m_worldPosition, m_worldMatrix);
	
	XMMATRIX parentMat = GameObject::FindIndex(m_parentID)->m_transform.GetWorldMatrix();
	XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentMat);
	XMMATRIX newLocalMatrix = XMMatrixMultiply(XMMATRIX(matrix), parentWorldInverse);
	m_localMatrix = newLocalMatrix;

	SetLocalMatrix(m_localMatrix);
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

bool Transform::IsDirty() const
{
	return m_dirty;
}

void Transform::SetParentID(uint32 id)
{
	m_parentID = id;
}
