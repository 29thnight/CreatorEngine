#include "TestComponent.h"
#include "InputManager.h"
#include "RigidbodyComponent.h"
#include "Object.h"
#include "../RenderEngine/ImGuiRegister.h"
#include "Physics/Common.h"
#include "ColliderComponent.h"
#include "InteractableComponent.h"
#include <vector>

TestComponent::TestComponent()
{
}

void TestComponent::Initialize()
{
	auto position = _owner->GetLocalPosition();
	position *= 10.f;

	physx::PxCapsuleControllerDesc controllerDesc;
	controllerDesc.position = physx::PxExtendedVec3(position.x, position.y, position.z);
	controllerDesc.radius = attribute.radius;
	controllerDesc.height = 1.f;
	controllerDesc.stepOffset = attribute.stepOffset;
	controllerDesc.slopeLimit = attribute.slopeLimit;
	controllerDesc.contactOffset = attribute.contactOffset;
	controllerDesc.maxJumpHeight = attribute.maxJumpHeight;
	controllerDesc.material = Physics->GetDefaultMaterial();
	controllerDesc.reportCallback = &contactCallback;
	controllerDesc.contactOffset = PxF32(0.001f);

	controller = (physx::PxCapsuleController*)Physics->GetControllerManager()->createController(controllerDesc);
	controller->getActor()->userData = static_cast<IRigidbody*>(this);
	auto actor = controller->getActor();
	uint32_t shapeCount = actor->getNbShapes();
	physx::PxShape** shapes = new physx::PxShape * [shapeCount];
	actor->getShapes(shapes, shapeCount);

	info.isCharacterController = true;

	for (uint32_t i = 0; i < shapeCount; ++i) {
		shapes[i]->userData = static_cast<ICollider*>(this);
	}
}

