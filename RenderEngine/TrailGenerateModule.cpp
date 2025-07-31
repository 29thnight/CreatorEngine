#include "TrailGenerateModule.h"
#include "ShaderSystem.h"
#include "DeviceState.h"
#include "EffectSerializer.h"

TrailGenerateModule::TrailGenerateModule()
    : m_trailMeshCS(nullptr)
    , m_paramsBuffer(nullptr)
    , m_prevPositionBufferA(nullptr)
    , m_prevPositionBufferB(nullptr)
    , m_prevPositionSRV_A(nullptr)
    , m_prevPositionSRV_B(nullptr)
    , m_prevPositionUAV_A(nullptr)
    , m_prevPositionUAV_B(nullptr)
    , m_vertexBuffer(nullptr)
    , m_indexBuffer(nullptr)
    , m_vertexUAV(nullptr)
    , m_indexUAV(nullptr)
    , m_counterBuffer(nullptr)
    , m_counterUAV(nullptr)
    , m_paramsDirty(true)
    , m_needsBufferClear(false)
    , m_maxVertices(0)
    , m_maxIndices(0)
    , m_totalTime(0.0f)
{
    // 기본 파라미터 설정
    m_params.minDistance = 0.1f;           // 최소 0.1 단위마다 트레일 생성
    m_params.trailWidth = 0.2f;            // 기본 폭 0.2
    m_params.deltaTime = 0.0f;
    m_params.maxParticles = 0;
    m_params.enableTrail = 1;              // 기본적으로 활성화
    m_params.velocityThreshold = 0.01f;    // 최소 속도 임계값
    m_params.maxTrailLength = 2.0f;        // 최대 트레일 길이
    m_params.widthOverLength = 0.5f;       // 길이에 따라 절반으로 감소
    m_params.trailColor = Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f);  // 흰색
    m_params.uvTiling = 1.0f;              // UV 1회 반복
    m_params.uvScrollSpeed = 0.0f;         // 스크롤 없음
    m_params.currentTime = 0.0f;
    m_params.pad1 = 0.0f;

    // 시뮬레이션 스테이지로 설정 (파티클 시뮬레이션 후에 실행)
    SetStage(ModuleStage::SIMULATION);
}

void TrailGenerateModule::Initialize()
{
    if (m_isInitialized)
        return;

    if (!InitializeComputeShader())
    {
        OutputDebugStringA("TrailGenerateModule: Failed to initialize compute shader\n");
        return;
    }

    if (!CreateBuffers())
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create buffers\n");
        return;
    }

    m_isInitialized = true;
    OutputDebugStringA("TrailGenerateModule: Initialized successfully\n");
}

void TrailGenerateModule::Update(float deltaTime)
{
    if (!m_enabled || !m_isInitialized)
        return;

    DirectX11::BeginEvent(L"TrailGenerateModule Update");

    // 버퍼 클리어가 필요한 경우 (렌더 스레드에서만 실행)
    if (m_needsBufferClear)
    {
        ClearMeshData();

        // 이전 위치 버퍼 A, B 모두 클리어
        if (m_prevPositionUAV_A)
        {
            UINT clearValue[4] = { 0, 0, 0, 0 };
            DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_prevPositionUAV_A, clearValue);
        }

        if (m_prevPositionUAV_B)
        {
            UINT clearValue[4] = { 0, 0, 0, 0 };
            DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_prevPositionUAV_B, clearValue);
        }

        // 버퍼 상태도 초기화
        m_usingPrevBufferA = true;

        m_needsBufferClear = false;
    }

    // 파라미터 업데이트
    m_params.deltaTime = deltaTime;
    m_totalTime += deltaTime;
    m_params.currentTime = m_totalTime;
    m_paramsDirty = true;

    // 상수 버퍼 업데이트
    UpdateConstantBuffer();

    // 메쉬 생성 실행
    UpdateMeshGeneration();

    DirectX11::EndEvent();
}

void TrailGenerateModule::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

void TrailGenerateModule::ResetForReuse()
{
    if (!m_enabled) return;

    // 상태 초기화 (CPU 작업만)
    m_params.deltaTime = 0.0f;
    m_params.currentTime = 0.0f;
    m_totalTime = 0.0f;
    m_paramsDirty = true;

    // GPU 버퍼 클리어는 다음 Update()에서 처리하도록 플래그 설정
    m_needsBufferClear = true;
}

bool TrailGenerateModule::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_paramsBuffer != nullptr &&
        m_trailMeshCS != nullptr &&
        m_vertexBuffer != nullptr &&
        m_indexBuffer != nullptr;
}

