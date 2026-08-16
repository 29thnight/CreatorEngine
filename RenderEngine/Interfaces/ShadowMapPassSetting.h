#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ShadowMapPassSetting
{
   // CT4 파일럿 — 매크로가 멤버 선언 앞에 오는 배치 케이스(CtReflect가
   // 함수라 complete-class 문맥에서 멤버 포인터가 형성된다).
   ReflectionMetaField(ShadowMapPassSetting,
       ct_property(useCascade),
       ct_property(isCloudOn),
       ct_property(cloudSize),
       ct_property(cloudDirection),
       ct_property(cloudMoveSpeed),
       ct_property(cloudAlpha),
       ct_property(epsilon))

    ShadowMapPassSetting() = default;

    bool useCascade{ true };
    bool isCloudOn{ true };
    Mathf::Vector2 cloudSize{ 7.f, 7.f };
    Mathf::Vector2 cloudDirection{ 1.f, 1.f };
    float cloudMoveSpeed{ 0.00006f };
	float cloudAlpha{ 1.f };
    float epsilon{ 0.001f };
};
