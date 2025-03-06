#include "RigidbodyComponent.h"
#include "Physics/Common.h"
#include "Object.h"
#include "ColliderComponent.h"
#include "../RenderEngine/ImGuiRegister.h"

using namespace DirectX::SimpleMath;

RigidbodyComponent::RigidbodyComponent()
{
}

RigidbodyComponent::~RigidbodyComponent()
{
	auto coll = _owner->GetComponents<ColliderComponent>();
	for (auto& c : coll) {
		auto actor = rbActor->is<PxRigidDynamic>();
		actor->detachShape(*c->mShape);
		c->rigidbody = nullptr;
	}
	Physics->GetPxScene()->removeActor(*rbActor);
	rbActor->release();
	rbActor = nullptr;
}

void RigidbodyComponent::Initialize()
{
	auto coll = _owner->GetComponents<ColliderComponent>();
	for (auto& c : coll) {
		CreateActor(c);
	}

	_id = 4001;
}

void RigidbodyComponent::Update(float tick)
{
}

void RigidbodyComponent::EditorContext()
{
	ImGui::Text("RigidbodyComponent");

	ImGui::Spacing();

	RigidbodyInfo tempInfo = info;

	ImGui::DragFloat("Mass", &tempInfo.mass, 0.1f);
	ImGui::DragFloat("drag", &tempInfo.drag, 0.1f);
	ImGui::DragFloat("angularDrag", &tempInfo.angularDrag, 0.1f);
	ImGui::DragFloat3("centerOfMass", tempInfo.centerOfMass, 0.1f);
	ImGui::Checkbox("freezePosition", tempInfo.freezePosition);
	ImGui::SameLine();
	ImGui::Checkbox("fpY", &tempInfo.freezePosition[1]);
	ImGui::SameLine();
	ImGui::Checkbox("fpZ", &tempInfo.freezePosition[2]);

	ImGui::Checkbox("freezeRotation", tempInfo.freezeRotation);
	ImGui::SameLine();
	ImGui::Checkbox("frY", &tempInfo.freezeRotation[1]);
	ImGui::SameLine();
	ImGui::Checkbox("frZ", &tempInfo.freezeRotation[2]);

	ImGui::Checkbox("useGravity", &tempInfo.useGravity);
	ImGui::Checkbox("isKinematic", &tempInfo.isKinematic);

	info = tempInfo;

	if (ImGui::Button("Test AddForce : Up")) {
		float velocity[3] = { 0.f, 2.f, 0.f };
		info.isKinematic = false;
		info.useGravity = true;

		auto act = static_cast<PxRigidDynamic*>(rbActor);

		rbActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !info.useGravity);
		act->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, info.isKinematic);

		AddForce(velocity, ForceMode::IMPULSE);
	}
	if (ImGui::Button("Test AddTorque : Front")) {
		float velocity[3] = { 0.f, 0.f, 20.f };
		AddTorque(velocity, ForceMode::IMPULSE);
	}
}

void RigidbodyComponent::Serialize(_inout json& out)
{
	json data;
	data["RigidbodyInfo"]["mass"] = { info.mass };
	data["RigidbodyInfo"]["drag"] = { info.drag };
	data["RigidbodyInfo"]["angularDrag"] = { info.angularDrag };
	data["RigidbodyInfo"]["centerOfMass"] = { info.centerOfMass[0], info.centerOfMass[1], info.centerOfMass[2] };
	data["RigidbodyInfo"]["freezePosition"] = { info.freezePosition[0], info.freezePosition[1], info.freezePosition[2] };
	data["RigidbodyInfo"]["freezeRotation"] = { info.freezeRotation[0], info.freezeRotation[1], info.freezeRotation[2] };
	data["RigidbodyInfo"]["useGravity"] = { info.useGravity };
	data["RigidbodyInfo"]["isKinematic"] = { info.isKinematic };


	data["Type"] = MetaType<RigidbodyComponent>::type;

	out["components"].push_back(data);
}