void TrailGenerateModule::OnSystemResized(UINT maxParticles)
{
    if (maxParticles == m_params.maxParticles)
        return;

    bool wasInitialized = m_isInitialized;

    // 기존 버퍼들 해제
    if (m_prevPositionBufferA) { m_prevPositionBufferA->Release(); m_prevPositionBufferA = nullptr; }
    if (m_prevPositionBufferB) { m_prevPositionBufferB->Release(); m_prevPositionBufferB = nullptr; }
    if (m_prevPositionSRV_A) { m_prevPositionSRV_A->Release(); m_prevPositionSRV_A = nullptr; }
    if (m_prevPositionSRV_B) { m_prevPositionSRV_B->Release(); m_prevPositionSRV_B = nullptr; }
    if (m_prevPositionUAV_A) { m_prevPositionUAV_A->Release(); m_prevPositionUAV_A = nullptr; }
    if (m_prevPositionUAV_B) { m_prevPositionUAV_B->Release(); m_prevPositionUAV_B = nullptr; }
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_indexBuffer) { m_indexBuffer->Release(); m_indexBuffer = nullptr; }
    if (m_vertexUAV) { m_vertexUAV->Release(); m_vertexUAV = nullptr; }
    if (m_indexUAV) { m_indexUAV->Release(); m_indexUAV = nullptr; }

    m_params.maxParticles = maxParticles;
    m_maxVertices = maxParticles * MAX_VERTICES_PER_PARTICLE;
    m_maxIndices = maxParticles * MAX_INDICES_PER_PARTICLE;
    m_paramsDirty = true;

    // 새 크기로 버퍼 재생성
    if (wasInitialized)
    {
        CreateMeshBuffers();
    }
}

bool TrailGenerateModule::InitializeComputeShader()
{
    // 트레일 메쉬 생성 컴퓨트 셰이더 로드
    m_trailMeshCS = ShaderSystem->ComputeShaders["TrailMeshGenerate"].GetShader();
    return m_trailMeshCS != nullptr;
}

bool TrailGenerateModule::CreateBuffers()
{
    // 파라미터 상수 버퍼 생성
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TrailMeshParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_paramsBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create params buffer\n");
        return false;
    }

    // 카운터 버퍼 생성 (정점/인덱스 수 추적용)
    bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(UINT) * 2;  // vertex count, index count
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(UINT);

    hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_counterBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create counter buffer\n");
        return false;
    }

    // 카운터 UAV 생성
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 2;
    uavDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_counterBuffer, &uavDesc, &m_counterUAV);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create counter UAV\n");
        return false;
    }

    // 메쉬 버퍼 생성
    return CreateMeshBuffers();
}

bool TrailGenerateModule::CreateMeshBuffers()
{
    if (m_params.maxParticles == 0)
        return true;

    m_maxVertices = m_params.maxParticles * MAX_VERTICES_PER_PARTICLE;
    m_maxIndices = m_params.maxParticles * MAX_INDICES_PER_PARTICLE;

    // 이전 위치 버퍼 생성
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(Mathf::Vector3) * m_params.maxParticles;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(Mathf::Vector3);

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_prevPositionBufferA);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create previous position buffer\n");
        return false;
    }

    hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_prevPositionBufferB);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create previous position buffer\n");
        return false;
    }


    // 이전 위치 SRV/UAV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_params.maxParticles;

    hr = DeviceState::g_pDevice->CreateShaderResourceView(m_prevPositionBufferA, &srvDesc, &m_prevPositionSRV_A);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create previous position SRV\n");
        return false;
    }
    hr = DeviceState::g_pDevice->CreateShaderResourceView(m_prevPositionBufferB, &srvDesc, &m_prevPositionSRV_B);


    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = m_params.maxParticles;
    uavDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_prevPositionBufferA, &uavDesc, &m_prevPositionUAV_A);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create previous position UAV\n");
        return false;
    }
    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_prevPositionBufferB, &uavDesc, &m_prevPositionUAV_B);

    // 동적 정점 버퍼 생성
    bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TrailVertex) * m_maxVertices;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(TrailVertex);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    bufferDesc.StructureByteStride = 0;

    hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_vertexBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create vertex buffer\n");
        return false;
    }

    // 정점 UAV 생성
    uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = (sizeof(TrailVertex) * m_maxVertices) / 4;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_vertexBuffer, &uavDesc, &m_vertexUAV);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create vertex UAV\n");
        return false;
    }

    // 동적 인덱스 버퍼 생성
    bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(UINT) * m_maxIndices;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    bufferDesc.StructureByteStride = 0;

    hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_indexBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create index buffer\n");
        return false;
    }

    // 인덱스 UAV 생성
    uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = m_maxIndices;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_indexBuffer, &uavDesc, &m_indexUAV);
    if (FAILED(hr))
    {
        OutputDebugStringA("TrailGenerateModule: Failed to create index UAV\n");
        return false;
    }

    char debugMsg[256];
    sprintf_s(debugMsg, "TrailGenerateModule: Created mesh buffers (Max vertices: %u, Max indices: %u)\n",
        m_maxVertices, m_maxIndices);
    OutputDebugStringA(debugMsg);

    return true;
}

