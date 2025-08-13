#include "SpawnModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"
#include "EffectSerializer.h"

SpawnModuleCS::SpawnModuleCS()
	: m_computeShader(nullptr)
	, m_spawnParamsBuffer(nullptr)
	, m_templateBuffer(nullptr)
	, m_randomStateBuffer(nullptr)
	, m_randomStateUAV(nullptr)
	, m_spawnParamsDirty(true)
	, m_templateDirty(true)
	, m_particleCapacity(0)
	, m_randomGenerator(m_randomDevice())
	, m_uniform(0.0f, 1.0f)
	, m_forcePositionUpdate(false)
{
	// �⺻�� ����
	m_spawnParams.spawnRate = 1.0f;
	m_spawnParams.deltaTime = 0.0f;
	m_spawnParams.currentTime = 0.0f;      // ���� �߰�
	m_spawnParams.emitterType = static_cast<int>(EmitterType::point);
	m_spawnParams.emitterSize = XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_spawnParams.emitterRadius = 1.0f;
	m_spawnParams.maxParticles = 0;
	m_spawnParams.emitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.previousEmitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.forcePositionUpdate = 0;
	m_spawnParams.emitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.forceRotationUpdate = 0;
	m_spawnParams.previousEmitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_previousEmitterPosition = Mathf::Vector3(0.0f, 0.0f, 0.0f);

	// ��ƼŬ ���ø� �⺻��
	m_particleTemplate.lifeTime = 10.0f;
	m_particleTemplate.rotateSpeed = 0.0f;
	m_particleTemplate.size = XMFLOAT2(0.1f, 0.1f);
	m_particleTemplate.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_particleTemplate.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_particleTemplate.acceleration = XMFLOAT3(0.0f, -9.8f, 0.0f);
	m_particleTemplate.minVerticalVelocity = 0.0f;
	m_particleTemplate.maxVerticalVelocity = 0.0f;
	m_particleTemplate.horizontalVelocityRange = 0.0f;

	m_originalEmitterSize = XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_originalParticleScale = XMFLOAT2(1.0f, 1.0f);
}

SpawnModuleCS::~SpawnModuleCS()
{
	Release();
}

void SpawnModuleCS::Initialize()
{

	if (m_isInitialized)
		return;

	if (!InitializeComputeShader())
	{
		OutputDebugStringA("Failed to initialize compute shader\n");
		return;
	}

	if (!CreateConstantBuffers())
	{
		OutputDebugStringA("Failed to create constant buffers\n");
		return;
	}

	if (!CreateUtilityBuffers())
	{
		OutputDebugStringA("Failed to create utility buffers\n");
		return;
	}

	m_isInitialized = true;
}