void TestComponent::FixedUpdate(float fixedTick)
{
	//Vector3 position = GetOwner()->GetLocalPosition();
	//position.y -= controller->getHeight() * 0.5f;

	//std::vector<HitInfo> infos;
	//int hitcount = raycastAllResult(position, Vector3(0.f, -1.f, 0.f), 1.f, infos);
	//if (hitcount > 0) {
	//	for (auto& info : infos) {
	//		auto rigid = static_cast<IRigidbody*>(info.iRigidbody);
	//		auto thisrigid = static_cast<IRigidbody*>(this);
	//		if (rigid == thisrigid)
	//		{
	//			continue;
	//		}

	//		if (position.y < info.hitPosition[1]) {
	//			position.y = info.hitPosition[1];
	//			auto e = PxExtendedVec3(position.x, position.y, position.z);
	//			controller->setPosition(PxExtendedVec3(position.x, position.y, position.z));
	//			//_owner->SetPosition(position);
	//		}
	//	}
	//	if (hitcount > 1) {
	//		int  a = 1;
	//	}
	//}

	//Vector3 position = GetOwner()->GetLocalPosition();
	//position.y -= controller->getHeight() * 0.5f;

	//physx::PxRaycastBuffer hit;
	//bool hitDetected = controller->getScene()->raycast(PxVec3(position.x, position.y, position.z), physx::PxVec3(0, -1, 0), 1.0f, hit);

	//if (hitDetected && hit.hasAnyHits()) {
	//	// 바닥과 충돌이 있으면 Y축 위치를 바닥 위치로 수정
	//	if (position.y < hit.block.position.y) {
	//		position.y = hit.block.position.y;  // 바닥 위치로 보정
	//		controller->setPosition(PxExtendedVec3(position.x, position.y, position.z));
	//	}
	//}

	//float2 dir = { 0.f, 0.f };
	float2 tempdir = { 0.f, 0.f };
	if (InputManagement->IsControllerConnected(playerID)) {
		tempdir = InputManagement->GetControllerThumbL(playerID);
		tempdir.y *= -1.f;
		if (pow(tempdir.x, 2) + pow(tempdir.y, 2) < 0.04f) {
			tempdir.x = 0.f;
			tempdir.y = 0.f;
		}
	}
	else {
		if (InputManagement->IsKeyPressed('J')) {
			tempdir.x += 1.f;
		}
		else if (InputManagement->IsKeyPressed('L')) {
			tempdir.x -= 1.f;
		}
		if (InputManagement->IsKeyPressed('I')) {
			tempdir.y += 1.f;
		}
		else if (InputManagement->IsKeyPressed('K')) {
			tempdir.y -= 1.f;
		}
	}
	PxVec3 surfaceNormal = contactCallback.groundNormal;  // 표면 법선 벡터 (충돌 콜백에서 얻을 수 있음)
	PxVec3 moveDirection = PxVec3(tempdir.x, 0.f, tempdir.y);  // XZ 평면에서의 이동 벡터

	// 법선 벡터와 이동 벡터의 수직 성분을 구하기 위해 법선 벡터와의 내적을 뺍니다.
	PxVec3 flatMoveDirection = moveDirection - surfaceNormal * surfaceNormal.dot(moveDirection);
	flatMoveDirection.normalize();  // 평면에서 수평으로 정규화된 이동 벡터

	// 속도를 표면의 법선에 맞게 수정합니다.
	flatMoveDirection *= moveSpeed;  // 이동 속도 적용

	// 새로운 이동 벡터를 velocity에 반영합니다.
	//velocity.x = flatMoveDirection.x;
	//velocity.z = flatMoveDirection.z;
	if (contactCallback.isGround) {
		velocity = flatMoveDirection;
		if ((InputManagement->IsControllerButtonDown(playerID, ControllerButton::A) || InputManagement->IsKeyPressed(' ')) && contactCallback.isGround) {
			velocity.y = jumpVelocity;
			contactCallback.isGround = false;
		}
		contactCallback.isGround = false;
	}
	else {
		velocity.x = flatMoveDirection.x;
		velocity.z = flatMoveDirection.z;
	}
	//tempdir.x *= moveSpeed;
	//tempdir.y *= moveSpeed;
	//
	//PxVec3 temp = { tempdir.x, 0.f, tempdir.y };
	//PxVec3 horizontalMovement = temp - (temp.dot(contactCallback.groundNormal) * contactCallback.groundNormal);
	//
	//velocity.x = horizontalMovement.x;
	//velocity.z = horizontalMovement.y;
	//velocity.x = tempdir.x;
	//velocity.z = tempdir.y;
	

	velocity.y += gravity * fixedTick;
	PxVec3 displacement = velocity * fixedTick;

	PxControllerCollisionFlags collisionFlags = controller->move(displacement, 0.001f, fixedTick, physx::PxControllerFilters());

	if (collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) {
		velocity.y = 0; // 바닥과 충돌 시 Y속도 초기화
	}

	/*
	if (InputManagement->IsKeyDown('N')) {
		auto rb = _owner->GetComponent<RigidbodyComponent>();

		if (rb != nullptr) {
			float velocity[3] = { 2000.f * fixedTick, 0.f, 0.f };
			rb->AddForce(velocity, ForceMode::FORCE);
		}
	}
	if (InputManagement->IsKeyDown('M')) {
		auto rb = _owner->GetComponent<RigidbodyComponent>();

		if (rb != nullptr) {
			float velocity[3] = { -2000.f * fixedTick, 0.f, 0.f };
			rb->AddForce(velocity, ForceMode::FORCE);
		}
	}
	if (InputManagement->IsKeyDown(' ')) {
		auto rb = _owner->GetComponent<RigidbodyComponent>();

		if (rb != nullptr) {
			float velocity[3] = { 0.f, jumpVelocity, 0.f };
			rb->AddForce(velocity, ForceMode::IMPULSE);
		}
	}

	if (InputManagement->IsControllerConnected(0)) {
		float2 dir = InputManagement->GetControllerThumbL(0);
		if (pow(dir.x, 2) + pow(dir.y, 2) < 0.04f)
			return;
		dir.x *= moveSpeed;
		dir.y *= moveSpeed;
		auto rb = _owner->GetComponent<RigidbodyComponent>();

		if (rb != nullptr) {
			float velocity[3] = { -dir.x * fixedTick, 0.f, dir.y * fixedTick };
			rb->AddForce(velocity, ForceMode::FORCE);
		}
	}
	else {
		Vector2 dir = { 0, 0 };
		if (InputManagement->IsKeyPressed('A')) {
			dir.x += 1.f;
		}
		else if (InputManagement->IsKeyPressed('D')) {
			dir.x -= 1.f;
		}
		if (InputManagement->IsKeyPressed('W')) {
			dir.y += 1.f;
		}
		else if (InputManagement->IsKeyPressed('S')) {
			dir.y -= 1.f;
		}
		dir.Normalize();

		dir.x *= moveSpeed;
		dir.y *= moveSpeed;
		auto rb = _owner->GetComponent<RigidbodyComponent>();

		if (rb != nullptr) {
			float velocity[3] = { -dir.x * fixedTick, 0.f, dir.y * fixedTick };
			rb->AddForce(velocity, ForceMode::FORCE);
		}
	}
	*/

	if (InputManagement->IsKeyDown('P')) {
		Physics->ConnectPVD();
	}
}

