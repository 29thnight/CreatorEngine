#include "CharacterController.h"
#include "InputManager.h"
#include "RigidbodyComponent.h"
#include "Object.h"
#include "../RenderEngine/ImGuiRegister.h"
#include "Physics/Common.h"
#include "ColliderComponent.h"
#include "InteractableComponent.h"
#include <vector>

CharacterController::CharacterController()
{
}

CharacterController::~CharacterController()
{
	controller->release();
}

void CharacterController::Initialize()
{
	auto position = _owner->GetLocalPosition();
	position *= 10.f;

	physx::PxCapsuleControllerDesc controllerDesc;
	controllerDesc.position = physx::PxExtendedVec3(position.x, position.y, position.z);
	controllerDesc.radius = attribute.radius;
	controllerDesc.height = attribute.height;
	controllerDesc.stepOffset = attribute.stepOffset;
	controllerDesc.slopeLimit = attribute.slopeLimit;
	controllerDesc.contactOffset = attribute.contactOffset;
	controllerDesc.maxJumpHeight = attribute.maxJumpHeight;
	controllerDesc.material = Physics->GetDefaultMaterial();
	controllerDesc.reportCallback = &contactCallback;
	controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;
	controllerDesc.userData = this;
	controllerDesc.scaleCoeff = 1.f;

	controller = (physx::PxCapsuleController*)Physics->GetControllerManager()->createController(controllerDesc);
	controller->getActor()->userData = static_cast<IRigidbody*>(this);
	auto actor = controller->getActor();
	uint32_t shapeCount = actor->getNbShapes();
	physx::PxShape** shapes = new physx::PxShape * [shapeCount];
	actor->getShapes(shapes, shapeCount);

	info.isCharacterController = true;

	for (uint32_t i = 0; i < shapeCount; ++i) {
		shapes[i]->userData = static_cast<ICollider*>(this);
		PxFilterData filterData;
		filterData.word0 = PhysFilter::WORDALL;
		shapes[i]->setQueryFilterData(filterData);
	}

	_id = 4004;
}

void CharacterController::FixedUpdate(float fixedTick)
{
	lockTime -= fixedTick;

	if (lockTime <= 0.f) {

		if (pow(direction.x, 2) + pow(direction.y, 2) > 0.04f) {
			auto tempdir = direction;
			tempdir.Normalize();
			float angle = atan2f(tempdir.x, tempdir.y);
			float currentAngle = _owner->GetLocalRotation().y;
			float angleInDegrees = angle * (180.0f / 3.141592f);

			float angleDiff = angleInDegrees - currentAngle;
			if (angleDiff > 180.0f) {
				angleDiff -= 360.0f;  // 360도 이상 차이나면, -360을 더해서 -180 ~ 180 범위로 만들기
			}
			else if (angleDiff < -180.0f) {
				angleDiff += 360.0f;  // -180도 이하 차이나면, +360을 더해서 -180 ~ 180 범위로 만들기
			}
			float rotationStep = rotationSpeed * fixedTick;  // deltaTime은 프레임 시간

			// 각도 차이가 회전 속도보다 크면, 회전 속도만큼 회전
			if (fabs(angleDiff) > rotationStep) {
				currentAngle += rotationStep * (angleDiff > 0 ? 1 : -1);
			}
			else {
				currentAngle = angleInDegrees;  // 목표 회전값에 도달하면 바로 설정
			}

			_owner->SetRotation({ 0.f, currentAngle, 0.f });

			currDirection = tempdir;
		}

		otherhit = contactCallback.GetOtherController();

		PxVec3 surfaceNormal = contactCallback.groundNormal;  // 표면 법선 벡터 (충돌 콜백에서 얻을 수 있음)
		PxVec3 moveDirection = PxVec3(direction.x, 0.f, direction.y);  // XZ 평면에서의 이동 벡터

		// 법선 벡터와 이동 벡터의 수직 성분을 구하기 위해 법선 벡터와의 내적을 뺍니다.
		PxVec3 flatMoveDirection = moveDirection - surfaceNormal * surfaceNormal.dot(moveDirection);
		flatMoveDirection.normalize();  // 평면에서 수평으로 정규화된 이동 벡터

		// 속도를 표면의 법선에 맞게 수정합니다.
		flatMoveDirection *= moveSpeed;  // 이동 속도 적용

		// 새로운 이동 벡터를 velocity에 반영합니다.
		if (contactCallback.isGround) {
			velocity += flatMoveDirection;

			contactCallback.isGround = false;
		}
		else {
			velocity.x += flatMoveDirection.x;
			velocity.z += flatMoveDirection.z;
		}
	}

	if (addForceChangeVelocityflag) {
		addForceChangeVelocityflag = false;
		velocity = changeVelocity;
	}

	if (addForceLockWhenisGround) {
		addForceLockWhenisGround = false;
 		velocity += changeLockVelocity;
	}


	velocity.y += gravity * fixedTick;
	PxVec3 displacement = velocity * fixedTick;

	if (velocity.y > maxJumpVelocity) {
		velocity.y = maxJumpVelocity;
	}

	PxControllerCollisionFlags collisionFlags = controller->move(displacement, 0.001f, fixedTick, physx::PxControllerFilters());

	if (collisionFlags & PxControllerCollisionFlag::eCOLLISION_SIDES) {
		velocity.x = 0.f; // 벽 충돌 시 X 속도 제거
		velocity.z = 0.f;
	}

	if (collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) {
		velocity.y = 0; // 바닥과 충돌 시 Y속도 초기화
		//addForceLockWhenisGround = false;
		float friction = 0.1f;
		velocity.x *= friction;
		velocity.z *= friction;	
	}
	else {
		//float airFriction = 0.5f;
		if (lockTime <= 0.f) {
			velocity.x *= airFriction;
			velocity.z *= airFriction;
		}
	}
	direction = { 0.f, 0.f };
}

