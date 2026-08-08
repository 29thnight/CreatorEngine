#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "Shader.h"

//-----------------------------------------------------------------------------
// PipelineStateObject: 셰이더 묶음 (M1, 2026-08-08)
//
// ★ 이름이 오래전부터 사실과 달랐다. DX12PSOManager.h가 이미 그렇게 적어 두었다 —
//   "ShaderPSO/VisualShaderPSO는 이름만 PSO인 '셰이더 묶음'이다. 진짜 PSO는
//   DX12에서 만든다." M1은 그 문장을 코드로 만든 것이다.
//
// ★ 걷어낸 것: DX11 입력 레이아웃·래스터라이저·블렌드·깊이 상태 객체와 그 소유권
//   관리(Adopt*), 즉시 컨텍스트에 거는 Apply/Reset, 샘플러 목록.
//
//   전부 호출자가 0이었다. 마지막 경로였던 IRenderPass가 D4에서 사라졌고
//   (그 전에 EffectManager가 PHASE 10-0에서 사라지면서 상속자가 0이 됐다),
//   그 뒤로는 이 상태 객체들을 GPU에 거는 코드가 어디에도 없었다.
//
// 남은 것은 둘이다 — 셰이더 blob 묶음과 정점 입력 레이아웃 기술. 둘 다
// 백엔드가 아니라 자산의 성질이고, DX12가 PSO를 구울 때 쓰는 입력이다.
//-----------------------------------------------------------------------------

// TODO(M2): D3D11_INPUT_ELEMENT_DESC를 중립 기술로 바꾼다. 지금은 타입만
//           DX11이고 디바이스는 쓰지 않는다 — 리플렉션이 채우는 순수 자료다.
using InputLayOutContainer = std::vector<D3D11_INPUT_ELEMENT_DESC>;

class PipelineStateObject
{
public:
	ShaderPtr<VertexShader>		m_vertexShader;
	ShaderPtr<PixelShader>		m_pixelShader;
	ShaderPtr<GeometryShader>	m_geometryShader;
	ShaderPtr<HullShader>		m_hullShader;
	ShaderPtr<DomainShader>		m_domainShader;
	ShaderPtr<ComputeShader>	m_computeShader;

	Core::DelegateHandle m_shaderReloadEventHandle;
	InputLayOutContainer m_inputLayoutDescContainer;
	bool m_isShaderPSO{ false };

public:
	PipelineStateObject();
	PipelineStateObject(bool isShaderPSO);
	~PipelineStateObject();

	/// 핫리로드 후 셰이더 참조를 다시 잡는다. 이름으로 찾으므로 blob이
	/// 바뀌어도 묶음은 그대로다.
	void ReloadShaders();
};
#else
class PipelineStateObject {};
#endif // !DYNAMICCPP_EXPORTS