void RigidbodyComponent::DeSerialize(_in json& in)
{
	info.mass = in["RigidbodyInfo"]["mass"][0].get<float>();
	info.drag = in["RigidbodyInfo"]["drag"][0].get<float>();
	info.angularDrag = in["RigidbodyInfo"]["angularDrag"][0].get<float>();

	Vector3 cOm = Mathf::jsonToVector3(in["RigidbodyInfo"]["centerOfMass"]);
	Vector3 fP  = Mathf::jsonToVector3(in["RigidbodyInfo"]["freezePosition"]);
	Vector3 fR  = Mathf::jsonToVector3(in["RigidbodyInfo"]["freezeRotation"]);

	info.centerOfMass[0] = cOm.x;
	info.centerOfMass[1] = cOm.y;
	info.centerOfMass[2] = cOm.z;
	info.freezePosition[0] = fP.x;
	info.freezePosition[1] = fP.y;
	info.freezePosition[2] = fP.z;
	info.freezeRotation[0] = fR.x;
	info.freezeRotation[1] = fR.y;
	info.freezeRotation[2] = fR.z;

	info.useGravity = in["RigidbodyInfo"]["useGravity"][0].get<bool>();
	info.isKinematic = in["RigidbodyInfo"]["isKinematic"][0].get<bool>();
}

void RigidbodyComponent::SetCollider(ColliderComponent* collider)
{
	auto coll = collider;
	CreateActor(coll);
}


std::tuple<float, float, float> RigidbodyComponent::GetWorldPosition()
{
	auto worldMat = _owner->GetWorldMatrix();
	Vector3 position(worldMat.Translation());
	return std::make_tuple(position.x, position.y, position.z);
}

std::tuple<float, float, float, float> RigidbodyComponent::GetWorldRotation()
{
	auto worldMat = _owner->GetWorldMatrix();
	Quaternion rotation = Quaternion::CreateFromRotationMatrix(worldMat);
	return std::make_tuple(rotation.x, rotation.y, rotation.z, rotation.w);
}

void RigidbodyComponent::SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot)
{
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

void RigidbodyComponent::AddForce(float* velocity, ForceMode mode)
{
	auto actor = static_cast<PxRigidDynamic*>(rbActor);
	if (actor != nullptr) {
		actor->addForce({ velocity[0], velocity[1], velocity[2] }, physx::PxForceMode::Enum(mode));
	}
}

void RigidbodyComponent::AddTorque(float* velocity, ForceMode mode)
{
	auto actor = static_cast<PxRigidDynamic*>(rbActor);
	if (actor != nullptr) {
		actor->addTorque({ velocity[0], velocity[1], velocity[2] }, physx::PxForceMode::Enum(mode));
	}
}

void RigidbodyComponent::CreateActor(ColliderComponent* collider)
{
	// 콜라이더가 없음.
	if (collider == nullptr) {
		return;
	}
	collider->rigidbody = this;

	if (rbActor == nullptr) {

		auto ownerMat = _owner->GetWorldMatrix();
		Vector3 ownerPos;
		Quaternion ownerRot;
		Vector3 ownerScale;
		ownerMat.Decompose(ownerScale, ownerRot, ownerPos);

		std::cout << "pos : " << ownerPos.x << ", " << ownerPos.y << ", " << ownerPos.z << std::endl;
		//ownerPos /= 10.f;

		auto actor = Physics->GetPhysics()->createRigidDynamic(
			PxTransform(
				PxVec3(ownerPos.x, ownerPos.y, ownerPos.z),
				PxQuat(ownerRot.x, ownerRot.y, ownerRot.z, ownerRot.w)
			));


		rbActor = actor;
		rbActor->userData = static_cast<IRigidbody*>(this);
		Physics->AddActor(rbActor);

		actor->setMass(info.mass);
		actor->setLinearDamping(info.drag);
		actor->setAngularDamping(info.angularDrag);
		actor->setCMassLocalPose(PxTransform(info.centerOfMass[0], info.centerOfMass[1], info.centerOfMass[2]));
		actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !info.useGravity);
		actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, info.isKinematic);

		/*rbActor->setGlobalPose(PxTransform(
			PxVec3(ownerPos.x, ownerPos.y, ownerPos.z),
			PxQuat(ownerRot.x, ownerRot.y, ownerRot.z, ownerRot.w)
		));*/
	}

	rbActor->attachShape(*collider->mShape);
}

void RigidbodyComponent::FixedUpdate(float fixedTick)
{
}

void RigidbodyComponent::LateUpdate(float tick)
{
}

RigidbodyInfo* RigidbodyComponent::GetInfo()
{
	return &info;
}




