#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "GridPass.h"
#include "GizmoPass.h"
#include "WireFramePass.h"
#include "SpritePass.h"

class RenderPassWindow;
class SceneRenderer;
class GizmoRenderer
{
private:
	friend class RenderPassWindow;
public:
	GizmoRenderer(SceneRenderer* pRenderer);
	~GizmoRenderer();
	void OnDrawGizmos();

	void SetWireFrame() { useWireFrame = !useWireFrame; }

	RenderScene* m_renderScene{};
	Camera* m_pEditorCamera{};

private:
	std::unique_ptr<GizmoPass>      m_pGizmoPass{};
	std::unique_ptr<WireFramePass>  m_pWireFramePass{};
	std::unique_ptr<GridPass>       m_pGridPass{};

	bool useWireFrame{ false };
};