void TrailGenerateModule::UpdateConstantBuffer()
{
    if (!m_paramsDirty || !m_paramsBuffer)
        return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = DeviceState::g_pDeviceContext->Map(m_paramsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, &m_params, sizeof(TrailMeshParams));
        DeviceState::g_pDeviceContext->Unmap(m_paramsBuffer, 0);
        m_paramsDirty = false;
    }
}

void TrailGenerateModule::UpdateMeshGeneration()
{
    if (!m_trailMeshCS || m_params.maxParticles == 0)
        return;

    // 1. 먼저 모든 리소스 언바인딩
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 2, nullSRVs);

    // 현재 읽기/쓰기 버퍼 결정
    ID3D11ShaderResourceView* readSRV = m_usingPrevBufferA ? m_prevPositionSRV_A : m_prevPositionSRV_B;
    ID3D11UnorderedAccessView* writeUAV = m_usingPrevBufferA ? m_prevPositionUAV_B : m_prevPositionUAV_A;

    // 2. 카운터 초기화
    UINT clearValue[4] = { 0, 0, 0, 0 };
    DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_counterUAV, clearValue);

    // 3. 컴퓨트 셰이더 바인딩
    DeviceState::g_pDeviceContext->CSSetShader(m_trailMeshCS, nullptr, 0);

    // 4. 상수 버퍼 바인딩
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, &m_paramsBuffer);

    // 5. 입력 리소스 바인딩
    ID3D11ShaderResourceView* srvs[] = {
        m_inputSRV,         // t0: 현재 파티클 데이터
        readSRV             // t1: 이전 위치 데이터
    };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 2, srvs);

    // 6. 출력 리소스 바인딩
    ID3D11UnorderedAccessView* uavs[] = {
        writeUAV,           // u0: 이전 위치 업데이트
        m_vertexUAV,        // u1: 생성된 정점
        m_indexUAV,         // u2: 생성된 인덱스
        m_counterUAV        // u3: 카운터
    };

    UINT initCounts[] = { 0, 0, 0, 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, uavs, initCounts);

    // 7. 디스패치 실행
    UINT numThreadGroups = (m_params.maxParticles + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 8. 리소스 정리 (기존과 동일)
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 2, nullSRVs);

    m_usingPrevBufferA = !m_usingPrevBufferA;

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);
}

void TrailGenerateModule::ClearMeshData()
{
    // GPU 버퍼 클리어 (렌더 스레드에서만 호출되어야 함)
    if (m_vertexUAV)
    {
        UINT clearValue[4] = { 0, 0, 0, 0 };
        DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_vertexUAV, clearValue);
    }

    if (m_indexUAV)
    {
        UINT clearValue[4] = { 0, 0, 0, 0 };
        DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_indexUAV, clearValue);
    }

    if (m_counterUAV)
    {
        UINT clearValue[4] = { 0, 0, 0, 0 };
        DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_counterUAV, clearValue);
    }
}

void TrailGenerateModule::ReleaseResources()
{
    if (m_trailMeshCS) { m_trailMeshCS->Release(); m_trailMeshCS = nullptr; }
    if (m_paramsBuffer) { m_paramsBuffer->Release(); m_paramsBuffer = nullptr; }
    if (m_prevPositionBufferA) { m_prevPositionBufferA->Release(); m_prevPositionBufferA = nullptr; }
    if (m_prevPositionBufferB) { m_prevPositionBufferB->Release(); m_prevPositionBufferB = nullptr; }
    if (m_prevPositionSRV_A) { m_prevPositionSRV_A->Release(); m_prevPositionSRV_A = nullptr; }
    if (m_prevPositionSRV_B) { m_prevPositionSRV_B->Release(); m_prevPositionSRV_B = nullptr; }
    if (m_prevPositionUAV_A) { m_prevPositionUAV_A->Release(); m_prevPositionUAV_A = nullptr; }
    if (m_prevPositionUAV_B) { m_prevPositionUAV_B->Release(); m_prevPositionUAV_B = nullptr; }
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_indexBuffer) { m_indexBuffer->Release(); m_indexBuffer = nullptr; }
    if (m_vertexUAV) { m_vertexUAV->Release(); m_vertexUAV = nullptr; }
    if (m_indexUAV) { m_indexUAV->Release(); m_indexUAV = nullptr; }
    if (m_counterBuffer) { m_counterBuffer->Release(); m_counterBuffer = nullptr; }
    if (m_counterUAV) { m_counterUAV->Release(); m_counterUAV = nullptr; }
}