void CharacterController::Update(float tick)
{
}

void CharacterController::LateUpdate(float tick)
{
}

void CharacterController::EditorContext()
{
	ImGui::Text("CharacterController");

	ImGui::Spacing();

	auto tempSpeed = moveSpeed;
	auto tempJump = jumpVelocity;
	auto tempDir = direction;
	auto tempGravity = gravity;
	auto temprotationSpeed = rotationSpeed;

	ImGui::DragFloat("MoveSpeed", &tempSpeed, 10.f, 0.f, 10000.f);
	ImGui::DragFloat("JumpVelocity", &tempJump, 10.f, 0.f, 10000.f);
	ImGui::DragFloat2("originDir", &tempDir.x, 0.05f, -360.f, 360.f);
	ImGui::DragFloat("Graviry", &tempGravity, 0.1f, 100.f, -100.f);
	ImGui::DragFloat("rotationSpeed", &temprotationSpeed, 0.1f, 100.f, -100.f);
	ImGui::DragFloat("AirFriction", &airFriction, 0.1f, 0.f, 1.f);

	moveSpeed = tempSpeed;
	jumpVelocity = tempJump;
	direction = tempDir;
	gravity = tempGravity;
	rotationSpeed = temprotationSpeed;

	auto tempAttribute = attribute;
	ImGui::DragFloat("radius", &tempAttribute.radius, 0.1f, 0.f, 10.f);
	ImGui::DragFloat("height", &tempAttribute.height, 0.1f, 0.f, 10.f);
	ImGui::DragFloat("stepOffset", &tempAttribute.stepOffset, 0.1f, 0.f, 10.f);
	ImGui::DragFloat("slopeLimit", &tempAttribute.slopeLimit, 0.1f, 0.f, 1.f);
	ImGui::DragFloat("contactOffset", &tempAttribute.contactOffset, 0.01f, 0.001f, 10.f);
	ImGui::DragFloat("maxJumpHeight", &tempAttribute.maxJumpHeight, 0.1f, 0.f, 10.f);
	attribute = tempAttribute;

	if (ImGui::Button("Apply Attribute")) {
		controller->setRadius(attribute.radius);
		controller->setHeight(attribute.height);
		controller->setStepOffset(attribute.stepOffset);
		controller->setSlopeLimit(attribute.slopeLimit);
		controller->setContactOffset(attribute.contactOffset);
	}

	if (ImGui::Button("controller SetPosition!")) {
		controller->setPosition(PxExtendedVec3(forceMovePosition[0], forceMovePosition[1], forceMovePosition[2]));
	}
	ImGui::DragFloat3("ForceMovePos", forceMovePosition, 0.1f);
}

