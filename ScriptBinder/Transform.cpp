#include "Transform.h"
#include "GameObject.h"

Transform::Transform(const Transform& other) :
	position(other.position),
	rotation(other.rotation),
	scale(other.scale),
	m_parentID(other.m_parentID),
	m_worldMatrix(other.m_worldMatrix),
	m_localMatrix(other.m_localMatrix),
	m_inverseMatrix(other.m_inverseMatrix),
	m_worldScale(other.m_worldScale),
	m_worldQuaternion(other.m_worldQuaternion),
	m_worldPosition(other.m_worldPosition)
{
}

Transform::Transform(Transform&& other) noexcept :
	position(std::exchange(other.position, {})),
	rotation(std::exchange(other.rotation, {})),
	scale(std::exchange(other.scale, {})),
	m_parentID(std::exchange(other.m_parentID, {})),
	m_worldMatrix(std::exchange(other.m_worldMatrix, {})),
	m_localMatrix(std::exchange(other.m_localMatrix, {})),
	m_inverseMatrix(std::exchange(other.m_inverseMatrix, {})),
	m_worldScale(std::exchange(other.m_worldScale, {})),
	m_worldQuaternion(std::exchange(other.m_worldQuaternion, {})),
	m_worldPosition(std::exchange(other.m_worldPosition, {}))
{
}

Transform& Transform::operator=(const Transform& rhs)
{
	position = rhs.position;
	rotation = rhs.rotation;
	scale = rhs.scale;
	m_parentID = rhs.m_parentID;
	m_worldMatrix = rhs.m_worldMatrix;
	m_localMatrix = rhs.m_localMatrix;
	m_inverseMatrix = rhs.m_inverseMatrix;
	m_worldScale = rhs.m_worldScale;
	m_worldQuaternion = rhs.m_worldQuaternion;
	m_worldPosition = rhs.m_worldPosition;

	return *this;
}

Transform& Transform::operator=(Transform&& rhs) noexcept
{
	position			= std::exchange(rhs.position,{});
	rotation			= std::exchange(rhs.rotation, {});
	scale				= std::exchange(rhs.scale, {});
	m_parentID			= std::exchange(rhs.m_parentID, {});
	m_worldMatrix		= std::exchange(rhs.m_worldMatrix, {});
	m_localMatrix		= std::exchange(rhs.m_localMatrix, {});
	m_inverseMatrix		= std::exchange(rhs.m_inverseMatrix, {});
	m_worldScale		= std::exchange(rhs.m_worldScale, {});
	m_worldQuaternion	= std::exchange(rhs.m_worldQuaternion, {});
	m_worldPosition		= std::exchange(rhs.m_worldPosition, {});

	return *this;
}

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

Transform& Transform::SetWorldPosition(Mathf::Vector3 pos)
{
	// TODO: 여기에 return 문을 삽입합니다.
	if (m_owner->m_parentIndex == 0)
		return SetPosition(pos);
	else {
		auto parent = GameObject::FindIndex(m_owner->m_parentIndex);
		XMMATRIX parentWorldMat = parent->m_transform.GetWorldMatrix();
		XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentWorldMat);
		Mathf::Vector3 newLocalposition = XMVector3TransformCoord(pos, parentWorldInverse);
		return SetPosition(newLocalposition);
	}
}

Transform& Transform::SetWorldRotation(Mathf::Quaternion quaternion)
{
	if (m_owner->m_parentIndex == 0)
		return SetRotation(quaternion);
	else {
		auto parent = GameObject::FindIndex(m_owner->m_parentIndex);
		Mathf::Quaternion parentWorldQua = parent->m_transform.GetWorldQuaternion();
		Mathf::Quaternion parentWorldInverse = XMQuaternionInverse(parentWorldQua);
		Mathf::Quaternion newLocalrotation = XMQuaternionMultiply(parentWorldInverse, quaternion);
		return SetRotation(newLocalrotation);
	}
}

Transform& Transform::SetWorldScale(Mathf::Vector3 scale)
{
	if (m_owner->m_parentIndex == 0)
		return SetScale(scale);
	else {
		auto parent = GameObject::FindIndex(m_owner->m_parentIndex);
		XMMATRIX parentWorldMat = parent->m_transform.GetWorldMatrix();
		XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentWorldMat);
		Mathf::Vector3 newLocalscale = XMVector3TransformCoord(scale, parentWorldInverse);
		return SetScale(newLocalscale);
	}
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

void Transform::UpdateLocalMatrix()
{
	if (m_dirty)
	{
		m_localMatrix = DirectX::XMMatrixScalingFromVector(scale);
		m_localMatrix *= DirectX::XMMatrixRotationQuaternion(rotation);
		m_localMatrix *= DirectX::XMMatrixTranslationFromVector(position);
		m_dirty = false;
	}
}

