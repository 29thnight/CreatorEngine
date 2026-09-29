#pragma once
// 등록 함수(reflgen::generated::register_<모듈>)의 번역 단위가 맨 앞에 include 하는 엔진 header — Directory.Build.targets
// 의 ReflgenRegistrationHeaders. 세 모듈(RenderEngine·SceneRuntime·Editor)의 등록 번역 단위가 모두 include 하므로 셋이 다
// 여는 자리(RenderEngine)에 둔다.
//
// 런타임 서술자(Meta::Types 의 type_descriptor)는 그 번역 단위에서 만들어진다. 서술자의 쓰기·읽기 썽크와 필드 판정은
// 엔진의 다른 번역 단위와 같은 것을 보아야 한다 — 다르면 같은 타입의 서술이 번역 단위마다 갈린다(ODR 위반). 그 번역 단위가
// 보는 것은 모듈의 반영 header 들뿐이라, 거기서 모자라는 것을 여기서 채운다:
//
//   ① 엔진 serializer 특수화(ReflectionTypedYml.h → ReflgenAuthoringSerializers.h) — 봉투·enum·math·포인터 규칙.
//   ② 반영 header 가 전방 선언만 하는 필드 타입의 정의 — 서술된 타입이 여기서 불완전하면 그 서술(생성된 describe())이
//      실체화되며 컴파일이 멈춘다. 새로 생기면 그 타입의 header 를 여기에 더한다.
#include "ReflectionTypedYml.h"
#include "Material.h" // MeshRenderer::m_Material(std::shared_ptr<Material>) — MeshRenderer.h 는 전방 선언만 한다