void CharacterController::Serialize(_inout json& out)
{
	json data;

	data["CharacterController"]["moveSpeed"] = { moveSpeed };
	data["CharacterController"]["jumpVelocity"] = { jumpVelocity };
	data["CharacterController"]["gravity"] = { gravity };
	data["CharacterController"]["rotationSpeed"] = { rotationSpeed };

	data["CharacterController"]["radius"] =			{ attribute.radius };
	data["CharacterController"]["height"] =			{ attribute.height };
	data["CharacterController"]["stepOffset"] =		{ attribute.stepOffset };
	data["CharacterController"]["slopeLimit"] =		{ attribute.slopeLimit };
	data["CharacterController"]["contactOffset"] =	{ attribute.contactOffset };
	data["CharacterController"]["maxJumpHeight"] =	{ attribute.maxJumpHeight };

	data["Type"] = MetaType<CharacterController>::type;

	out["components"].push_back(data);
}

void CharacterController::DeSerialize(_in json& in)
{
	moveSpeed = in["CharacterController"]["moveSpeed"][0].get<float>();
	jumpVelocity = in["CharacterController"]["jumpVelocity"][0].get<float>();
	gravity = in["CharacterController"]["gravity"][0].get<float>();
	rotationSpeed = in["CharacterController"]["rotationSpeed"][0].get<float>();

	attribute.radius		 = in["CharacterController"]["radius"][0].get<float>();
	attribute.height 		 = in["CharacterController"]["height"][0].get<float>();
	attribute.stepOffset 	 = in["CharacterController"]["stepOffset"][0].get<float>();
	attribute.slopeLimit 	 = in["CharacterController"]["slopeLimit"][0].get<float>();
	attribute.contactOffset	 = in["CharacterController"]["contactOffset"][0].get<float>();
	attribute.maxJumpHeight	 = in["CharacterController"]["maxJumpHeight"][0].get<float>();
}

void CharacterController::Jump()
{
	//if(contactCallback.isGround)
	//	jumpflag = true;
}

void CharacterController::AddForceChangeVelocity(float velocity[3])
{
	addForceChangeVelocityflag = true;
	changeVelocity.x = velocity[0];
	changeVelocity.y = velocity[1];
	changeVelocity.z = velocity[2];
}

void CharacterController::AddForceChangeVelocityWhenisGround(float velocity[3])
{
	addForceLockWhenisGround = true;
	lockTime = maxLockTime;
	changeLockVelocity.x = velocity[0];
	changeLockVelocity.y = velocity[1];
	changeLockVelocity.z = velocity[2];
}

void CharacterController::AddForceChangeVelocityWhenisGround(float velocity[3], float time)
{
	addForceLockWhenisGround = true;
	lockTime = time;
	changeLockVelocity.x = velocity[0];
	changeLockVelocity.y = velocity[1];
	changeLockVelocity.z = velocity[2];
}

void CharacterController::OnTriggerEnter(ICollider* other)
{
}

void CharacterController::OnTriggerStay(ICollider* other)
{
}

void CharacterController::OnTriggerExit(ICollider* other)
{
}

void CharacterController::OnCollisionEnter(ICollider* other)
{
}

void CharacterController::OnCollisionStay(ICollider* other)
{
}

void CharacterController::OnCollisionExit(ICollider* other)
{
}

std::tuple<float, float, float> CharacterController::GetWorldPosition()
{
	auto worldMat = _owner->GetWorldMatrix();
	Vector3 position(worldMat.Translation());
	return std::make_tuple(position.x, position.y, position.z);
}

std::tuple<float, float, float, float> CharacterController::GetWorldRotation()
{
	auto worldMat = _owner->GetWorldMatrix();
	Quaternion rotation = Quaternion::CreateFromRotationMatrix(worldMat);
	return std::make_tuple(rotation.x, rotation.y, rotation.z, rotation.w);
}

void CharacterController::SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot)
{
	auto p = controller->getPosition();
	p /= 10.f;
	_owner->SetPosition(Vector3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));
}

void CharacterController::AddForce(float* velocity, ForceMode mode)
{
}

void CharacterController::AddTorque(float* velocity, ForceMode mode)
{
}

RigidbodyInfo* CharacterController::GetInfo()
{
	return &info;
}

void CharacterController::SetLocalPosition(std::tuple<float, float, float> pos)
{
}

void CharacterController::SetRotation(std::tuple<float, float, float, float> rotation)
{
}

void CharacterController::SetIsTrigger(bool isTrigger)
{
}

bool CharacterController::GetIsTrigger()
{
	return collInfo.isTrigger;
}

Object* CharacterController::GetOwner()
{
	return _owner;
}
