#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"

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