void TestComponent::Update(float tick)
{

	direction.Normalize();

	if (InputManagement->IsKeyDown('Y')) {
		DirectX::SimpleMath::Vector3 pos = originPos;
		DirectX::SimpleMath::Vector3 view = direction;

		ICollider* g = nullptr;
		bool isHit = raycast(pos, view, 1000.f, &g);

		ICollider* coll = raycast(pos, view, 1000.f);

		if (coll != nullptr) {
			auto collider = static_cast<ColliderComponent*>(coll);
			if (collider != nullptr)
				std::cout << collider->GetOwner()->GetName() << std::endl;
		}
	}

	if (InputManagement->IsControllerButtonDown(playerID, ControllerButton::X) ||
		InputManagement->IsKeyDown('K')) {
		int size = interactable.size();
		for (auto& inter : interactable) {
			inter->Interact(_owner);
		}
	}
}

void TestComponent::LateUpdate(float tick)
{
}

void TestComponent::EditorContext()
{
	ImGui::Text("TestComponent");

	ImGui::Spacing();


	auto tempSpeed = moveSpeed;
	auto tempJump = jumpVelocity;
	auto tempPos = originPos;
	auto tempDir = direction;
	auto tempPlayerID = playerID;
	auto tempGravity = gravity;

	ImGui::DragFloat("MoveSpeed", &tempSpeed, 10.f, 0.f, 10000.f);
	ImGui::DragFloat("JumpVelocity", &tempJump, 10.f, 0.f, 10000.f);
	ImGui::DragFloat3("originPos", &tempPos.x, 0.1f, -100.f, 100.f);
	ImGui::DragFloat3("originDir", &tempDir.x, 0.05f, -360.f, 360.f);
	ImGui::DragInt("PlayerID", &tempPlayerID, 1.f, 0, 3);
	ImGui::DragFloat("Graviry", &tempGravity, 0.1f, 100.f, -100.f);

	moveSpeed = tempSpeed;
	jumpVelocity = tempJump;
	originPos = tempPos;
	direction = tempDir;
	playerID = tempPlayerID;
	gravity = tempGravity;
}

void TestComponent::Serialize(_inout json& out)
{
	json data;

	data["TestComponent"]["moveSpeed"] = { moveSpeed };
	data["TestComponent"]["jumpVelocity"] = { jumpVelocity };
	data["TestComponent"]["gravity"] = { gravity };
	data["TestComponent"]["playerID"] = { playerID };

	data["Type"] = MetaType<TestComponent>::type;

	out["components"].push_back(data);
}

void TestComponent::DeSerialize(_in json& in)
{
	moveSpeed = in["TestComponent"]["moveSpeed"][0].get<float>();
	jumpVelocity = in["TestComponent"]["jumpVelocity"][0].get<float>();
	gravity = in["TestComponent"]["gravity"][0].get<float>();
	playerID = in["TestComponent"]["playerID"][0].get<int>();
}

void TestComponent::OnTriggerEnter(ICollider* other)
{
	auto o = other->GetOwner();
	auto inter = o->GetComponent<InteractableComponent>();
	AddInteract(inter);
}

void TestComponent::OnTriggerStay(ICollider* other)
{
}

void TestComponent::OnTriggerExit(ICollider* other)
{
	auto o = other->GetOwner();
	auto inter = o->GetComponent<InteractableComponent>();
	RemoveInteract(inter);
}

void TestComponent::OnCollisionEnter(ICollider* other)
{
}

void TestComponent::OnCollisionStay(ICollider* other)
{
}

void TestComponent::OnCollisionExit(ICollider* other)
{
}

