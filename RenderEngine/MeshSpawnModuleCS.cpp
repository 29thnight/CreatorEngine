// MeshSpawnModuleCS.cpp - 주요 변경 부분들
#include "MeshSpawnModuleCS.h"
#include "ShaderSystem.h"

MeshSpawnModuleCS::MeshSpawnModuleCS()
    : m_computeShader(nullptr)
    , m_spawnParamsBuffer(nullptr)
    , m_templateBuffer(nullptr)
    , m_randomStateBuffer(nullptr)
    , m_randomStateUAV(nullptr)
    , m_spawnParamsDirty(true)
    , m_templateDirty(true)
    , m_isInitialized(false)
    , m_particleCapacity(0)
    , m_randomGenerator(m_randomDevice())
    , m_uniform(0.0f, 1.0f)
{
    // 스폰 파라미터 기본값 (동일)
    m_spawnParams.spawnRate = 1.0f;
    m_spawnParams.deltaTime = 0.0f;
    m_spawnParams.currentTime = 0.0f;
    m_spawnParams.emitterType = static_cast<int>(EmitterType::point);
    m_spawnParams.emitterSize = XMFLOAT3(0.5f, 0.5f, 0.5f);
    m_spawnParams.emitterRadius = 1.0f;
    m_spawnParams.maxParticles = 0;

    // 🔥 3D 메시 파티클 템플릿 기본값
    m_meshParticleTemplate.lifeTime = 10.0f;

    // 3D 스케일 범위
    m_meshParticleTemplate.minScale = XMFLOAT3(0.5f, 0.5f, 0.5f);
    m_meshParticleTemplate.maxScale = XMFLOAT3(1.5f, 1.5f, 1.5f);

    // 3D 회전 속도 범위
    m_meshParticleTemplate.minRotationSpeed = XMFLOAT3(-1.0f, -1.0f, -1.0f);
    m_meshParticleTemplate.maxRotationSpeed = XMFLOAT3(1.0f, 1.0f, 1.0f);

    // 3D 초기 회전 범위
    m_meshParticleTemplate.minInitialRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_meshParticleTemplate.maxInitialRotation = XMFLOAT3(0.0f, 0.0f, 0.0f);

    // 기존과 동일한 속성들
    m_meshParticleTemplate.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    m_meshParticleTemplate.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_meshParticleTemplate.acceleration = XMFLOAT3(0.0f, -9.8f, 0.0f);
    m_meshParticleTemplate.minVerticalVelocity = 0.0f;
    m_meshParticleTemplate.maxVerticalVelocity = 0.0f;
    m_meshParticleTemplate.horizontalVelocityRange = 0.0f;
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
    m_spawnParams.currentTime = currentTime;  // TimeSystem에서 가져온 시간
    m_spawnParamsDirty = true;

    // 디버깅: 시간 정보 출력
    static int debugCount = 0;
    if (debugCount < 5)
    {
        char debug[256];
        sprintf_s(debug, "SpawnModule Frame %d: Rate=%.2f, DeltaTime=%.4f, CycleTime=%.2f/%.1f\n",
            debugCount, m_spawnParams.spawnRate, deltaTime, currentTime, maxCycleTime);
        OutputDebugStringA(debug);
        debugCount++;
    }

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(deltaTime);

    // 컴퓨트 셰이더 바인딩
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 바인딩
    ID3D11Buffer* constantBuffers[] = { m_spawnParamsBuffer, m_templateBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, constantBuffers);

    // 입력 리소스 바인딩
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // 출력 리소스 바인딩 (스폰 타이머 버퍼 제거)
    ID3D11UnorderedAccessView* uavs[] = {
        m_outputUAV,        // u0: 파티클 출력
        m_randomStateUAV    // u1: 난수 상태
    };
    UINT initCounts[] = { 0, 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    // 디스패치 실행
    UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;  // 64는 셰이더의 THREAD_GROUP_SIZE
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 정리
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
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


bool MeshSpawnModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["MeshSpawnModule"].GetShader();
    return m_computeShader != nullptr;
}

// 상수 버퍼 생성: 크기만 변경
bool MeshSpawnModuleCS::CreateConstantBuffers()
{
    // 스폰 파라미터 상수 버퍼 (동일)
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(SpawnParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_spawnParamsBuffer);
    if (FAILED(hr))
        return false;

    // 🔥 메시 파티클 템플릿 상수 버퍼: 크기 변경
    bufferDesc.ByteWidth = sizeof(MeshParticleTemplateParams);
    hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_templateBuffer);
    if (FAILED(hr))
        return false;

    return true;
}

