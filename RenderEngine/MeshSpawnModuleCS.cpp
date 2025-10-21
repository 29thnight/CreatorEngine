// MeshSpawnModuleCS.cpp - emitterPosition 추가
#include "MeshSpawnModuleCS.h"
#include "ShaderSystem.h"
#include "EffectSerializer.h"

MeshSpawnModuleCS::MeshSpawnModuleCS()
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
    , m_forceRotationUpdate(false)
{
    // 스폰 파라미터 기본값
    m_spawnParams.spawnRate = 1.0f;
    m_spawnParams.deltaTime = 0.0f;
    m_spawnParams.currentTime = 0.0f;
    m_spawnParams.emitterType = static_cast<int>(EmitterType::point);
    m_spawnParams.emitterRadius = 1.0f;
    m_spawnParams.emitterSize = XMFLOAT3(0.5f, 0.5f, 0.5f);
    m_spawnParams.maxParticles = 0;
    m_spawnParams.emitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.previousEmitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.forcePositionUpdate = 0;
    m_spawnParams.emitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.forceRotationUpdate = 0;
    m_spawnParams.previousEmitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_previousEmitterPosition = Mathf::Vector3(0.0f, 0.0f, 0.0f);

    // 3D 메시 파티클 템플릿 기본값
    m_meshParticleTemplate.lifeTime = 10.0f;

    // 3D 스케일 범위
    m_meshParticleTemplate.Scale = XMFLOAT3(1.0f, 1.0f, 1.0f);

    // 3D 회전 속도 범위
    m_meshParticleTemplate.RotationSpeed = XMFLOAT3(0.0f, 0.0f, 0.0f);

    // 기존과 동일한 속성들
    m_meshParticleTemplate.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    m_meshParticleTemplate.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_meshParticleTemplate.acceleration = XMFLOAT3(0.0f, -9.8f, 0.0f);
    m_meshParticleTemplate.velocityRandomRange = 0.0f;
    m_meshParticleTemplate.textureIndex = 0;
    m_meshParticleTemplate.particleRandomRotation = Mathf::Vector3(0.f, 0.f, 0.f);

    m_originalEmitterSize = XMFLOAT3(1.0f, 1.0f, 1.0f);
    m_originalParticleScale = XMFLOAT3(1.0f, 1.0f, 1.0f);
}

MeshSpawnModuleCS::~MeshSpawnModuleCS()
{
    Release();
}

void MeshSpawnModuleCS::Initialize()
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

void MeshSpawnModuleCS::Update(float deltaTime)
{
    if (!m_enabled) return;

    if (m_isInitialized == 0)
    {
        OutputDebugStringA("ERROR: MeshSpawnModule not initialized!\n");
        return;
    }

    DirectX11::BeginEvent(L"MeshSpawnModule Update");

    // TimeSystem에서 총 경과 시간 가져오기
    double totalSeconds = Time->GetTotalSeconds();
    float currentTime = static_cast<float>(totalSeconds);

    // 모듈로 연산으로 시간 순환 (정밀도 유지)
    float maxCycleTime = std::max(60.0f, m_particleCapacity / m_spawnParams.spawnRate * 2.0f);
    currentTime = fmod(currentTime, maxCycleTime);

    // 파티클 용량 업데이트
    m_spawnParams.maxParticles = m_particleCapacity;
    m_spawnParams.deltaTime = deltaTime;
    m_spawnParams.currentTime = currentTime;

    m_spawnParams.forcePositionUpdate = m_forcePositionUpdate ? 1 : 0;
    m_spawnParams.forceRotationUpdate = m_forceRotationUpdate ? 1 : 0;

    m_spawnParamsDirty = true;

    // 디버깅: 시간 정보 출력
    static int debugCount = 0;
    if (debugCount < 5)
    {
        char debug[256];
        sprintf_s(debug, "MeshSpawnModule Frame %d: Rate=%.2f, DeltaTime=%.4f, CycleTime=%.2f/%.1f\n",
            debugCount, m_spawnParams.spawnRate, deltaTime, currentTime, maxCycleTime);
        OutputDebugStringA(debug);
        debugCount++;
    }

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(deltaTime);

    // 컴퓨트 셰이더 바인딩
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 바인딩
    ID3D11Buffer* constantBuffers[] = { m_spawnParamsBuffer, m_templateBuffer };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 2, constantBuffers);

    // 입력 리소스 바인딩
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // 출력 리소스 바인딩
    ID3D11UnorderedAccessView* uavs[] = {
        m_outputUAV,        // u0: 파티클 출력
        m_randomStateUAV    // u1: 난수 상태
    };
    UINT initCounts[] = { 0, 0 };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    // 디스패치 실행
    UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
    DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 정리
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 2, nullBuffers);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

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

    if (m_forceRotationUpdate)
    {
        m_forceRotationUpdate = false;
        m_spawnParams.previousEmitterRotation = m_spawnParams.emitterRotation;
    }

}

