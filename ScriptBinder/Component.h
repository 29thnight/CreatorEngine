#pragma once
#include "IComponent.h"

class Object;
class Component : public IComponent
{
	Component() = default;
	virtual ~Component() = default;
	virtual void Initialize() override = 0;
	virtual void FixedUpdate(float fixedTick) override = 0;
	virtual void Update(float tick) override = 0;
	virtual void LateUpdate(float tick) override = 0;

	void SetOwner(Object* owner) { _owner = owner; }
	Object* GetOwner() const { return _owner; }

	void SetDestroy() { _destroyMark = true; }
	bool IsDestroyMark() const { return _destroyMark; }

	uint32 GetId() const { return _id; }
	auto operator<=>(const Component& other) const { return _id <=> other._id; }

	virtual uint32 ID() = 0;

protected:
	Object* _owner{};
	uint32 _id{};
	bool _destroyMark{};
};