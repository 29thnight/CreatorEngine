#pragma once
#include "ColliderComponent.h"

class BoxColliderComponent : public ColliderComponent
{
public:
	BoxColliderComponent();
	BoxColliderComponent(DirectX::SimpleMath::Vector3 size);
	~BoxColliderComponent() {}
	// ColliderComponent을(를) 통해 상속됨
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;

	// ColliderComponent을(를) 통해 상속됨
	void Initialize() override;

	void Serialize(_inout json& out) override;
	void DeSerialize(_in json& in) override;

	uint32 ID() override { return _ID; }

public:
	DirectX::SimpleMath::Vector3 size;

private:
	static constexpr uint32 _ID = 4003;
};

template<>
struct MetaType<BoxColliderComponent>
{
	static constexpr std::string_view type{ "BoxColliderComponent" };
};