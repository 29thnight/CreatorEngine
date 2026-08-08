// 렌더 측 광원 프록시 로직. 컴포넌트를 읽는 생성자는
// ScriptBinder/PrimitiveProxyBridge.cpp에 있다 — 프록시 "타입"은 렌더
// 소유, 프록시 "생성"(컴포넌트 -> 프록시 변환)은 게임플레이 소유가
// 경계 원칙이다(PHASE 4-2 C1).
#include "LightRenderProxy.h"
#include "RenderScene.h"

void LightRenderProxy::DestroyProxy()
{
	m_isExpired = true;
	// 회수 전에 스냅샷을 뜬 프레임이 이 광원을 그리지 않도록 함께 끈다.
	// 프리미티브 쪽에서 태그를 Expired로 바꾸는 것과 같은 취지다.
	m_lightStatus = LightStatus::Disabled;
	RenderScene::RegisteredDestroyLightProxyGUIDs.push(m_instancedID);
}