void SpawnModuleCS::Update(float deltaTime)
{
	if (!m_enabled) return;

	if (m_isInitialized == 0)
	{
		OutputDebugStringA("ERROR: SpawnModule not initialized!\n");
		return;
	}

	DirectX11::BeginEvent(L"SpawnModule Update");

	// TimeSystem���� �� ��� �ð� ��������
	double totalSeconds = Time->GetTotalSeconds();
	float currentTime = static_cast<float>(totalSeconds);

	// ���� �������� �ð� ��ȯ (���е� ����)
	float maxCycleTime = std::max(60.0f, m_particleCapacity / m_spawnParams.spawnRate * 2.0f);
	currentTime = fmod(currentTime, maxCycleTime);

	// ��ƼŬ �뷮 ������Ʈ
	m_spawnParams.maxParticles = m_particleCapacity;
	m_spawnParams.deltaTime = deltaTime;
	m_spawnParams.currentTime = currentTime;  // TimeSystem���� ������ �ð�
	m_spawnParams.forcePositionUpdate = m_forcePositionUpdate ? 1 : 0;

	m_spawnParamsDirty = true;

	// �����: �ð� ���� ���
	static int debugCount = 0;
	if (debugCount < 5)
	{
		char debug[256];
		sprintf_s(debug, "SpawnModule Frame %d: Rate=%.2f, DeltaTime=%.4f, CycleTime=%.2f/%.1f\n",
			debugCount, m_spawnParams.spawnRate, deltaTime, currentTime, maxCycleTime);
		OutputDebugStringA(debug);
		debugCount++;
	}

	// ��� ���� ������Ʈ
	UpdateConstantBuffers(deltaTime);

	// ��ǻƮ ���̴� ���ε�
	DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

	// ��� ���� ���ε�
	ID3D11Buffer* constantBuffers[] = { m_spawnParamsBuffer, m_templateBuffer };
	DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, constantBuffers);

	// �Է� ���ҽ� ���ε�
	ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
	DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

	// ��� ���ҽ� ���ε� (���� Ÿ�̸� ���� ����)
	ID3D11UnorderedAccessView* uavs[] = {
		m_outputUAV,        // u0: ��ƼŬ ���
		m_randomStateUAV    // u1: ���� ����
	};
	UINT initCounts[] = { 0, 0 };
	DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

	// ����ġ ����
	UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
	DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

	// ���ҽ� ����
	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
	DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

	ID3D11Buffer* nullBuffers[] = { nullptr, nullptr };
	DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, nullBuffers);

	DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

	DirectX11::EndEvent();

	if (m_forcePositionUpdate)
	{
		m_forcePositionUpdate = false;
		m_previousEmitterPosition = Mathf::Vector3(
			m_spawnParams.emitterPosition.x,
			m_spawnParams.emitterPosition.y,
			m_spawnParams.emitterPosition.z
		);
		m_spawnParams.previousEmitterPosition = m_spawnParams.emitterPosition;
	}

	if (m_spawnParams.forceRotationUpdate)
	{
		m_spawnParams.forceRotationUpdate = 0;
	}
}

void SpawnModuleCS::Release()
{
	ReleaseResources();
	m_isInitialized = false;
}

bool SpawnModuleCS::InitializeComputeShader()
{
	m_computeShader = ShaderSystem->ComputeShaders["SpawnModule"].GetShader();
	return m_computeShader != nullptr;
}

bool SpawnModuleCS::CreateConstantBuffers()
{
	// ���� �Ķ���� ��� ����
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(SpawnParams);
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_spawnParamsBuffer);
	if (FAILED(hr))
		return false;

	// ��ƼŬ ���ø� ��� ����
	bufferDesc.ByteWidth = sizeof(ParticleTemplateParams);
	hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_templateBuffer);
	if (FAILED(hr))
		return false;

	return true;
}

bool SpawnModuleCS::CreateUtilityBuffers()
{
	// ���� ���� ���۸� ���� (���� Ÿ�̸� ���� ����)
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(UINT);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(UINT);

	// �ʱ� ���� �õ�
	UINT initialSeed = m_randomGenerator();
	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = &initialSeed;

	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_randomStateBuffer);
	if (FAILED(hr))
		return false;

	// ���� ���� UAV
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = 1;
	uavDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_randomStateBuffer, &uavDesc, &m_randomStateUAV);
	if (FAILED(hr))
		return false;

	// ����� ���
	OutputDebugStringA("SpawnModule: Utility buffers created successfully\n");

	return true;
}

