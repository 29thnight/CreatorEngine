#include "Transform.h"
#include "Entity.h"
#include "Scene.h"

// ── 스토어 슬롯 해석 (SceneGraphRedesignPlan §4 트랙 S, S1) ──
std::optional<Transform::StoreSlot> Transform::ResolveStore() const
{
	if (!m_pOwner) return std::nullopt;

	Scene* scene = m_pOwner->GetScene();
	if (!scene) return std::nullopt;

	const Entity::Index idx = m_pOwner->m_index;
	if (idx < 0) return std::nullopt;

	// "그 슬롯의 진짜 점유자가 나 자신인가" — 기본 생성자의 m_index=0 같은
	// 가짜 인덱스가 다른 오브젝트(대개 씬 루트)의 슬롯을 잘못 가리키는 사고를
	// 막는다(Transform.h StoreSlot 주석 참고).
	if (scene->GetEntityRaw(idx) != m_pOwner) return std::nullopt;

	return StoreSlot{ &scene->GetTransformStore(), static_cast<size_t>(idx) };
}

Transform::LocalFallback& Transform::Fallback() const
{
	if (!m_fallback)
	{
		m_fallback = std::make_unique<LocalFallback>();
	}
	return *m_fallback;
}

void Transform::CaptureSceneTransferState()
{
	// ReleaseSlot이 옛 Scene의 SoA 슬롯을 초기화하기 전에 값을 객체 로컬 임시
	// 저장소로 복사한다. DDOL unique_ptr은 이 상태로 이송 버퍼에 머문다.
	LocalFallback snapshot;
	snapshot.localMatrix = GetStoredLocalMatrix();
	snapshot.worldMatrix = GetStoredWorldMatrix();
	snapshot.dirty = GetStoredDirty();
	snapshot.worldScale = GetStoredWorldScale();
	snapshot.worldQuaternion = GetStoredWorldQuaternion();
	snapshot.worldPosition = GetStoredWorldPosition();
	snapshot.worldChanged = GetStoredWorldChanged();
	m_fallback = std::make_unique<LocalFallback>(snapshot);
}

void Transform::RestoreSceneTransferState()
{
	if (!m_fallback) return;
	const LocalFallback snapshot = *m_fallback;
	SetStoredLocalMatrix(snapshot.localMatrix);
	SetStoredWorldMatrix(snapshot.worldMatrix);
	SetStoredDirty(snapshot.dirty);
	SetStoredWorldScale(snapshot.worldScale);
	SetStoredWorldQuaternion(snapshot.worldQuaternion);
	SetStoredWorldPosition(snapshot.worldPosition);
	SetStoredWorldChanged(snapshot.worldChanged);
	m_fallback.reset();
}

Mathf::xMatrix Transform::GetStoredLocalMatrix() const
{
	if (auto s = ResolveStore()) return s->store->localMatrix[s->slot];
	return Fallback().localMatrix;
}

void Transform::SetStoredLocalMatrix(const Mathf::xMatrix& m)
{
	if (auto s = ResolveStore()) s->store->localMatrix[s->slot] = m;
	else Fallback().localMatrix = m;
}

Mathf::xMatrix Transform::GetStoredWorldMatrix() const
{
	if (auto s = ResolveStore()) return s->store->worldMatrix[s->slot];
	return Fallback().worldMatrix;
}

void Transform::SetStoredWorldMatrix(const Mathf::xMatrix& m)
{
	if (auto s = ResolveStore()) s->store->worldMatrix[s->slot] = m;
	else Fallback().worldMatrix = m;
}

bool Transform::GetStoredDirty() const
{
	if (auto s = ResolveStore()) return 0 != s->store->dirty[s->slot];
	return Fallback().dirty;
}

void Transform::SetStoredDirty(bool value)
{
	if (auto s = ResolveStore()) s->store->dirty[s->slot] = value ? 1 : 0;
	else Fallback().dirty = value;
}

bool Transform::GetStoredWorldChanged() const
{
	if (auto s = ResolveStore()) return 0 != s->store->worldChanged[s->slot];
	return Fallback().worldChanged;
}

