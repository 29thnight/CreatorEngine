#include "SizeModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"

SizeModuleCS::SizeModuleCS()
    : m_computeShader(nullptr)
    , m_sizeParamsBuffer(nullptr)
    , m_paramsDirty(true)
    , m_easingEnable(false)
    , m_isInitialized(false)
    , m_particleCapacity(0)
{
    // 기본값 설정
    m_sizeParams.startSize = XMFLOAT2(0.1f, 0.1f);
    m_sizeParams.endSize = XMFLOAT2(1.0f, 1.0f);
    m_sizeParams.deltaTime = 0.0f;
    m_sizeParams.useRandomScale = 0;
    m_sizeParams.randomScaleMin = 0.5f;
    m_sizeParams.randomScaleMax = 2.0f;
    m_sizeParams.maxParticles = 0;
    m_sizeParams.padding1 = 0.0f;
    m_sizeParams.padding2 = 0.0f;
    m_sizeParams.padding3 = 0.0f;
}

SizeModuleCS::~SizeModuleCS()
{
    Release();
}

void SizeModuleCS::Initialize()
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

void SizeModuleCS::Update(float deltaTime)
{
    if (!m_isInitialized)
    {
        OutputDebugStringA("ERROR: SizeModuleCS not initialized!\n");
        return;
    }

    DirectX11::BeginEvent(L"SizeModuleCS Update");

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
    m_paramsDirty = true;

    // 상수 버퍼 업데이트
    UpdateConstantBuffers();

    // 컴퓨트 셰이더 바인딩
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 바인딩
    ID3D11Buffer* constantBuffers[] = { m_sizeParamsBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // 입력 리소스 바인딩
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // 출력 리소스 바인딩
    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
    UINT initCounts[] = { 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    // 디스패치 실행
    UINT numThreadGroups = (m_particleCapacity + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 정리
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void SizeModuleCS::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

void SizeModuleCS::OnSystemResized(UINT maxParticles)
{
    if (maxParticles != m_particleCapacity)
    {
        m_particleCapacity = maxParticles;
        m_sizeParams.maxParticles = maxParticles;
        m_paramsDirty = true;
    }
}

bool SizeModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["SizeModule"].GetShader();
    return m_computeShader != nullptr;
}

bool SizeModuleCS::CreateConstantBuffers()
{
    // Size 파라미터 상수 버퍼
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(SizeParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_sizeParamsBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create size params buffer\n");
        return false;
    }

    return true;
}

void SizeModuleCS::UpdateConstantBuffers()
{
    if (m_paramsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_sizeParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &m_sizeParams, sizeof(SizeParams));
            DeviceState::g_pDeviceContext->Unmap(m_sizeParamsBuffer, 0);
            m_paramsDirty = false;
        }
    }
}

void SizeModuleCS::ReleaseResources()
{
    if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
    if (m_sizeParamsBuffer) { m_sizeParamsBuffer->Release(); m_sizeParamsBuffer = nullptr; }
}

// 설정 메서드들
void SizeModuleCS::SetStartSize(const XMFLOAT2& size)
{
    if (m_sizeParams.startSize.x != size.x || m_sizeParams.startSize.y != size.y)
    {
        m_sizeParams.startSize = size;
        m_paramsDirty = true;
    }
}

void SizeModuleCS::SetEndSize(const XMFLOAT2& size)
{
    if (m_sizeParams.endSize.x != size.x || m_sizeParams.endSize.y != size.y)
    {
        m_sizeParams.endSize = size;
        m_paramsDirty = true;
    }
}

void SizeModuleCS::SetRandomScale(bool enabled, float minScale, float maxScale)
{
    int enabledInt = enabled ? 1 : 0;
    if (m_sizeParams.useRandomScale != enabledInt ||
        m_sizeParams.randomScaleMin != minScale ||
        m_sizeParams.randomScaleMax != maxScale)
    {
        m_sizeParams.useRandomScale = enabledInt;
        m_sizeParams.randomScaleMin = minScale;
        m_sizeParams.randomScaleMax = maxScale;
        m_paramsDirty = true;
    }
}

void SizeModuleCS::SetEasing(EasingEffect easingType, StepAnimation animationType, float duration)
{
    m_easingModule.SetEasinType(easingType);
    m_easingModule.SetAnimationType(animationType);
    m_easingModule.SetDuration(duration);
    m_easingEnable = true;
}