void MeshSpawnModuleCS::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

void MeshSpawnModuleCS::OnSystemResized(UINT maxParticles)
{
    if (maxParticles != m_particleCapacity)
    {
        m_particleCapacity = maxParticles;
        m_spawnParams.maxParticles = maxParticles;
        m_spawnParamsDirty = true;
    }
}

void MeshSpawnModuleCS::OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition)
{
    SetEmitterPosition(newPosition);
}

void MeshSpawnModuleCS::ResetForReuse()
{
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resetMutex);

    // 스폰 관련 상태 초기화
    m_spawnParams.currentTime = 0.0f;
    m_spawnParams.deltaTime = 0.0f;
    m_forcePositionUpdate = true;
    m_forceRotationUpdate = true;
    m_spawnParamsDirty = true;
    m_templateDirty = true;

    // 이전 위치 리셋
    m_previousEmitterPosition = Mathf::Vector3(0.0f, 0.0f, 0.0f);
    m_spawnParams.previousEmitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.emitterPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);

    // 회전 리셋
    m_spawnParams.emitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.previousEmitterRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_spawnParams.forceRotationUpdate = 0;
}

bool MeshSpawnModuleCS::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_randomStateBuffer != nullptr &&
        m_spawnParamsBuffer != nullptr &&
        m_templateBuffer != nullptr;
}

std::string MeshSpawnModuleCS::GetModuleType() const
{
    return "MeshSpawnModuleCS";
}

bool MeshSpawnModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["MeshSpawnModule"].GetShader();
    return m_computeShader != nullptr;
}

bool MeshSpawnModuleCS::CreateConstantBuffers()
{
    // 스폰 파라미터 상수 버퍼
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(SpawnParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_spawnParamsBuffer);
    if (FAILED(hr))
        return false;

    // 메시 파티클 템플릿 상수 버퍼
    bufferDesc.ByteWidth = sizeof(MeshParticleTemplateParams);
    hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_templateBuffer);
    if (FAILED(hr))
        return false;

    return true;
}

bool MeshSpawnModuleCS::CreateUtilityBuffers()
{
    // 난수 상태 버퍼만 생성
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(UINT);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(UINT);

    // 초기 난수 시드
    UINT initialSeed = m_randomGenerator();
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = &initialSeed;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_randomStateBuffer);
    if (FAILED(hr))
        return false;

    // 난수 상태 UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 1;
    uavDesc.Buffer.Flags = 0;

    hr = DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_randomStateBuffer, &uavDesc, &m_randomStateUAV);
    if (FAILED(hr))
        return false;

    return true;
}

void MeshSpawnModuleCS::UpdateConstantBuffers(float deltaTime)
{
    // 스폰 파라미터 업데이트
    if (m_spawnParamsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_spawnParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            m_spawnParams.allowNewSpawn = m_allowNewSpawn ? 1 : 0;
            memcpy(mappedResource.pData, &m_spawnParams, sizeof(SpawnParams));
            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_spawnParamsBuffer, 0);
            m_spawnParamsDirty = false;
        }
    }

    // 메시 파티클 템플릿 업데이트
    if (m_templateDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_templateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &m_meshParticleTemplate, sizeof(MeshParticleTemplateParams));
            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_templateBuffer, 0);
            m_templateDirty = false;
        }
    }
}

