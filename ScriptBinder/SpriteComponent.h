#pragma once
#include "Component.h"
#include "ILifeSycle.h"

//��� 2d�̹��� �⺻?
class SpriteComponent : public Component,public ILifeSycle
{
public:
	SpriteComponent();
	~SpriteComponent() = default;

	virtual void Start();
	virtual void Update(float tick);
	virtual void FixedUpdate(float fixedTick);
	virtual void LateUpdate(float tick);
};

