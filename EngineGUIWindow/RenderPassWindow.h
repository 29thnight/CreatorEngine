#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

// LEGACY DEAD CODE: DX11 SceneRenderer 패스 조작 창. Dx11Main은 이 창을
// 생성하지 않으며 EnhancedRenderer의 UI 표면으로 사용하지 않는다.
class SceneRenderer;
class GizmoRenderer;
class RenderPassWindow
{
public:
	RenderPassWindow(SceneRenderer* ptr, GizmoRenderer* gizmo_ptr);
	~RenderPassWindow() = default;

	SceneRenderer* m_sceneRenderer{ nullptr };
	GizmoRenderer* m_gizmoRenderer{ nullptr };
};
#endif // !DYNAMICCPP_EXPORTS
