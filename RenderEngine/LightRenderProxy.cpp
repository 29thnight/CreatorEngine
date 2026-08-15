// 렌더 측 광원 프록시 로직. 컴포넌트를 읽는 생성자는
// ScriptBinder/PrimitiveProxyBridge.cpp에 있다 — 프록시 "타입"은 렌더
// 소유, 프록시 "생성"(컴포넌트 -> 프록시 변환)은 게임플레이 소유가
// 경계 원칙이다(PHASE 4-2 C1).
#include "LightRenderProxy.h"
