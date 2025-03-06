#include "BoxColliderComponent.h"
#include "Physics/Common.h"
#include "../RenderEngine/ImGuiRegister.h"

BoxColliderComponent::BoxColliderComponent() : BoxColliderComponent(DirectX::SimpleMath::Vector3(.5f, .5f, .5f))
{
}

BoxColliderComponent::BoxColliderComponent(DirectX::SimpleMath::Vector3 size) : size(size)
{
	
}

void BoxColliderComponent::Initialize()
{
	mShape = Physics->GetPhysics()
		->createShape(PxBoxGeometry(PxVec3(size.x, size.y, size.z)), *Physics.get()->GetDefaultMaterial(), true);
	mShape->userData = this;
	mShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
	mShape->userData = static_cast<ICollider*>(this);

	SetEnable(info.enable);
	SetIsTrigger(info.isTrigger);
	SetFilter();

	float yaw = XMConvertToRadians(info.localRotation[1]);  // Y축 회전
	float pitch = XMConvertToRadians(info.localRotation[0]);  // X축 회전
	float roll = XMConvertToRadians(info.localRotation[2]);  // Z축 회전

	XMVECTOR additionalRotation = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
	XMFLOAT4 quatComponents;
	XMStoreFloat4(&quatComponents, additionalRotation);

	mShape->setLocalPose(PxTransform(PxVec3(info.localPosition[0], info.localPosition[1], info.localPosition[2]),
		PxQuat(quatComponents.x, quatComponents.y, quatComponents.z, quatComponents.w)));

	_id = 4000;

	__super::Initialize();
}

void BoxColliderComponent::Serialize(_inout json& out)
{
	json data;
	data["ColliderInfo"]["enable"] = { info.enable };
	data["ColliderInfo"]["isTrigger"] = { info.isTrigger };
	data["ColliderInfo"]["flag"] = { info.flag };
	data["ColliderInfo"]["size"] = { size.x, size.y, size.z };
	data["ColliderInfo"]["localpos"] = { info.localPosition[0], info.localPosition[1], info.localPosition[2] };
	data["ColliderInfo"]["localrot"] = { info.localRotation[0], info.localRotation[1], info.localRotation[2] };

	data["Type"] = MetaType<BoxColliderComponent>::type;

	out["components"].push_back(data);
}

void BoxColliderComponent::DeSerialize(_in json& in)
{
	info.enable = in["ColliderInfo"]["enable"][0].get<bool>();
	info.isTrigger = in["ColliderInfo"]["isTrigger"][0].get<bool>();
	size = Mathf::jsonToVector3(in["ColliderInfo"]["size"]);

	if (in["ColliderInfo"]["localpos"].empty()) return;
	auto pos = Mathf::jsonToVector3(in["ColliderInfo"]["localpos"]);
	auto rot = Mathf::jsonToVector3(in["ColliderInfo"]["localrot"]);

	info.localPosition[0] = pos.x;
	info.localPosition[1] = pos.y;
	info.localPosition[2] = pos.z;

	info.localRotation[0] = rot.x;
	info.localRotation[1] = rot.y;
	info.localRotation[2] = rot.z;

	if(!in["ColliderInfo"]["flag"].empty())
		info.flag = in["ColliderInfo"]["flag"][0].get<int>();
}

void BoxColliderComponent::LateUpdate(float tick)
{
}

void BoxColliderComponent::FixedUpdate(float fixedTick)
{
}

void BoxColliderComponent::Update(float tick)
{
}

void BoxColliderComponent::EditorContext()
{
	ImGui::Text("BoxColliderComponent");

	ImGui::Spacing();


	ColliderInfo tempInfo = info;
	if (ImGui::Button(tempInfo.enable ? "Box Disable" : "Box Enable")) {
		tempInfo.enable = !tempInfo.enable;
		SetEnable(tempInfo.enable);
	}

	ImGui::DragFloat3("BoxSize", &size.x, 0.1f, 0.f, 100.f);
	ImGui::DragFloat3("BoxPos", tempInfo.localPosition, 0.1f, -100.f, 100.f);
	ImGui::DragFloat3("BoxRot", tempInfo.localRotation, 0.1f, -360.f, 360.f);
	ImGui::Checkbox("IsTrigger box", &tempInfo.isTrigger);

	int temp = tempInfo.flag & PhysFilter::WORD1;
	bool on = temp > 0;
	if (ImGui::Checkbox("Box Raycast", &on)) {
		if (on)
			tempInfo.flag = PhysFilter::WORDALL;
		else
			tempInfo.flag = PhysFilter::WORDALL & ~PhysFilter::WORD1;

		info.flag = tempInfo.flag;
		SetFilter();
	}

	info = tempInfo;

	ImGui::DragInt("box flag", &tempInfo.flag);

	if (ImGui::Button("Apply box Size")) {

		float yaw = XMConvertToRadians(info.localRotation[1]);  // Y축 회전
		float pitch = XMConvertToRadians(info.localRotation[0]);  // X축 회전
		float roll = XMConvertToRadians(info.localRotation[2]);  // Z축 회전

		XMVECTOR additionalRotation = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
		XMFLOAT4 quatComponents;
		XMStoreFloat4(&quatComponents, additionalRotation);

		mShape->setGeometry(PxBoxGeometry(PxVec3(size.x, size.y, size.z)));
		mShape->setLocalPose(PxTransform(PxVec3(info.localPosition[0], info.localPosition[1], info.localPosition[2]),
			PxQuat(quatComponents.x, quatComponents.y, quatComponents.z, quatComponents.w)));
		SetIsTrigger(info.isTrigger);
	}

	if (ImGui::Button("Delete Box Collider")) {
		_destroyMark = true;
	}
}
