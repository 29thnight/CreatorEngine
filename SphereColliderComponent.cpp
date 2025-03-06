#include "SphereColliderComponent.h"
#include "RigidbodyComponent.h"
#include "Physics/Common.h"
#include "../RenderEngine/ImGuiRegister.h"

SphereColliderComponent::SphereColliderComponent() : SphereColliderComponent(1.f)
{
}

SphereColliderComponent::SphereColliderComponent(float radius)
{
}

void SphereColliderComponent::Initialize()
{
	mShape = Physics->GetPhysics()
		->createShape(PxSphereGeometry(radius), *Physics.get()->GetDefaultMaterial(), true);
	mShape->userData = this;
	mShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
	mShape->userData = static_cast<ICollider*>(this);

	SetEnable(info.enable);
	SetIsTrigger(info.isTrigger);
	SetFilter();

	_id = 4003;

	__super::Initialize();
}

void SphereColliderComponent::FixedUpdate(float fixedTick)
{
}

void SphereColliderComponent::Update(float tick)
{
}

void SphereColliderComponent::LateUpdate(float tick)
{
}

void SphereColliderComponent::EditorContext()
{
	ImGui::Text("SphereColliderComponent");

	ImGui::Spacing();


	ColliderInfo tempInfo = info;
	if (ImGui::Button(tempInfo.enable ? "Sphere Disable" : "Sphere Enable")) {
		tempInfo.enable = !tempInfo.enable;
		SetEnable(tempInfo.enable);
	}

	ImGui::DragFloat("Radius", &radius, 0.1f, 0.f, 100.f);
	ImGui::Checkbox("IsTrigger sphere", &tempInfo.isTrigger);

	int temp = tempInfo.flag & PhysFilter::WORD1;
	bool on = temp > 0;
	if (ImGui::Checkbox("Sphere Raycast", &on)) {
		if (on)
			tempInfo.flag = PhysFilter::WORDALL;
		else
			tempInfo.flag = PhysFilter::WORDALL & ~PhysFilter::WORD1;

		info.flag = tempInfo.flag;
		SetFilter();
	}

	info = tempInfo;

	ImGui::DragInt("Sphere flag", &tempInfo.flag);

	if (ImGui::Button("Apply sphere Radius")) {
		mShape->setGeometry(PxSphereGeometry(radius));
		SetIsTrigger(info.isTrigger);
	}

	if (ImGui::Button("Delete Sphere Collider")) {
		_destroyMark = true;
	}
}

void SphereColliderComponent::Serialize(_inout json& out)
{
	json data;
	data["ColliderInfo"]["enable"] = { info.enable };
	data["ColliderInfo"]["isTrigger"] = { info.isTrigger };
	data["ColliderInfo"]["flag"] = { info.flag };
	data["ColliderInfo"]["radius"] = { radius };

	data["Type"] = MetaType<SphereColliderComponent>::type;

	out["components"].push_back(data);
}

void SphereColliderComponent::DeSerialize(_in json& in)
{
	info.enable = in["ColliderInfo"]["enable"][0].get<bool>();
	info.isTrigger = in["ColliderInfo"]["isTrigger"][0].get<bool>();
	radius = in["ColliderInfo"]["radius"][0].get<float>();
	if(!in["ColliderInfo"]["flag"][0].empty())
		info.flag = in["ColliderInfo"]["flag"][0].get<int>();
}