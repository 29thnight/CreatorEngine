#pragma once
#include "ColliderComponent.h"

class PxConvexMesh;
class StairColliderComponent : public ColliderComponent
{
public:
	StairColliderComponent();
	StairColliderComponent(float size);
	~StairColliderComponent();
	// ColliderComponent을(를) 통해 상속됨
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;
	void Serialize(_inout json& out) override;
	void DeSerialize(_in json& in) override;

	uint32 ID() override { return _ID; }

public:
	float size = 1.f;

private:
	static constexpr uint32 _ID = 4004;
	physx::PxConvexMesh* convexMesh = nullptr;
};

template<>
struct MetaType<StairColliderComponent>
{
	static constexpr std::string_view type{ "StairColliderComponent" };
};