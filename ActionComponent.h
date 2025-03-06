#pragma once
#include "Component.h"
#include "ObjectTypeMeta.h"

class ActionComponent : public Component
{
public:
	// Component을(를) 통해 상속됨
	void Initialize() override;
	void Update(float tick) override;
	void EditorContext() override;

	// Component을(를) 통해 상속됨
	void FixedUpdate(float fixedTick) override;
	void LateUpdate(float tick) override;

	virtual void Serialize(_inout json& out) override final;
	virtual void DeSerialize(_in json& in) override final;

	uint32 ID() override { return _ID; }

private:
	static constexpr uint32 _ID{ 5000 };
};

template<>
struct MetaType<ActionComponent>
{
	static constexpr std::string_view type{ "ActionComponent" };
};

