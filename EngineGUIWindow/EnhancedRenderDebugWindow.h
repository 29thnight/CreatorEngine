#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include "Render/Scene/EnhancedSceneRenderer.h"

// EnhancedRenderer(DX12) 렌더 디버그 창.
//
// 왜 새 창인가: 예전 "RenderPass" 창(RenderPassWindow)은 DX11 SceneRenderer의
// 패스 객체를 직접 들고 ControlPanel()을 부르는 구조였다. SceneRenderer가
// 메인 배선에서 빠지면서 Dx11Main이 그 창을 생성하지 않게 됐고, 그 순간
// ImGui 컨텍스트 "RenderPass"는 등록되지 않는 이름이 됐다. 메뉴의
// Settings > Pipeline Setting은 여전히 그 이름을 열려고 하는데,
// ImGuiRegister::GetContext는 unordered_map::operator[]라 없는 이름을
// 조용히 빈 컨텍스트로 만들어 낸다 — 그리기 함수가 없으니 창은 뜨지 않고
// 오류도 남지 않는다. 그래서 같은 이름을 DX12 내용으로 다시 등록한다.
//
// 무엇을 보여주는가: 상시 러너가 이미 재고 있으면서 아무도 읽지 않던 것들이다.
// DX12GpuProfiler가 패스마다 타임스탬프를 뜨지만 합계만 상태 문자열에 실렸고
// 패스별 수치는 매 프레임 버려졌다. 합계만으로는 "느려졌다"까지만 알 수 있다.
class EnhancedRenderDebugWindow
{
public:
	EnhancedRenderDebugWindow();
	~EnhancedRenderDebugWindow() = default;

private:
	void DrawPassSettings();

	// 패스 목록 정렬 기준. 병목을 찾을 때는 시간순, 그래프 배선을 확인할
	// 때는 선언순이라야 읽힌다 — 둘 다 필요하다.
	bool m_sortByDuration{ false };

	// 편집 중인 파라미터. 매 프레임 라이브 값으로 덮지 않는 이유는 적용이
	// 한 프레임 늦기 때문이다 — 덮으면 슬라이더를 끄는 동안 값이 계속
	// 이전 값으로 되돌아가 잡히지 않는다. 처음 한 번과 Revert에서만 읽는다.
	EnhancedLiveTuning m_editing{};
	bool               m_editingLoaded{ false };
};
#endif // !DYNAMICCPP_EXPORTS
