#pragma once
#include "Component.h"
#include "ILifeSycle.h"

//모든 2d이미지 기본?
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