void MeshSpawnModuleCS::ReleaseResources()
{
    if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
    if (m_spawnParamsBuffer) { m_spawnParamsBuffer->Release(); m_spawnParamsBuffer = nullptr; }
    if (m_templateBuffer) { m_templateBuffer->Release(); m_templateBuffer = nullptr; }
    if (m_randomStateBuffer) { m_randomStateBuffer->Release(); m_randomStateBuffer = nullptr; }
    if (m_randomStateUAV) { m_randomStateUAV->Release(); m_randomStateUAV = nullptr; }
}

// emitterPosition 설정 메서드 추가
void MeshSpawnModuleCS::SetEmitterPosition(const Mathf::Vector3& position)
{
    Mathf::Vector3 newPos = position;
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
        // 이전 위치 저장
        m_spawnParams.previousEmitterPosition = m_spawnParams.emitterPosition;

        // 새 위치 설정
        m_spawnParams.emitterPosition = XMFLOAT3(newPos.x, newPos.y, newPos.z);

        m_forcePositionUpdate = true;
        m_spawnParamsDirty = true;
    }
}

void MeshSpawnModuleCS::SetEmitterRotation(const Mathf::Vector3& rotation)
{
    Mathf::Vector3 newRot = rotation;

    // 기존 회전과 비교하여 실제로 변경되었는지 확인
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

        // 새 회전값 설정
        m_spawnParams.emitterRotation = XMFLOAT3(newRot.x, newRot.y, newRot.z);

        m_forceRotationUpdate = 1;
        m_spawnParamsDirty = true;
    }
}

