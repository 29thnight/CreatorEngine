#pragma once
#include "Core.Mathf.h"

class GameObject;
//struct HitResult {
//	GameObject* hitObject = nullptr;
//	DirectX::SimpleMath::Vector3 hitPoint{};
//	DirectX::SimpleMath::Vector3 hitNormal{};
//	unsigned int hitObjectLayer = 0;
//};

using namespace Mathf;
extern bool Raycast(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, HitResult& hit);
extern int RaycastAll(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, std::vector<HitResult>& hits);



