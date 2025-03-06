//오브젝트 인터페이스
#pragma once
#include "Core.Minimal.h"
interface IObject
{
	virtual void Initialize() = 0;
	virtual void FixedUpdate(float fixedTick) = 0;
	virtual void Update(float tick) = 0;
	virtual void LateUpdate(float tick) = 0;
	virtual void Destroy() = 0;
	virtual void Serialize(_inout json& out) = 0;
	virtual void DeSerialize(_in json& in) = 0;
	virtual std::string_view GetType() = 0;
};
