#include "Transform.h"
#include "GameObject.h"
#include "Scene.h"

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
	SetDirty();
	this->scale = Mathf::Vector4(scale);

	return *this;
}

Transform& Transform::SetPosition(Mathf::Vector3 pos)
{
	SetDirty();
	position = Mathf::Vector4(pos);

	return *this;
}

Transform& Transform::AddPosition(Mathf::Vector3 pos)
{
	SetDirty();
	position = DirectX::XMVectorAdd(position, pos);

	return *this;
}

Transform& Transform::SetRotation(Mathf::Quaternion quaternion)
{
	SetDirty();
	rotation = quaternion;

	return *this;
}

Transform& Transform::AddRotation(Mathf::Quaternion quaternion)
{
	SetDirty();
	rotation = DirectX::XMQuaternionMultiply(quaternion, rotation);

	return *this;
}

// 부모가 없으면 월드 = 로컬이다.
//
// 인덱스 0은 씬 루트라 항상 항등이므로 예전부터 부모 없음과 같이 취급해 왔다.
// 여기에 널 검사를 더한 이유는, 부모 인덱스가 INVALID_INDEX(-1)인 오브젝트가
// 실제로 존재하기 때문이다(Main Camera·Directional Light 등 최상위 오브젝트).
// 전에는 그런 오브젝트에 월드 setter를 부르면 FindIndex가 널을 돌려주고
// 곧바로 역참조해서 죽었다.
static GameObject* FindTransformParent(const GameObject* owner)
{
	if (nullptr == owner || 0 == owner->m_parentIndex) return nullptr;
	return GameObject::FindIndex(owner->m_parentIndex);
}

Transform& Transform::SetWorldPosition(Mathf::Vector3 pos)
{
	GameObject* parent = FindTransformParent(m_owner);
	if (nullptr == parent) return SetPosition(pos);

	XMMATRIX parentWorldMat = parent->m_transform.GetWorldMatrix();
	XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentWorldMat);
	Mathf::Vector3 newLocalposition = XMVector3TransformCoord(pos, parentWorldInverse);
	return SetPosition(newLocalposition);
}

Transform& Transform::SetWorldRotation(Mathf::Quaternion quaternion)
{
	GameObject* parent = FindTransformParent(m_owner);
	if (nullptr == parent) return SetRotation(quaternion);

	Mathf::Quaternion parentWorldQua = parent->m_transform.GetWorldQuaternion();
	Mathf::Quaternion parentWorldInverse = XMQuaternionInverse(parentWorldQua);

	// 월드 = 로컬 다음 부모다. XMQuaternionMultiply(A, B)는 "A를 적용한 뒤 B"이므로
	// 월드 = Multiply(로컬, 부모) 이고, 따라서 로컬 = Multiply(월드, 부모역)이다.
	// 인자 순서가 뒤집혀 있어서 부모가 회전해 있으면 엉뚱한 축으로 돌아갔다
	// (부모가 회전한 뼈에 월드 회전을 걸어 실측 확인).
	Mathf::Quaternion newLocalrotation = XMQuaternionMultiply(quaternion, parentWorldInverse);
	return SetRotation(newLocalrotation);
}

Transform& Transform::SetWorldScale(Mathf::Vector3 scale)
{
	GameObject* parent = FindTransformParent(m_owner);
	if (nullptr == parent) return SetScale(scale);

	// 스케일은 행렬로 되돌리면 안 된다 — TransformCoord는 이동 성분까지 먹어서
	// 부모가 원점에서 떨어져 있기만 해도 값이 망가진다. 부모 월드 스케일로 나눈다.
	Mathf::Vector3 parentWorldScale{};
	XMStoreFloat3(&parentWorldScale, parent->m_transform.GetWorldScale());

	constexpr float kMinScale = 1e-6f;
	Mathf::Vector3 newLocalscale{
		std::abs(parentWorldScale.x) > kMinScale ? scale.x / parentWorldScale.x : scale.x,
		std::abs(parentWorldScale.y) > kMinScale ? scale.y / parentWorldScale.y : scale.y,
		std::abs(parentWorldScale.z) > kMinScale ? scale.z / parentWorldScale.z : scale.z };

	return SetScale(newLocalscale);
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

//add joker1092
Mathf::xMatrix Transform::GetWorldMatrix_NoScale() const
{
	// ���� ȸ���� ��ġ������ ����� �����մϴ�.
	Mathf::xMatrix localMatrix_NoScale = DirectX::XMMatrixRotationQuaternion(rotation);
	localMatrix_NoScale *= DirectX::XMMatrixTranslationFromVector(position);

	// �θ� �ִٸ�, �θ��� ������ ���� ���� ����� ��������� �����ݴϴ�.
	if (m_owner && GameObject::IsValidIndex(m_owner->m_parentIndex))
	{
		if (auto parent = m_owner->GetScene()->TryGetGameObject(m_owner->m_parentIndex))
		{
			return XMMatrixMultiply(localMatrix_NoScale, parent->m_transform.GetWorldMatrix_NoScale());
		}
	}

	return localMatrix_NoScale;
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
	else
	{
		UpdateLocalMatrix();
		// ★ 대입이 아니라 분해 경로를 탄다. 예전에는 여기서 m_worldMatrix에
		//   local을 대입만 해서, 루트 오브젝트의 m_worldPosition·
		//   m_worldQuaternion 캐시가 영영 초기값(원점·항등)으로 남았다.
		//   부모가 있는 분기는 SetAndDecomposeMatrix로 캐시를 갱신하는데
		//   루트만 건너뛴 것이다. GetWorldMatrix만 읽는 소비자(메시 프록시)는
		//   무사했고, GetWorldPosition/GetWorldQuaternion을 읽는 소비자
		//   (광원 프록시·CameraComponent)만 루트 오브젝트에서 조용히 깨졌다 —
		//   PIX 실측 검증에서 광원 위치가 전부 (0,0,0)으로 찍혀 드러났다
		//   (2026-08-09). setLocal=false: 루트는 local이 곧 world라 역산이
		//   필요 없고, 그 분기의 부모 역참조도 피한다.
		SetAndDecomposeMatrix(m_localMatrix, false);
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
	SetDirty();
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
	auto forward = Mathf::Vector3::TransformNormal(Mathf::Vector3::UnitZ, GetWorldMatrix());
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
	if (!m_dirty)
	{
		m_dirty = true;
	}
	//	if (m_owner && m_owner->GetScene())
	//	{
	//		m_owner->GetScene()->RegisterDirtyTransform(this);
	//	}
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
	SetDirty();
}

void Transform::UpdateDirty()
{
	if (m_owner && m_owner->GetScene())
	{
		//m_owner->GetScene()->RegisterDirtyTransform(this);
	}
}
