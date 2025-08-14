#include "RaycastHelper.h"

bool Raycast(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, HitResult& hit)
{
    RayEvent event;
    event.bUseDebugDraw = true;
    event.direction = dir;
    event.distance = distance;
    event.isStatic = false;
    event.layerMask = layerMask;
    event.origin = origin;
    RayEvent::ResultData result;
    event.resultData = &result;
    RaycastHit raycastHit;
    bool isHit = PhysicsManagers->Raycast(event, raycastHit);
    /*hit.hitObject = raycastHit.hitObject;
    hit.hitPoint = raycastHit.hitPoint;
    hit.hitNormal = raycastHit.hitNormal;
    hit.hitObjectLayer = raycastHit.hitObjectLayer;*/
	hit.gameObject = raycastHit.hitObject;
	hit.point = raycastHit.hitPoint;
	hit.normal = raycastHit.hitNormal;
	hit.layer = raycastHit.hitObjectLayer;
    return isHit;
}

int RaycastAll(Vector3 origin, Vector3 dir, float distance, unsigned int layerMask, std::vector<HitResult>& hits)
{
    RayEvent event;
    event.bUseDebugDraw = true;
    event.direction = dir;
    event.distance = distance;
    event.isStatic = false;
    event.layerMask = layerMask;
    event.origin = origin;
    RayEvent::ResultData result;
    event.resultData = &result;
    std::vector<RaycastHit> raycastHit;

    int hitSize = PhysicsManagers->Raycast(event, raycastHit);
    for (int i = 0; i < hitSize; i++) {
        HitResult hit;
        /*hit.hitObject = raycastHit[i].hitObject;
        hit.hitPoint = raycastHit[i].hitPoint;
        hit.hitNormal = raycastHit[i].hitNormal;
        hit.hitObjectLayer = raycastHit[i].hitObjectLayer;*/
		hit.gameObject = raycastHit[i].hitObject;
		hit.point = raycastHit[i].hitPoint;
		hit.normal = raycastHit[i].hitNormal;
		hit.layer = raycastHit[i].hitObjectLayer;
        hits.push_back(hit);
    }
    return hitSize;
}
