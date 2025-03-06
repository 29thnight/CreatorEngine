#include "StairColliderComponent.h"
#include "RigidbodyComponent.h"
#include "Physics/Common.h"
#include "../RenderEngine/ImGuiRegister.h"

StairColliderComponent::StairColliderComponent() : StairColliderComponent(1.f)
{
}

StairColliderComponent::StairColliderComponent(float size)
{
}

StairColliderComponent::~StairColliderComponent()
{
	convexMesh->release();
}

void StairColliderComponent::Initialize()
{
	PxVec3 vertices[] = {
		{-0.5, 0.5, -0.5},
		{0.5, 0.5, -0.5},
		{-0.5, -0.5, 0.5},
		{0.5, -0.5, 0.5},
		{-0.5, -0.5, -0.5},
		{0.5, -0.5, -0.5}
	};

	PxConvexMeshDesc convexDesc;
	convexDesc.points.count = 6; // 정점 개수
	convexDesc.points.stride = sizeof(PxVec3);
	convexDesc.points.data = vertices;
	convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX; // 볼록 메시 자동 생성


	PxTolerancesScale scale;
	PxCookingParams params(scale);

	PxDefaultMemoryOutputStream buf;
	PxConvexMeshCookingResult::Enum result;
	if (!PxCookConvexMesh(params, convexDesc, buf, &result))
		return;
	PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
	convexMesh = Physics->GetPhysics()->createConvexMesh(input);

	
	mShape = Physics->GetPhysics()
		->createShape(PxConvexMeshGeometry(convexMesh), *Physics.get()->GetDefaultMaterial(), true);
	mShape->userData = this;
	mShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
	mShape->userData = static_cast<ICollider*>(this);

	SetEnable(info.enable);
	SetIsTrigger(info.isTrigger);
	SetFilter();

	_id = 4004;

	__super::Initialize();
}

void StairColliderComponent::FixedUpdate(float fixedTick)
{
}

void StairColliderComponent::Update(float tick)
{
}

void StairColliderComponent::LateUpdate(float tick)
{
}

void StairColliderComponent::EditorContext()
{
	ImGui::Text("StairColliderComponent");

	ImGui::Spacing();


	ColliderInfo tempInfo = info;
	if (ImGui::Button(tempInfo.enable ? "Stair Disable" : "Stair Enable")) {
		tempInfo.enable = !tempInfo.enable;
		SetEnable(tempInfo.enable);
	}

	ImGui::DragFloat("size", &size, 0.1f, 0.f, 100.f);
	ImGui::Checkbox("IsTrigger stair", &tempInfo.isTrigger);

	int temp = tempInfo.flag & PhysFilter::WORD1;
	bool on = temp > 0;
	if (ImGui::Checkbox("Stair Raycast", &on)) {
		if (on)
			tempInfo.flag = PhysFilter::WORDALL;
		else
			tempInfo.flag = PhysFilter::WORDALL & ~PhysFilter::WORD1;

		info.flag = tempInfo.flag;
		SetFilter();
	}

	info = tempInfo;

	ImGui::DragInt("box flag", &tempInfo.flag);

	if (ImGui::Button("Apply stair trigger")) {
		//mShape->setGeometry(PxSphereGeometry(radius));
		SetIsTrigger(info.isTrigger);
	}

	if (ImGui::Button("Delete Stair Collider")) {
		_destroyMark = true;
	}
}

void StairColliderComponent::Serialize(_inout json& out)
{
	json data;
	data["ColliderInfo"]["enable"] = { info.enable };
	data["ColliderInfo"]["isTrigger"] = { info.isTrigger };
	data["ColliderInfo"]["flag"] = { info.flag };
	data["ColliderInfo"]["size"] = { size };

	data["Type"] = MetaType<StairColliderComponent>::type;

	out["components"].push_back(data);
}

void StairColliderComponent::DeSerialize(_in json& in)
{
	info.enable = in["ColliderInfo"]["enable"][0].get<bool>();
	info.isTrigger = in["ColliderInfo"]["isTrigger"][0].get<bool>();
	size = in["ColliderInfo"]["size"][0].get<float>();
	if (!in["ColliderInfo"]["flag"][0].empty())
		info.flag = in["ColliderInfo"]["flag"][0].get<int>();
}