void SpawnModuleCS::UpdateConstantBuffers(float deltaTime)
{
	// ���� �Ķ���� ������Ʈ
	if (m_spawnParamsDirty)
	{
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = DeviceState::g_pDeviceContext->Map(m_spawnParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

		if (SUCCEEDED(hr))
		{
			memcpy(mappedResource.pData, &m_spawnParams, sizeof(SpawnParams));
			DeviceState::g_pDeviceContext->Unmap(m_spawnParamsBuffer, 0);
			m_spawnParamsDirty = false;
		}
	}

	// ��ƼŬ ���ø� ������Ʈ
	if (m_templateDirty)
	{
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = DeviceState::g_pDeviceContext->Map(m_templateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

		if (SUCCEEDED(hr))
		{
			memcpy(mappedResource.pData, &m_particleTemplate, sizeof(ParticleTemplateParams));
			DeviceState::g_pDeviceContext->Unmap(m_templateBuffer, 0);
			m_templateDirty = false;
		}
	}
}

void SpawnModuleCS::ReleaseResources()
{
	if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
	if (m_spawnParamsBuffer) { m_spawnParamsBuffer->Release(); m_spawnParamsBuffer = nullptr; }
	if (m_templateBuffer) { m_templateBuffer->Release(); m_templateBuffer = nullptr; }
	if (m_randomStateBuffer) { m_randomStateBuffer->Release(); m_randomStateBuffer = nullptr; }
	if (m_randomStateUAV) { m_randomStateUAV->Release(); m_randomStateUAV = nullptr; }
}

void SpawnModuleCS::ResetForReuse()
{
	if (!m_enabled) return;

	std::lock_guard<std::mutex> lock(m_resetMutex); // ���ؽ� ��ȣ

	// ���� ���� ����
	m_spawnParams.currentTime = 0.0f;
	m_spawnParams.deltaTime = 0.0f;
	m_forcePositionUpdate = true;
	m_spawnParamsDirty = true;
	m_templateDirty = true;

	// ��ġ/ȸ�� ����
	m_previousEmitterPosition = Mathf::Vector3(0.0f, 0.0f, 0.0f);
	m_spawnParams.previousEmitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.emitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.emitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.previousEmitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_spawnParams.forceRotationUpdate = 0;
}


bool SpawnModuleCS::IsReadyForReuse() const
{
	return m_isInitialized &&
		m_randomStateBuffer != nullptr &&
		m_spawnParamsBuffer != nullptr &&
		m_templateBuffer != nullptr;
}

// ���� �޼����
void SpawnModuleCS::SetEmitterPosition(const Mathf::Vector3& position)
{
	Mathf::Vector3 newPos = position;

	// ���� ��ġ�� ���Ͽ� ������ ����Ǿ����� Ȯ��
	Mathf::Vector3 currentPos(
		m_spawnParams.emitterPosition.x,
		m_spawnParams.emitterPosition.y,
		m_spawnParams.emitterPosition.z
	);

	float threshold = 0.001f;
	if (abs(newPos.x - currentPos.x) > threshold ||
		abs(newPos.y - currentPos.y) > threshold ||
		abs(newPos.z - currentPos.z) > threshold)
	{
		// ���� ��ġ ����
		m_spawnParams.previousEmitterPosition = m_spawnParams.emitterPosition;
		m_previousEmitterPosition = currentPos;

		// �� ��ġ ����
		m_spawnParams.emitterPosition = XMFLOAT3(newPos.x, newPos.y, newPos.z);

		// ���� ��ġ ������Ʈ �÷��� ����
		m_forcePositionUpdate = true;
		m_spawnParamsDirty = true;
	}
}

void SpawnModuleCS::SetEmitterRotation(const Mathf::Vector3& rotation)
{
	Mathf::Vector3 newRot = rotation;

	// ���� ȸ���� ���Ͽ� ������ ����Ǿ����� Ȯ��
	Mathf::Vector3 currentRot(
		m_spawnParams.emitterRotation.x,
		m_spawnParams.emitterRotation.y,
		m_spawnParams.emitterRotation.z
	);

	float threshold = 0.001f;
	if (abs(newRot.x - currentRot.x) > threshold ||
		abs(newRot.y - currentRot.y) > threshold ||
		abs(newRot.z - currentRot.z) > threshold)
	{
		m_spawnParams.previousEmitterRotation = m_spawnParams.emitterRotation;

		// �� ȸ���� ����
		m_spawnParams.emitterRotation = XMFLOAT3(newRot.x, newRot.y, newRot.z);

		m_spawnParams.forceRotationUpdate = 1;
		m_spawnParamsDirty = true;
	}
}

void SpawnModuleCS::SetEmitterScale(const Mathf::Vector3& scale)
{
	m_spawnParams.emitterSize = XMFLOAT3(
		m_originalEmitterSize.x * scale.x,
		m_originalEmitterSize.y * scale.y,
		m_originalEmitterSize.z * scale.z
	);

	m_particleTemplate.size = XMFLOAT2(
		m_originalParticleScale.x * scale.x,
		m_originalParticleScale.y * scale.y
	);

	m_spawnParamsDirty = true;
	m_templateDirty = true;
}


void SpawnModuleCS::SetSpawnRate(float rate)
{
	if (m_spawnParams.spawnRate != rate)
	{
		m_spawnParams.spawnRate = rate;
		m_spawnParamsDirty = true;
	}
}

void SpawnModuleCS::SetEmitterType(EmitterType type)
{
	int typeInt = static_cast<int>(type);
	if (m_spawnParams.emitterType != typeInt)
	{
		m_spawnParams.emitterType = typeInt;
		m_spawnParamsDirty = true;
	}
}

void SpawnModuleCS::SetEmitterSize(const XMFLOAT3& size)
{
	m_originalEmitterSize = size;
	m_spawnParams.emitterSize = size;
	m_spawnParamsDirty = true;
}

void SpawnModuleCS::SetEmitterRadius(float radius)
{
	if (m_spawnParams.emitterRadius != radius)
	{
		m_spawnParams.emitterRadius = radius;
		m_spawnParamsDirty = true;
	}
}

void SpawnModuleCS::SetParticleLifeTime(float lifeTime)
{
	if (m_particleTemplate.lifeTime != lifeTime)
	{
		m_particleTemplate.lifeTime = lifeTime;
		m_templateDirty = true;
	}
}

void SpawnModuleCS::SetParticleSize(const XMFLOAT2& size)
{
	m_particleTemplate.size = size;  // �׳� �ٷ� ����
	m_templateDirty = true;
}

void SpawnModuleCS::SetParticleColor(const XMFLOAT4& color)
{
	m_particleTemplate.color = color;
	m_templateDirty = true;
}

void SpawnModuleCS::SetParticleVelocity(const XMFLOAT3& velocity)
{
	m_particleTemplate.velocity = velocity;
	m_templateDirty = true;
}

void SpawnModuleCS::SetParticleAcceleration(const XMFLOAT3& acceleration)
{
	m_particleTemplate.acceleration = acceleration;
	m_templateDirty = true;
}

void SpawnModuleCS::SetVelocityRange(float minVertical, float maxVertical, float horizontalRange)
{
	m_particleTemplate.minVerticalVelocity = minVertical;
	m_particleTemplate.maxVerticalVelocity = maxVertical;
	m_particleTemplate.horizontalVelocityRange = horizontalRange;
	m_templateDirty = true;
}

void SpawnModuleCS::SetRotateSpeed(float speed)
{
	if (m_particleTemplate.rotateSpeed != speed)
	{
		m_particleTemplate.rotateSpeed = speed;
		m_templateDirty = true;
	}
}


void SpawnModuleCS::OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition)
{
	SetEmitterPosition(newPosition);
}

void SpawnModuleCS::OnSystemResized(UINT maxParticles)
{
	if (maxParticles != m_particleCapacity)
	{
		m_particleCapacity = maxParticles;
		m_spawnParams.maxParticles = maxParticles;
		m_spawnParamsDirty = true;
	}
}

nlohmann::json SpawnModuleCS::SerializeData() const
{

	nlohmann::json json;

	// SpawnParams ����ȭ
	json["spawnParams"] = {
		{"spawnRate", m_spawnParams.spawnRate},
		{"emitterType", m_spawnParams.emitterType},
		{"emitterSize", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterSize)},
		{"emitterRadius", m_spawnParams.emitterRadius},
		{"emitterPosition", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterPosition)}
	};

	// ParticleTemplateParams ����ȭ
	json["particleTemplate"] = {
		{"lifeTime", m_particleTemplate.lifeTime},
		{"rotateSpeed", m_particleTemplate.rotateSpeed},
		{"size", {
			{"x", m_particleTemplate.size.x},
			{"y", m_particleTemplate.size.y}
		}},
		{"color", EffectSerializer::SerializeXMFLOAT4(m_particleTemplate.color)},
		{"velocity", EffectSerializer::SerializeXMFLOAT3(m_particleTemplate.velocity)},
		{"acceleration", EffectSerializer::SerializeXMFLOAT3(m_particleTemplate.acceleration)},
		{"minVerticalVelocity", m_particleTemplate.minVerticalVelocity},
		{"maxVerticalVelocity", m_particleTemplate.maxVerticalVelocity},
		{"horizontalVelocityRange", m_particleTemplate.horizontalVelocityRange}
	};

	// ���� ����
	json["state"] = {
		{"isInitialized", m_isInitialized},
		{"particleCapacity", m_particleCapacity}
	};

	return json;

}

