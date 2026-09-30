#pragma once
#include "ImGui.h"
#include "Render/Scene/EnhancedSceneRenderer.h"

namespace editor
{
	void DrawRenderRuntime(const EnhancedLiveDebugSnapshot& snapshot);
	void DrawRenderLiveDiagnostics();
	void OpenRenderLiveDiagnostics();
}

// RenderPass shows the active pipeline declaration until the RG-V viewer exists.
// Project Settings owns live tuning; Profiler Rendering - Live owns diagnostics.
// Both DX12 and Vulkan publish through the same renderer snapshot interface.
class EnhancedRenderDebugWindow
{
public:
	void Draw();
	void DrawPassSettings();
	~EnhancedRenderDebugWindow() = default;

private:

	bool m_sortByDuration{ false };

	// 편집 중인 파라미터. 매 프레임 라이브 값으로 덮지 않는 이유는 적용이
	// 한 프레임 늦기 때문이다 — 덮으면 슬라이더를 끄는 동안 값이 계속
	// 이전 값으로 되돌아가 잡히지 않는다. 처음 한 번과 Revert에서만 읽는다.
	EnhancedLiveTuning m_editing{};
	bool               m_editingLoaded{ false };
};