// 설정 메서드들
void TrailGenerateModule::SetMinDistance(float distance)
{
    if (m_params.minDistance != distance)
    {
        m_params.minDistance = distance;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetTrailWidth(float width)
{
    if (m_params.trailWidth != width)
    {
        m_params.trailWidth = width;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetVelocityThreshold(float threshold)
{
    if (m_params.velocityThreshold != threshold)
    {
        m_params.velocityThreshold = threshold;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetMaxTrailLength(float length)
{
    if (m_params.maxTrailLength != length)
    {
        m_params.maxTrailLength = length;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetWidthOverLength(float curve)
{
    if (m_params.widthOverLength != curve)
    {
        m_params.widthOverLength = curve;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetTrailColor(const Mathf::Vector4& color)
{
    m_params.trailColor = color;
    m_paramsDirty = true;
}

void TrailGenerateModule::SetUVTiling(float tiling)
{
    if (m_params.uvTiling != tiling)
    {
        m_params.uvTiling = tiling;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::SetUVScrollSpeed(float speed)
{
    if (m_params.uvScrollSpeed != speed)
    {
        m_params.uvScrollSpeed = speed;
        m_paramsDirty = true;
    }
}

void TrailGenerateModule::EnableTrail(bool enable)
{
    UINT newValue = enable ? 1 : 0;
    if (m_params.enableTrail != newValue)
    {
        m_params.enableTrail = newValue;
        m_paramsDirty = true;
    }
}

// 직렬화 지원
nlohmann::json TrailGenerateModule::SerializeData() const
{
    nlohmann::json json;

    // 파라미터 직렬화
    json["params"] = {
        {"minDistance", m_params.minDistance},
        {"trailWidth", m_params.trailWidth},
        {"enableTrail", m_params.enableTrail != 0},
        {"velocityThreshold", m_params.velocityThreshold},
        {"maxTrailLength", m_params.maxTrailLength},
        {"widthOverLength", m_params.widthOverLength},
        {"trailColor", {
            {"x", m_params.trailColor.x},
            {"y", m_params.trailColor.y},
            {"z", m_params.trailColor.z},
            {"w", m_params.trailColor.w}
        }},
        {"uvTiling", m_params.uvTiling},
        {"uvScrollSpeed", m_params.uvScrollSpeed}
    };

    // 상태 정보
    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"maxParticles", m_params.maxParticles}
    };

    return json;
}

void TrailGenerateModule::DeserializeData(const nlohmann::json& json)
{
    // 파라미터 복원
    if (json.contains("params"))
    {
        const auto& paramsJson = json["params"];

        if (paramsJson.contains("minDistance"))
            m_params.minDistance = paramsJson["minDistance"];

        if (paramsJson.contains("trailWidth"))
            m_params.trailWidth = paramsJson["trailWidth"];

        if (paramsJson.contains("enableTrail"))
            m_params.enableTrail = paramsJson["enableTrail"].get<bool>() ? 1 : 0;

        if (paramsJson.contains("velocityThreshold"))
            m_params.velocityThreshold = paramsJson["velocityThreshold"];

        if (paramsJson.contains("maxTrailLength"))
            m_params.maxTrailLength = paramsJson["maxTrailLength"];

        if (paramsJson.contains("widthOverLength"))
            m_params.widthOverLength = paramsJson["widthOverLength"];

        if (paramsJson.contains("trailColor"))
        {
            const auto& colorJson = paramsJson["trailColor"];
            m_params.trailColor.x = colorJson.value("x", 1.0f);
            m_params.trailColor.y = colorJson.value("y", 1.0f);
            m_params.trailColor.z = colorJson.value("z", 1.0f);
            m_params.trailColor.w = colorJson.value("w", 1.0f);
        }

        if (paramsJson.contains("uvTiling"))
            m_params.uvTiling = paramsJson["uvTiling"];

        if (paramsJson.contains("uvScrollSpeed"))
            m_params.uvScrollSpeed = paramsJson["uvScrollSpeed"];
    }

    // 상태 정보 복원
    if (json.contains("state"))
    {
        const auto& stateJson = json["state"];

        if (stateJson.contains("maxParticles"))
        {
            UINT maxParticles = stateJson["maxParticles"];
            if (maxParticles != m_params.maxParticles)
            {
                OnSystemResized(maxParticles);
            }
        }
    }

    if (!m_isInitialized)
        Initialize();

    m_paramsDirty = true;
}

std::string TrailGenerateModule::GetModuleType() const
{
    return "TrailGenerateModule";
}