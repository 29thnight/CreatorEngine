#include "MeshSizeModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"

MeshSizeModuleCS::MeshSizeModuleCS()
	: m_computeShader(nullptr)
	, m_sizeParamsBuffer(nullptr)
	, m_paramsDirty(true)
	, m_easingEnable(false)
	, m_isInitialized(false)
	, m_particleCapacity(0)
{
	m_sizeParams.startSize = XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_sizeParams.endSize = XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_sizeParams.deltaTime = 0.0f;
	m_sizeParams.useRandomScale = 0;
	m_sizeParams.randomScaleMin = 0.5f;
	m_sizeParams.randomScaleMax = 2.0f;
	m_sizeParams.maxParticles = 0;
	m_sizeParams.emitterScale = { 1,1,1 };
	m_sizeParams.useOscillation = 0;
	m_sizeParams.oscillationSpeed = 5.0f;
}

MeshSizeModuleCS::~MeshSizeModuleCS()
{
	Release();
}

void MeshSizeModuleCS::Initialize()
{
	if (m_isInitialized)
		return;

	if (!InitializeComputeShader())
	{
		OutputDebugStringA("Failed to initialize size compute shader\n");
		return;
	}

	if (!CreateConstantBuffers())
	{
		OutputDebugStringA("Failed to create size constant buffers\n");
		return;
	}

	m_isInitialized = true;
	OutputDebugStringA("SizeModuleCS initialized successfully\n");
}

void MeshSizeModuleCS::Update(float deltaTime)
{
	if (!m_enabled) return;

	if (!m_isInitialized)
	{
		OutputDebugStringA("ERROR: SizeModuleCS not initialized!\n");
		return;
	}

	DirectX11::BeginEvent(L"MeshSizeModuleCS Update");

	// 파티클 용량 업데이트
	m_sizeParams.maxParticles = m_particleCapacity;

	// 이징 처리
	float processedDeltaTime = deltaTime;
	if (m_easingEnable)
	{
		float easingValue = m_easingModule.Update(deltaTime);
		processedDeltaTime = deltaTime * easingValue;
	}

	m_sizeParams.deltaTime = processedDeltaTime;

	// 상수 버퍼 업데이트
	UpdateConstantBuffers();

	// 컴퓨트 셰이더 바인딩
	DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

	// 상수 버퍼 바인딩
	ID3D11Buffer* constantBuffers[] = { m_sizeParamsBuffer };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

	// 입력 리소스 바인딩
	ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

	// 출력 리소스 바인딩
	ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
	UINT initCounts[] = { 0 };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

	// 디스패치 실행
	UINT numThreadGroups = (m_particleCapacity + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
	DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

	// 리소스 정리
	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

	ID3D11Buffer* nullBuffers[] = { nullptr };
	DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

	DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

	DirectX11::EndEvent();
}

void MeshSizeModuleCS::Release()
{
	ReleaseResources();
	m_isInitialized = false;
}

void MeshSizeModuleCS::OnSystemResized(UINT maxParticles)
{
	if (maxParticles != m_particleCapacity)
	{
		m_particleCapacity = maxParticles;
		m_sizeParams.maxParticles = maxParticles;
		m_paramsDirty = true;
	}
}

void MeshSizeModuleCS::ResetForReuse()
{
	if (!m_enabled) return;

	// 시간 관련 상태 초기화
	m_sizeParams.deltaTime = 0.0f;

	// 더티 플래그 설정
	m_paramsDirty = true;

	// 이징 모듈 리셋
	if (m_easingEnable) {
		m_easingModule.Reset();
	}
}

bool MeshSizeModuleCS::IsReadyForReuse() const
{
	return m_isInitialized &&
		m_sizeParamsBuffer != nullptr;
}

void MeshSizeModuleCS::SetRandomScale(bool useRandomScale, float min, float max)
{
	m_sizeParams.useRandomScale = useRandomScale;
	m_sizeParams.randomScaleMin = min;
	m_sizeParams.randomScaleMax = max;
	m_paramsDirty = true;
}

void MeshSizeModuleCS::SetOscillation(bool enabled, float speed)
{
	int enabledInt = enabled ? 1 : 0;
	if (m_sizeParams.useOscillation != enabledInt ||
		m_sizeParams.oscillationSpeed != speed)
	{
		m_sizeParams.useOscillation = enabledInt;
		m_sizeParams.oscillationSpeed = speed;
		m_paramsDirty = true;
	}
}

void MeshSizeModuleCS::SetEasingEnabled(bool enabled)
{
	m_easingEnable = enabled;
	m_paramsDirty = true;
}

void MeshSizeModuleCS::SetEasing(EasingEffect easingType, StepAnimation animationType, float duration)
{
	m_easingModule.SetEasingType(easingType);
	m_easingModule.SetAnimationType(animationType);
	m_easingModule.SetDuration(duration);
	m_easingEnable = true;
}

void MeshSizeModuleCS::DisableEasing()
{
	m_easingEnable = false;
}

bool MeshSizeModuleCS::InitializeComputeShader()
{
	m_computeShader = ShaderSystem->ComputeShaders["MeshSizeModule"].GetShader();
	return m_computeShader != nullptr;
}

bool MeshSizeModuleCS::CreateConstantBuffers()
{
	// Size 파라미터 상수 버퍼
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(MeshSizeParams);
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_sizeParamsBuffer);
	if (FAILED(hr))
	{
		OutputDebugStringA("Failed to create size params buffer\n");
		return false;
	}

	return true;
}

