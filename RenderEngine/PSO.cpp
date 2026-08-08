#ifndef DYNAMICCPP_EXPORTS
#include "PSO.h"
#include "ShaderSystem.h"

// ★ PSOHelper(VSSetShader 계열 스테이지 디스패치)와 상태 객체 관리를 걷었다 (M1).
//   전부 PipelineStateObject::Apply만 쓰던 것이고, 그 Apply의 호출자가 0이었다.

PipelineStateObject::PipelineStateObject()
{
	m_shaderReloadEventHandle = ShaderSystem->m_shaderReloadedDelegate.AddLambda([&]()
	{
		ReloadShaders();
	});
}

PipelineStateObject::PipelineStateObject(bool isShaderPSO)
{
	m_isShaderPSO = isShaderPSO;
}

PipelineStateObject::~PipelineStateObject()
{
	ShaderSystem->m_shaderReloadedDelegate.Remove(m_shaderReloadEventHandle);
}

void PipelineStateObject::ReloadShaders()
{
	// 이름으로 다시 잡는다. 핫리로드는 blob을 갈아 끼우므로 묶음의 구성은
	// 그대로이고, 참조만 새 것을 가리키면 된다.
	if (nullptr != m_vertexShader)
	{
		m_vertexShader = &ShaderSystem->VertexShaders[m_vertexShader.m_shader_identifier];
	}
	if (nullptr != m_pixelShader)
	{
		m_pixelShader = &ShaderSystem->PixelShaders[m_pixelShader.m_shader_identifier];
	}
	if (nullptr != m_geometryShader)
	{
		m_geometryShader = &ShaderSystem->GeometryShaders[m_geometryShader.m_shader_identifier];
	}
	if (nullptr != m_hullShader)
	{
		m_hullShader = &ShaderSystem->HullShaders[m_hullShader.m_shader_identifier];
	}
	if (nullptr != m_domainShader)
	{
		m_domainShader = &ShaderSystem->DomainShaders[m_domainShader.m_shader_identifier];
	}
	if (nullptr != m_computeShader)
	{
		m_computeShader = &ShaderSystem->ComputeShaders[m_computeShader.m_shader_identifier];
	}
}
#endif // !DYNAMICCPP_EXPORTS
