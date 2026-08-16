#pragma once
// CT4~CT5 이행기의 명시 등록 리스트 (PHASE 18).
//
// 명시 메타(ReflectionMetaField*)로 이전한 타입은 Serializable 어노테이션이
// 없으므로 생성기(MetaGenerator)의 RegisterReflect.def 스캔에서 빠진다 —
// 등록은 여기서 한다. CT5가 끝나면 이 리스트가 등록의 유일 정본이 되고
// .def는 은퇴한다(계획 CT5 항목). 누락은 K1-b 기동 검사(씬에 있는데 등록
// 안 됨)가 잡는다.
#include "Reflection.hpp"
#include "MeshRenderer.h"
#include "BoxColliderComponent.h"
#include "LightMapping.h"
#include "ShadowMapPassSetting.h"

inline void RegisterReflectManual()
{
    AUTO_REGISTER_CLASS(MeshRenderer);
    AUTO_REGISTER_CLASS(BoxColliderComponent);
    AUTO_REGISTER_CLASS(LightMapping);
    AUTO_REGISTER_CLASS(ShadowMapPassSetting);
}