void Transform::SetStoredWorldChanged(bool value)
{
	if (auto s = ResolveStore()) s->store->worldChanged[s->slot] = value ? 1 : 0;
	else Fallback().worldChanged = value;
}

Mathf::xVector Transform::GetStoredWorldScale() const
{
	if (auto s = ResolveStore()) return s->store->worldScale[s->slot];
	return Fallback().worldScale;
}

void Transform::SetStoredWorldScale(const Mathf::xVector& v)
{
	if (auto s = ResolveStore()) s->store->worldScale[s->slot] = v;
	else Fallback().worldScale = v;
}

Mathf::xVector Transform::GetStoredWorldQuaternion() const
{
	if (auto s = ResolveStore()) return s->store->worldQuaternion[s->slot];
	return Fallback().worldQuaternion;
}

void Transform::SetStoredWorldQuaternion(const Mathf::xVector& v)
{
	if (auto s = ResolveStore()) s->store->worldQuaternion[s->slot] = v;
	else Fallback().worldQuaternion = v;
}

Mathf::xVector Transform::GetStoredWorldPosition() const
{
	if (auto s = ResolveStore()) return s->store->worldPosition[s->slot];
	return Fallback().worldPosition;
}

void Transform::SetStoredWorldPosition(const Mathf::xVector& v)
{
	if (auto s = ResolveStore()) s->store->worldPosition[s->slot] = v;
	else Fallback().worldPosition = v;
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
static Entity* FindTransformParent(Entity* owner)
{
	if (nullptr == owner) return nullptr;
	const Entity::Index parentIndex = owner->GetParentIndex();
	if (0 == parentIndex || !Entity::IsValidIndex(parentIndex)) return nullptr;
	return owner->OwnerSceneFindIndex(parentIndex);
}

Transform& Transform::SetWorldPosition(Mathf::Vector3 pos)
{
	Entity* parent = FindTransformParent(m_pOwner);
	if (nullptr == parent) return SetPosition(pos);

	DirectX::XMMATRIX parentWorldMat = parent->Transform_().GetWorldMatrix();
	DirectX::XMMATRIX parentWorldInverse = DirectX::XMMatrixInverse(nullptr, parentWorldMat);
	Mathf::Vector3 newLocalposition = DirectX::XMVector3TransformCoord(pos, parentWorldInverse);
	return SetPosition(newLocalposition);
}

Transform& Transform::SetWorldRotation(Mathf::Quaternion quaternion)
{
	Entity* parent = FindTransformParent(m_pOwner);
	if (nullptr == parent) return SetRotation(quaternion);

	Mathf::Quaternion parentWorldQua = parent->Transform_().GetWorldQuaternion();
	Mathf::Quaternion parentWorldInverse = DirectX::XMQuaternionInverse(parentWorldQua);

	// 월드 = 로컬 다음 부모다. XMQuaternionMultiply(A, B)는 "A를 적용한 뒤 B"이므로
	// 월드 = Multiply(로컬, 부모) 이고, 따라서 로컬 = Multiply(월드, 부모역)이다.
	// 인자 순서가 뒤집혀 있어서 부모가 회전해 있으면 엉뚱한 축으로 돌아갔다
	// (부모가 회전한 뼈에 월드 회전을 걸어 실측 확인).
	Mathf::Quaternion newLocalrotation = DirectX::XMQuaternionMultiply(quaternion, parentWorldInverse);
	return SetRotation(newLocalrotation);
}

Transform& Transform::SetWorldScale(Mathf::Vector3 scale)
{
	Entity* parent = FindTransformParent(m_pOwner);
	if (nullptr == parent) return SetScale(scale);

	// 스케일은 행렬로 되돌리면 안 된다 — TransformCoord는 이동 성분까지 먹어서
	// 부모가 원점에서 떨어져 있기만 해도 값이 망가진다. 부모 월드 스케일로 나눈다.
	Mathf::Vector3 parentWorldScale{};
	DirectX::XMStoreFloat3(&parentWorldScale, parent->Transform_().GetWorldScale());

	constexpr float kMinScale = 1e-6f;
	Mathf::Vector3 newLocalscale{
		std::abs(parentWorldScale.x) > kMinScale ? scale.x / parentWorldScale.x : scale.x,
		std::abs(parentWorldScale.y) > kMinScale ? scale.y / parentWorldScale.y : scale.y,
		std::abs(parentWorldScale.z) > kMinScale ? scale.z / parentWorldScale.z : scale.z };

	return SetScale(newLocalscale);
}

Mathf::xMatrix Transform::GetLocalMatrix()
{
	if (GetStoredDirty())
	{
		Mathf::xMatrix local = DirectX::XMMatrixScalingFromVector(scale);
		local *= DirectX::XMMatrixRotationQuaternion(rotation);
		local *= DirectX::XMMatrixTranslationFromVector(position);
		SetStoredLocalMatrix(local);
		SetStoredDirty(false);
		// S2 — dirty는 "로컬 행렬이 TRS와 어긋났다"는 캐시 플래그이고, 그것을
		// 내리는 일이 이렇게 **읽기 쪽**에서도 일어난다. 순회 밖에서 누가 먼저
		// 읽으면 dirty가 꺼진 채 순회가 도착해 "안 바뀌었다"로 오판한다.
		// 로컬이 실제로 다시 계산된 이 자리에서 월드 갱신 필요 신호를 남긴다.
		SetStoredWorldChanged(true);
	}

	return GetStoredLocalMatrix();
}

Mathf::xMatrix Transform::GetWorldMatrix() const
{
	return GetStoredWorldMatrix();
}

//add joker1092
Mathf::xMatrix Transform::GetWorldMatrix_NoScale() const
{
	// 로컬 회전·이동만으로 행렬을 구성합니다.
	Mathf::xMatrix localMatrix_NoScale = DirectX::XMMatrixRotationQuaternion(rotation);
	localMatrix_NoScale *= DirectX::XMMatrixTranslationFromVector(position);

	// 부모가 있다면, 부모의 스케일이 빠진 월드 행렬과 결합해서 전달합니다.
	if (m_pOwner)
	{
		const Entity::Index parentIndex = m_pOwner->GetParentIndex();
		if (Entity::IsValidIndex(parentIndex))
		{
			if (Entity* parent = m_pOwner->OwnerSceneFindIndex(parentIndex))
			{
				return DirectX::XMMatrixMultiply(localMatrix_NoScale, parent->Transform_().GetWorldMatrix_NoScale());
			}
		}
	}

	return localMatrix_NoScale;
}

void Transform::UpdateLocalMatrix()
{
	if (GetStoredDirty())
	{
		Mathf::xMatrix local = DirectX::XMMatrixScalingFromVector(scale);
		local *= DirectX::XMMatrixRotationQuaternion(rotation);
		local *= DirectX::XMMatrixTranslationFromVector(position);
		SetStoredLocalMatrix(local);
		SetStoredDirty(false);
		// S2 — dirty는 "로컬 행렬이 TRS와 어긋났다"는 캐시 플래그이고, 그것을
		// 내리는 일이 이렇게 **읽기 쪽**에서도 일어난다. 순회 밖에서 누가 먼저
		// 읽으면 dirty가 꺼진 채 순회가 도착해 "안 바뀌었다"로 오판한다.
		// 로컬이 실제로 다시 계산된 이 자리에서 월드 갱신 필요 신호를 남긴다.
		SetStoredWorldChanged(true);
	}
}

Mathf::xMatrix Transform::UpdateWorldMatrix()
{
	// 부모 인덱스가 유효해도 조회는 실패할 수 있다 — 파괴된 슬롯(tombstone)이나
	// 씬 이송 중간 상태가 그렇다. 슬롯맵 전환(트랙 E1) 이후 무효 조회는 널을
	// 돌려주므로 검사 없이 역참조하면 그대로 죽는다(같은 무검사 패턴이
	// SceneViewWindow에서 실제 크래시로 드러났다 — 2026-08-18 덤프).
	// 부모를 못 찾으면 최상위로 취급한다.
	const Entity::Index parentIndex = m_pOwner
		? m_pOwner->GetParentIndex()
		: Entity::INVALID_INDEX;
	Entity* parent = (nullptr != m_pOwner && Entity::IsValidIndex(parentIndex))
		? m_pOwner->OwnerSceneFindIndex(parentIndex)
		: nullptr;

	if (nullptr != parent) {
		DirectX::XMMATRIX parentWorldMatrix = parent->Transform_().UpdateWorldMatrix();
		UpdateLocalMatrix();
		DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixMultiply(GetStoredLocalMatrix(), parentWorldMatrix);
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
		SetAndDecomposeMatrix(GetStoredLocalMatrix(), false);
		return GetStoredWorldMatrix();
	}
}

void Transform::SetOwner(Entity* owner)
{
	// 기반 구현을 먼저 호출한다 — m_pOwner·m_pTransform을 세우는 정본은
	// Component::SetOwner 하나다(리플렉션 로드 경로와 템플릿 AddComponent<T>()
	// 경로가 여기서 합류한다). Transform.h 클래스 상단 주석 참고.
	Component::SetOwner(owner);

	// Transform의 "내 트랜스폼" 캐시는 자기 자신이다. 기반이 하는 조회는 부착
	// 순서에 따라 자기를 못 찾을 수 있다 — Meta::Type 경로의 AddComponent는
	// SetOwner를 push_back **앞에** 부르므로 그 시점의 m_components에 자기가 없다.
	// 여기서 self로 덮어 두면 어느 경로로 만들어져도 불변식이 성립한다
	// (Component.inl의 GetComponent<Transform>() 지름길이 이 값을 읽는다).
	m_pTransform = this;
	if (owner)
	{
		m_parentID = owner->GetParentIndex();
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

	SetStoredLocalMatrix(matrix);
	DirectX::XMMatrixDecompose(&_scale, &_rotation, &_position, matrix);

	/*if (std::isnan(_scale.m128_f32[0]) || std::isnan(_scale.m128_f32[1]) || std::isnan(_scale.m128_f32[2]) ||
		std::isnan(_rotation.m128_f32[0]) || std::isnan(_rotation.m128_f32[1]) || std::isnan(_rotation.m128_f32[2]) ||
		std::isnan(_position.m128_f32[0]) || std::isnan(_position.m128_f32[1]) || std::isnan(_position.m128_f32[2])) {
		std::cout << "Nan transform" << std::endl;
	}*/

	DirectX::XMStoreFloat4(&position, _position);
	DirectX::XMStoreFloat4(&scale, _scale);
	DirectX::XMStoreFloat4(&rotation, DirectX::XMVector4Normalize(_rotation));

	SetStoredDirty(false);
	// S2 — 여기서 dirty를 내리는 것은 맞다(로컬 행렬을 직접 써 넣었으니 TRS와
	// 어긋나지 않는다). 그러나 **월드는 낡았다**. dirty 하나로 두 뜻을 겸하던
	// 시절엔 순회가 무조건 전량 재계산이라 무해했지만, dirty를 재계산 게이트로
	// 승격시킨 뒤로는 이 자리에서 신호를 안 남기면 그 오브젝트가 영원히
	// 스킵된다 — 실제로 Socket::Update(무기 부착, 매 프레임)와 씬뷰 기즈모
	// 드래그가 이 경로다. 호출부마다 SetDirty를 흩뿌리는 대신, 불변식을 아는
	// 유일한 자리인 여기서 한 번 세운다.
	SetStoredWorldChanged(true);
}

void Transform::SetAndDecomposeMatrix(const Mathf::xMatrix& matrix, bool setLocal)
{
	Mathf::Matrix compareMat = matrix;
	if (compareMat == GetStoredWorldMatrix()) return;

	SetStoredWorldMatrix(matrix);
	// S2 — 값을 실제로 쓰는 이 자리에서만 세운다. 호출 경로가 Scene 순회든
	// ClrHost::EnsureWorldMatrix든 PhysicsManager든 상관없이, "자식에게 아직
	// 못 알린 변경이 있다"는 사실은 여기서만 정확히 안다(TransformStore.h 참고).
	SetStoredWorldChanged(true);

	Mathf::xVector worldScale{}, worldQuaternion{}, worldPosition{};
	DirectX::XMMatrixDecompose(&worldScale, &worldQuaternion, &worldPosition, matrix);
	worldQuaternion = DirectX::XMVector4Normalize(worldQuaternion);

	SetStoredWorldScale(worldScale);
	SetStoredWorldQuaternion(worldQuaternion);
	SetStoredWorldPosition(worldPosition);

	const Entity::Index parentIndex = m_pOwner
		? m_pOwner->GetParentIndex()
		: Entity::INVALID_INDEX;
	Entity* parentObject = (nullptr != m_pOwner)
		? m_pOwner->OwnerSceneFindIndex(parentIndex)
		: nullptr;
	if (!parentObject && nullptr != m_pOwner)
	{
		// 부모를 못 찾은 사실을 m_parentID에도 반영해 둔다(기존 동작). 같은 값으로
		// 다시 조회해 봐야 결과가 같으므로 재조회는 하지 않는다.
		m_parentID = parentIndex;
	}

	if (setLocal) {
		if (nullptr == parentObject)
		{
			// 최상위 오브젝트는 월드가 곧 로컬이다 — 역산할 부모 월드 행렬 자체가
			// 없다. 예전에는 이 경우에도 부모를 역참조해서 죽었다.
			SetLocalMatrix(matrix);
		}
		else
		{
			DirectX::XMMATRIX parentMat = parentObject->Transform_().GetWorldMatrix();
			DirectX::XMMATRIX parentWorldInverse = DirectX::XMMatrixInverse(nullptr, parentMat);
			DirectX::XMMATRIX newLocalMatrix = DirectX::XMMatrixMultiply(matrix, parentWorldInverse);

			SetLocalMatrix(newLocalMatrix);
		}
	}
}

Mathf::xVector Transform::GetWorldPosition() const
{
	return GetStoredWorldPosition();
}

Mathf::xVector Transform::GetWorldScale() const
{
	return GetStoredWorldScale();
}

Mathf::xVector Transform::GetWorldQuaternion() const
{
	return GetStoredWorldQuaternion();
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
	SetStoredDirty(true);
	//	if (m_pOwner && m_pOwner->GetScene())
	//	{
	//		m_pOwner->GetScene()->RegisterDirtyTransform(this);
	//	}
}

bool Transform::IsDirty() const
{
	return GetStoredDirty();
}

bool Transform::ConsumeWorldChanged()
{
	const bool wasChanged = GetStoredWorldChanged();
	if (wasChanged)
	{
		SetStoredWorldChanged(false);
	}
	return wasChanged;
}

// 현재 계약은 '로컬 유지'다 — 부모가 바뀌면 월드 위치가 따라 움직인다.
//
// 예전에는 여기서 oldWorld에 부모 역행렬을 곱해 newLocal을 구했는데, 그 값을
// 아무 데도 대입하지 않아 월드 보존은 실제로 일어난 적이 없다. 호출처 몇 곳의
// "월드 유지" 주석은 그래서 사실과 다르다. 죽은 계산만 걷어내고 동작은 그대로
// 둔다 — 월드 보존으로 바꾸면 그 동작에 맞춰 저장된 씬·프리팹이 전부 틀어진다.
void Transform::SetParentID(uint32 id)
{
	m_parentID = id;
	SetDirty();
}

void Transform::TransformReset()
{
	position = Mathf::Vector4{ 0.f, 0.f, 0.f, 0.f };
	rotation = Mathf::Vector4{ 0.f, 0.f, 0.f, 0.f };
	scale = Mathf::Vector4{ 1.f, 1.f, 1.f, 1.f };
	SetDirty();
}

void Transform::UpdateDirty()
{
	if (m_pOwner && m_pOwner->GetScene())
	{
		//m_pOwner->GetScene()->RegisterDirtyTransform(this);
	}
}
