#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "GridPass.h"
#include "GizmoPass.h"
#include "GizmoLinePass.h"
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

    void EditorView();

	void SetWireFrame() { m_buseWireFrame = !m_buseWireFrame; }

	RenderScene* m_renderScene{};
	Camera* m_pEditorCamera{};
    bool m_bShowGridSettings{ false };

private:
    void ShowGridSettings();

private:
	SceneRenderer* m_pRenderer{ nullptr };
	std::unique_ptr<GizmoPass>      m_pGizmoPass{};
	std::unique_ptr<WireFramePass>  m_pWireFramePass{};
	std::unique_ptr<GridPass>       m_pGridPass{};
	std::unique_ptr<GizmoLinePass>  m_pGizmoLinePass{};

	bool m_buseWireFrame{ false };
};
