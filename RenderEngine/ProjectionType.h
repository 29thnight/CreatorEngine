#pragma once
#include "Core.Minimal.h"

enum class ProjectionType
{
	Perspective,
	Orthographic,
};
AUTO_REGISTER_ENUM(ProjectionType)

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
};