void MeshSizeModuleCS::UpdateConstantBuffers()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_sizeParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	if (SUCCEEDED(hr))
	{
		memcpy(mappedResource.pData, &m_sizeParams, sizeof(MeshSizeParams));
		DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_sizeParamsBuffer, 0);
		m_paramsDirty = false;
	}
}

void MeshSizeModuleCS::ReleaseResources()
{
	if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
	if (m_sizeParamsBuffer) { m_sizeParamsBuffer->Release(); m_sizeParamsBuffer = nullptr; }
}

nlohmann::json MeshSizeModuleCS::SerializeData() const
{
	nlohmann::json json;

	// SizeParams 직렬화
	json["sizeParams"] = {
		{"startSize", {
			{"x", m_sizeParams.startSize.x},
			{"y", m_sizeParams.startSize.y},
			{"z", m_sizeParams.startSize.z},
		}},
		{"endSize", {
			{"x", m_sizeParams.endSize.x},
			{"y", m_sizeParams.endSize.y},
			{"z", m_sizeParams.endSize.z},
		}},
		{"useRandomScale", m_sizeParams.useRandomScale},
		{"randomScaleMin", m_sizeParams.randomScaleMin},
		{"randomScaleMax", m_sizeParams.randomScaleMax},
		{"useOscillation", m_sizeParams.useOscillation},
		{"oscillationSpeed", m_sizeParams.oscillationSpeed}
	};

	// 이징 관련 정보 직렬화
	json["easing"] = {
		{"enabled", m_easingEnable},
		{"type", static_cast<int>(m_easingModule.GetEasingType())},
		{"animationType", static_cast<int>(m_easingModule.GetAnimationType())},
		{"duration", m_easingModule.GetDuration()}
	};

	// 상태 정보
	json["state"] = {
		{"isInitialized", m_isInitialized},
		{"particleCapacity", m_particleCapacity}
	};

	return json;
}

void MeshSizeModuleCS::DeserializeData(const nlohmann::json& json)
{
	// SizeParams 복원
	if (json.contains("sizeParams"))
	{
		const auto& sizeJson = json["sizeParams"];

		if (sizeJson.contains("startSize"))
		{
			const auto& startSizeJson = sizeJson["startSize"];
			m_sizeParams.startSize.x = startSizeJson.value("x", 1.0f);
			m_sizeParams.startSize.y = startSizeJson.value("y", 1.0f);
			m_sizeParams.startSize.z = startSizeJson.value("z", 1.0f);
		}

		if (sizeJson.contains("endSize"))
		{
			const auto& endSizeJson = sizeJson["endSize"];
			m_sizeParams.endSize.x = endSizeJson.value("x", 1.0f);
			m_sizeParams.endSize.y = endSizeJson.value("y", 1.0f);
			m_sizeParams.endSize.z = endSizeJson.value("z", 1.0f);
		}

		if (sizeJson.contains("useRandomScale"))
			m_sizeParams.useRandomScale = sizeJson["useRandomScale"];

		if (sizeJson.contains("randomScaleMin"))
			m_sizeParams.randomScaleMin = sizeJson["randomScaleMin"];

		if (sizeJson.contains("randomScaleMax"))
			m_sizeParams.randomScaleMax = sizeJson["randomScaleMax"];

		if (sizeJson.contains("useOscillation"))
			m_sizeParams.useOscillation = sizeJson["useOscillation"];

		if (sizeJson.contains("oscillationSpeed"))
			m_sizeParams.oscillationSpeed = sizeJson["oscillationSpeed"];
	}

	// 이징 정보 복원
	if (json.contains("easing"))
	{
		const auto& easingJson = json["easing"];

		if (easingJson.contains("enabled"))
			m_easingEnable = easingJson["enabled"];

		if (easingJson.contains("type") && easingJson.contains("animationType") && easingJson.contains("duration"))
		{
			EasingEffect easingType = static_cast<EasingEffect>(easingJson["type"]);
			StepAnimation animationType = static_cast<StepAnimation>(easingJson["animationType"]);
			float duration = easingJson["duration"];

			m_easingModule.SetEasingType(easingType);
			m_easingModule.SetAnimationType(animationType);
			m_easingModule.SetDuration(duration);
		}
	}

	// 상태 정보 복원
	if (json.contains("state"))
	{
		const auto& stateJson = json["state"];

		if (stateJson.contains("particleCapacity"))
			m_particleCapacity = stateJson["particleCapacity"];
	}

	if (!m_isInitialized)
		Initialize();

	// 변경사항을 적용하기 위해 더티 플래그 설정
	m_paramsDirty = true;
}

std::string MeshSizeModuleCS::GetModuleType() const
{
	return "MeshSizeModuleCS";
}