bool MeshSpawnModuleCS::CreateUtilityBuffers()
{
    // 난수 상태 버퍼만 생성 (스폰 타이머 버퍼 제거)
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

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_randomStateBuffer);
    if (FAILED(hr))
        return false;

    // 난수 상태 UAV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 1;
    uavDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_randomStateBuffer, &uavDesc, &m_randomStateUAV);
    if (FAILED(hr))
        return false;

    // 디버그 출력
    //OutputDebugStringA("SpawnModule: Utility buffers created successfully\n");

    return true;
}

// 상수 버퍼 업데이트: 구조체 이름만 변경
void MeshSpawnModuleCS::UpdateConstantBuffers(float deltaTime)
{
    // 스폰 파라미터 업데이트 (동일)
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

    // 메시 파티클 템플릿 업데이트: 구조체 변경
    if (m_templateDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_templateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &m_meshParticleTemplate, sizeof(MeshParticleTemplateParams));
            DeviceState::g_pDeviceContext->Unmap(m_templateBuffer, 0);
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
void MeshSpawnModuleCS::SetParticleScaleRange(const XMFLOAT3& minScale, const XMFLOAT3& maxScale)
{
    m_meshParticleTemplate.minScale = minScale;
    m_meshParticleTemplate.maxScale = maxScale;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleRotationSpeedRange(const XMFLOAT3& minSpeed, const XMFLOAT3& maxSpeed)
{
    m_meshParticleTemplate.minRotationSpeed = minSpeed;
    m_meshParticleTemplate.maxRotationSpeed = maxSpeed;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleInitialRotationRange(const XMFLOAT3& minRot, const XMFLOAT3& maxRot)
{
    m_meshParticleTemplate.minInitialRotation = minRot;
    m_meshParticleTemplate.maxInitialRotation = maxRot;
    m_templateDirty = true;
}

// 🔥 기존 메서드들: 멤버 변수 이름만 변경
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

void MeshSpawnModuleCS::SetParticleVelocity(const XMFLOAT3& velocity)
{
    m_meshParticleTemplate.velocity = velocity;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetParticleAcceleration(const XMFLOAT3& acceleration)
{
    m_meshParticleTemplate.acceleration = acceleration;
    m_templateDirty = true;
}

void MeshSpawnModuleCS::SetVelocityRange(float minVertical, float maxVertical, float horizontalRange)
{
    m_meshParticleTemplate.minVerticalVelocity = minVertical;
    m_meshParticleTemplate.maxVerticalVelocity = maxVertical;
    m_meshParticleTemplate.horizontalVelocityRange = horizontalRange;
    m_templateDirty = true;
}


// ... 기타 Set 메서드들도 m_particleTemplate → m_meshParticleTemplate 로 변경

// 🔥 핵심 변경사항 요약:
// 1. 멤버 변수: m_particleTemplate → m_meshParticleTemplate
// 2. 구조체 타입: ParticleTemplateParams → MeshParticleTemplateParams  
// 3. 상수 버퍼 크기: sizeof(MeshParticleTemplateParams)
// 4. 컴퓨트 셰이더: "SpawnModule" → "MeshSpawnModule"
// 5. Update 함수의 파라미터 타입: ParticleData → MeshParticleData
// 6. 3D 관련 새로운 설정 메서드들 추가