void SpawnModuleCS::DeserializeData(const nlohmann::json& json)
{
	// SpawnParams ����
	if (json.contains("spawnParams"))
	{
		const auto& spawnJson = json["spawnParams"];

		if (spawnJson.contains("spawnRate"))
			m_spawnParams.spawnRate = spawnJson["spawnRate"];

		if (spawnJson.contains("emitterType"))
			m_spawnParams.emitterType = spawnJson["emitterType"];

		if (spawnJson.contains("emitterSize"))
			m_spawnParams.emitterSize = EffectSerializer::DeserializeXMFLOAT3(spawnJson["emitterSize"]);

		m_originalEmitterSize = m_spawnParams.emitterSize;

		if (spawnJson.contains("emitterRadius"))
			m_spawnParams.emitterRadius = spawnJson["emitterRadius"];

		if (spawnJson.contains("emitterPosition"))
			m_spawnParams.emitterPosition = EffectSerializer::DeserializeXMFLOAT3(spawnJson["emitterPosition"]);
	}

	// ParticleTemplateParams ����
	if (json.contains("particleTemplate"))
	{
		const auto& templateJson = json["particleTemplate"];

		if (templateJson.contains("lifeTime"))
			m_particleTemplate.lifeTime = templateJson["lifeTime"];

		if (templateJson.contains("rotateSpeed"))
			m_particleTemplate.rotateSpeed = templateJson["rotateSpeed"];

		if (templateJson.contains("size"))
		{
			const auto& sizeJson = templateJson["size"];
			m_particleTemplate.size.x = sizeJson.value("x", 1.0f);
			m_particleTemplate.size.y = sizeJson.value("y", 1.0f);
		}

		m_originalParticleScale = m_particleTemplate.size;

		if (templateJson.contains("color"))
			m_particleTemplate.color = EffectSerializer::DeserializeXMFLOAT4(templateJson["color"]);

		if (templateJson.contains("velocity"))
			m_particleTemplate.velocity = EffectSerializer::DeserializeXMFLOAT3(templateJson["velocity"]);

		if (templateJson.contains("acceleration"))
			m_particleTemplate.acceleration = EffectSerializer::DeserializeXMFLOAT3(templateJson["acceleration"]);

		if (templateJson.contains("minVerticalVelocity"))
			m_particleTemplate.minVerticalVelocity = templateJson["minVerticalVelocity"];

		if (templateJson.contains("maxVerticalVelocity"))
			m_particleTemplate.maxVerticalVelocity = templateJson["maxVerticalVelocity"];

		if (templateJson.contains("horizontalVelocityRange"))
			m_particleTemplate.horizontalVelocityRange = templateJson["horizontalVelocityRange"];
	}

	// ���� ���� ����
	if (json.contains("state"))
	{
		const auto& stateJson = json["state"];

		if (stateJson.contains("particleCapacity"))
			m_particleCapacity = stateJson["particleCapacity"];
	}

	if (!m_isInitialized)
		Initialize();

	// ��������� �����ϱ� ���� ��Ƽ �÷��� ����
	m_spawnParamsDirty = true;
	m_templateDirty = true;
}

std::string SpawnModuleCS::GetModuleType() const
{
	return "SpawnModuleCS";
}