void MeshSpawnModuleCS::SetEmitterScale(const Mathf::Vector3& scale)
{
    // 원본값에 직접 스케일 적용
    m_spawnParams.emitterSize = XMFLOAT3(
        m_originalEmitterSize.x * scale.x,
        m_originalEmitterSize.y * scale.y,
        m_originalEmitterSize.z * scale.z
    );

    m_meshParticleTemplate.Scale = XMFLOAT3(
        m_originalParticleScale.x * scale.x,
        m_originalParticleScale.y * scale.y,
        m_originalParticleScale.z * scale.z
    );

    m_spawnParamsDirty = true;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetSpawnRate(float rate)
{
    if (m_spawnParams.spawnRate != rate)
    {
        m_spawnParams.spawnRate = rate;
        m_spawnParamsDirty = true;
    }
}

void MeshSpawnModuleCS::SetEmitterType(EmitterType type)
{
    int typeInt = static_cast<int>(type);
    if (m_spawnParams.emitterType != typeInt)
    {
        m_spawnParams.emitterType = typeInt;
        m_spawnParamsDirty = true;
    }
}

void MeshSpawnModuleCS::SetEmitterSize(const XMFLOAT3& size)
{
    m_originalEmitterSize = size;
    m_spawnParams.emitterSize = size;
    m_spawnParamsDirty = true;
}

void MeshSpawnModuleCS::SetEmitterRadius(float radius)
{
    if (m_spawnParams.emitterRadius != radius)
    {
        m_spawnParams.emitterRadius = radius;
        m_spawnParamsDirty = true;
    }
}

// 3D 설정 메서드들
void MeshSpawnModuleCS::SetParticleScale(const XMFLOAT3& Scale)
{
    m_originalParticleScale = Scale;
    m_meshParticleTemplate.Scale = Scale;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleRotationSpeed(const XMFLOAT3& Speed)
{
    m_meshParticleTemplate.RotationSpeed = Speed;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleLifeTime(float lifeTime)
{
    if (m_meshParticleTemplate.lifeTime != lifeTime)
    {
        m_meshParticleTemplate.lifeTime = lifeTime;
        m_templateDirty = true;
    }
}

void MeshSpawnModuleCS::SetParticleColor(const XMFLOAT4& color)
{
    m_meshParticleTemplate.color = color;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleVelocity(const XMFLOAT3& velocity, float randomRange)
{
    m_meshParticleTemplate.velocity = velocity;
    m_meshParticleTemplate.velocityRandomRange = randomRange;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleAcceleration(const XMFLOAT3& acceleration)
{
    m_meshParticleTemplate.acceleration = acceleration;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetTextureIndex(UINT textureIndex)
{
    if (m_meshParticleTemplate.textureIndex != textureIndex)
    {
        m_meshParticleTemplate.textureIndex = textureIndex;
        m_templateDirty = true;
    }
}

void MeshSpawnModuleCS::SetRenderMode(UINT mode)
{
    if (m_meshParticleTemplate.renderMode != mode)
    {
        m_meshParticleTemplate.renderMode = mode;
        m_templateDirty = true;
    }
}

void MeshSpawnModuleCS::SetParticleRandomRotation(const XMFLOAT3& rotation)
{
    m_meshParticleTemplate.particleRandomRotation = rotation;
    m_templateDirty = true;
}

nlohmann::json MeshSpawnModuleCS::SerializeData() const
{
    nlohmann::json json;

    // SpawnParams 직렬화
    json["spawnParams"] = {
        {"spawnRate", m_spawnParams.spawnRate},
        {"emitterType", m_spawnParams.emitterType},
        {"emitterSize", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterSize)},
        {"emitterRadius", m_spawnParams.emitterRadius},
        {"emitterPosition", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterPosition)},
        {"emitterRotation", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterRotation)}
    };

    // MeshParticleTemplateParams 직렬화
    json["meshParticleTemplate"] = {
        {"lifeTime", m_meshParticleTemplate.lifeTime},
        {"initialRotation", EffectSerializer::SerializeVector3(m_meshParticleTemplate.particleRandomRotation)},
        {"Scale", EffectSerializer::SerializeXMFLOAT3(m_meshParticleTemplate.Scale)},
        {"RotationSpeed", EffectSerializer::SerializeXMFLOAT3(m_meshParticleTemplate.RotationSpeed)},
        {"color", EffectSerializer::SerializeXMFLOAT4(m_meshParticleTemplate.color)},
        {"velocity", EffectSerializer::SerializeXMFLOAT3(m_meshParticleTemplate.velocity)},
        {"velocityRange", m_meshParticleTemplate.velocityRandomRange},
        {"acceleration", EffectSerializer::SerializeXMFLOAT3(m_meshParticleTemplate.acceleration)},
        {"textureIndex", m_meshParticleTemplate.textureIndex},
        {"renderMode", m_meshParticleTemplate.renderMode}
    };

    // 상태 정보
    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}


void MeshSpawnModuleCS::DeserializeData(const nlohmann::json& json)
{
    // SpawnParams 복원
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

        if (spawnJson.contains("emitterRotation"))
            m_spawnParams.emitterRotation = EffectSerializer::DeserializeXMFLOAT3(spawnJson["emitterRotation"]);
    }

    // MeshParticleTemplateParams 복원
    if (json.contains("meshParticleTemplate"))
    {
        const auto& templateJson = json["meshParticleTemplate"];

        if (templateJson.contains("lifeTime"))
            m_meshParticleTemplate.lifeTime = templateJson["lifeTime"];

        if (templateJson.contains("initialRotation"))
            m_meshParticleTemplate.particleRandomRotation = EffectSerializer::DeserializeVector3(templateJson["initialRotation"]);

        if (templateJson.contains("Scale"))
            m_meshParticleTemplate.Scale = EffectSerializer::DeserializeXMFLOAT3(templateJson["Scale"]);

        m_originalParticleScale = m_meshParticleTemplate.Scale;

        if (templateJson.contains("RotationSpeed"))
            m_meshParticleTemplate.RotationSpeed = EffectSerializer::DeserializeXMFLOAT3(templateJson["RotationSpeed"]);

        if (templateJson.contains("color"))
            m_meshParticleTemplate.color = EffectSerializer::DeserializeXMFLOAT4(templateJson["color"]);

        if (templateJson.contains("velocity"))
            m_meshParticleTemplate.velocity = EffectSerializer::DeserializeXMFLOAT3(templateJson["velocity"]);

        if (templateJson.contains("velocityRange"))
            m_meshParticleTemplate.velocityRandomRange = templateJson["velocityRange"];

        if (templateJson.contains("acceleration"))
            m_meshParticleTemplate.acceleration = EffectSerializer::DeserializeXMFLOAT3(templateJson["acceleration"]);

        if (templateJson.contains("textureIndex"))
            m_meshParticleTemplate.textureIndex = templateJson["textureIndex"];

        if (templateJson.contains("renderMode"))
            m_meshParticleTemplate.renderMode = templateJson["renderMode"];
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
    m_spawnParamsDirty = true;
    m_templateDirty = true;
}