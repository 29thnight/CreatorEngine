#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class ProjectionType
{
	Perspective,
	Orthographic,
};

enum class RenderPipelinePass : flag
{
	ShadowPass = 0,
	GBufferPass = 1,
	SSAOPass = 2,
	DeferredPass = 3,
	SkyBoxPass = 4,
	ToneMapPass = 5,
	SpritePass = 6,
	WireFramePass = 7,
	GridPass = 8,
	BlitPass = 9,
	TerrainGizmoPass = 10,
	AutoExposurePass = 11,
};

struct ApplyRenderPipelinePass
{
	bool m_ShadowPass{ true };
	bool m_GBufferPass{ true };
	bool m_SSAOPass{ true };
	bool m_DeferredPass{ true };
	bool m_SkyBoxPass{ true };
	bool m_ToneMapPass{ true };
	bool m_SpritePass{ true };
	bool m_WireFramePass{ true };
	bool m_GridPass{ false };
	bool m_BlitPass{ false };
	bool m_TerrainGizmoPass{ false };
	bool m_autoExposurePass{ true };
};