void TestComponent::AddInteract(InteractableComponent* inter)
{
	if(inter != nullptr)
		interactable.insert(inter);
}

void TestComponent::RemoveInteract(InteractableComponent* inter)
{
	interactable.erase(inter);
}

std::tuple<float, float, float> TestComponent::GetWorldPosition()
{
	auto worldMat = _owner->GetWorldMatrix();
	Vector3 position(worldMat.Translation());
	return std::make_tuple(position.x, position.y, position.z);
}

std::tuple<float, float, float, float> TestComponent::GetWorldRotation()
{
	auto worldMat = _owner->GetWorldMatrix();
	Quaternion rotation = Quaternion::CreateFromRotationMatrix(worldMat);
	return std::make_tuple(rotation.x, rotation.y, rotation.z, rotation.w);
}

void TestComponent::SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot)
{
	auto p = controller->getPosition();
	p /= 10.f;
	_owner->SetPosition(Vector3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));

	return;

	auto parent = _owner->GetParent();

	auto ownerMat = _owner->GetWorldMatrix();
	Vector3 ownerPos;
	Quaternion ownerRot;
	Vector3 ownerScale;
	ownerMat.Decompose(ownerScale, ownerRot, ownerPos);
	Vector3 ownerEular = ownerRot.ToEuler() * 57.295779513f;

	if (parent != nullptr) {
		auto parentMat = parent->GetWorldMatrix();

		auto inverse = DirectX::XMMatrixInverse(nullptr, parentMat);
		auto [posX, posY, posZ] = pos;
		Vector3 localPos = Vector3::Transform(Vector3(posX, posY, posZ), inverse);

		auto [rotX, rotY, rotZ, rotW] = rot;
		Quaternion parentRot = XMQuaternionRotationMatrix(parentMat);
		Quaternion inversParentRot = XMQuaternionInverse(parentRot);
		Quaternion localRot = XMQuaternionMultiply({ rotX,rotY,rotZ,rotW }, inversParentRot);

		Vector3 eular = localRot.ToEuler() * 57.295779513f;

		ownerPos.x = info.freezePosition[0] ? ownerPos.x : localPos.x;
		ownerPos.y = info.freezePosition[1] ? ownerPos.y : localPos.y;
		ownerPos.z = info.freezePosition[2] ? ownerPos.z : localPos.z;

		ownerEular.x = info.freezeRotation[0] ? ownerEular.x : eular.x;
		ownerEular.y = info.freezeRotation[1] ? ownerEular.y : eular.y;
		ownerEular.z = info.freezeRotation[2] ? ownerEular.z : eular.z;

		ownerPos /= 10.f;

		_owner->SetPosition(ownerPos);
		_owner->SetRotation(ownerEular);
	}
	else {

		auto [posX, posY, posZ] = pos;
		auto [rotX, rotY, rotZ, rotW] = rot;

		Vector3 eular = Quaternion(rotX, rotY, rotZ, rotW).ToEuler() * 57.295779513f;

		ownerPos.x = info.freezePosition[0] ? ownerPos.x : posX;
		ownerPos.y = info.freezePosition[1] ? ownerPos.y : posY;
		ownerPos.z = info.freezePosition[2] ? ownerPos.z : posZ;

		ownerEular.x = info.freezeRotation[0] ? ownerEular.x : eular.x;
		ownerEular.y = info.freezeRotation[1] ? ownerEular.y : eular.y;
		ownerEular.z = info.freezeRotation[2] ? ownerEular.z : eular.z;

		ownerPos /= 10.f;

		_owner->SetPosition(ownerPos);
		_owner->SetRotation(ownerEular);
	}
}

void TestComponent::AddForce(float* velocity, ForceMode mode)
{
}

void TestComponent::AddTorque(float* velocity, ForceMode mode)
{
}

RigidbodyInfo* TestComponent::GetInfo()
{
	return &info;
}

void TestComponent::SetLocalPosition(std::tuple<float, float, float> pos)
{
}

void TestComponent::SetRotation(std::tuple<float, float, float, float> rotation)
{
}

void TestComponent::SetIsTrigger(bool isTrigger)
{

}

bool TestComponent::GetIsTrigger()
{
	return collInfo.isTrigger;
}

Object* TestComponent::GetOwner()
{
	return _owner;
}