Mathf::xMatrix Transform::UpdateWorldMatrix()
{
	if (m_owner->m_parentIndex != -1) {
		auto parent = GameObject::FindIndex(m_owner->m_parentIndex);
		XMMATRIX parentWorldMatrix = parent->m_transform.UpdateWorldMatrix();
		UpdateLocalMatrix();
		XMMATRIX worldMatrix = XMMatrixMultiply(m_localMatrix, parentWorldMatrix);
		SetAndDecomposeMatrix(worldMatrix);
		return worldMatrix;
	}
	else {
		UpdateLocalMatrix();
		m_worldMatrix = m_localMatrix;
		return m_worldMatrix;
	}
}

void Transform::SetOwner(GameObject* owner)
{
	m_owner = owner;
	if (owner)
	{
		m_parentID = owner->m_parentIndex;
	}
	else
	{
		m_parentID = 0;
	}
}

void Transform::SetLocalMatrix(const Mathf::xMatrix& matrix)
{
	Mathf::xVector _scale{}, _rotation{}, _position{};

	m_localMatrix = matrix;
	DirectX::XMMatrixDecompose(&_scale, &_rotation, &_position, m_localMatrix);

	/*if (std::isnan(_scale.m128_f32[0]) || std::isnan(_scale.m128_f32[1]) || std::isnan(_scale.m128_f32[2]) ||
		std::isnan(_rotation.m128_f32[0]) || std::isnan(_rotation.m128_f32[1]) || std::isnan(_rotation.m128_f32[2]) ||
		std::isnan(_position.m128_f32[0]) || std::isnan(_position.m128_f32[1]) || std::isnan(_position.m128_f32[2])) {
		std::cout << "Nan transform" << std::endl;
	}*/

	XMStoreFloat4(&position, _position);
	XMStoreFloat4(&scale, _scale);
	XMStoreFloat4(&rotation, DirectX::XMVector4Normalize(_rotation));

	m_dirty = false;
}

void Transform::SetAndDecomposeMatrix(const Mathf::xMatrix& matrix, bool setLocal)
{
	Mathf::Matrix compareMat = matrix;
	if (compareMat == m_worldMatrix) return;

	m_worldMatrix = matrix;
	XMMatrixDecompose(&m_worldScale, &m_worldQuaternion, &m_worldPosition, m_worldMatrix);
	m_worldQuaternion = DirectX::XMVector4Normalize(m_worldQuaternion);

	GameObject* parentObject = GameObject::FindIndex(m_owner->m_parentIndex);
	if (!parentObject)
	{
		m_parentID = m_owner->m_parentIndex;
		parentObject = GameObject::FindIndex(m_parentID);
	}

	if (setLocal) {
		XMMATRIX parentMat = parentObject->m_transform.GetWorldMatrix();
		XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentMat);
		XMMATRIX newLocalMatrix = XMMatrixMultiply(matrix, parentWorldInverse);

		SetLocalMatrix(newLocalMatrix);
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

Mathf::Vector3 Transform::GetForward()
{
	auto forward = Mathf::Vector3::TransformNormal(Mathf::Vector3::Forward, GetWorldMatrix());
	forward.Normalize();
	return forward;
}

Mathf::Vector3 Transform::GetRight()
{
	auto right = Mathf::Vector3::TransformNormal(Mathf::Vector3::Right, GetWorldMatrix());
	right.Normalize();
	return right;
}

Mathf::Vector3 Transform::GetUp()
{
	auto up = Mathf::Vector3::TransformNormal(Mathf::Vector3::Up, GetWorldMatrix());
	up.Normalize();
	return up;
}

void Transform::SetDirty()
{
	m_dirty = true;
}

bool Transform::IsDirty() const
{
	return m_dirty;
}

void Transform::SetParentID(uint32 id)
{
	XMMATRIX oldWorld = GetWorldMatrix();
	m_parentID = id;

	XMMATRIX parentWorldMatrix = XMMatrixIdentity();
	if (m_parentID != 0)
	{
		if (auto parent = GameObject::FindIndex(m_parentID))
		{
			parentWorldMatrix = parent->m_transform.GetWorldMatrix();
		}
	}

	XMMATRIX parentInverse = XMMatrixInverse(nullptr, parentWorldMatrix);
	XMMATRIX newLocal = XMMatrixMultiply(oldWorld, parentInverse);
	SetDirty();
}

void Transform::TransformReset()
{
	position = { Mathf::xVectorZero };
	rotation = { Mathf::xVectorZero };
	scale = { Mathf::xVectorOne };
	m_dirty = true;
}
