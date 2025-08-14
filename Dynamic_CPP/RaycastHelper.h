#pragma once
#include "Core.Mathf.h"
#include "PhysicsManager.h"
class GameObject;

using namespace Mathf;
extern bool Raycast(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, HitResult& hit);
extern int RaycastAll(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, std::vector<HitResult>& hits);



