#include "../ShaderSystem.h"
#include "LifeModuleCS.h"


void LifeModuleCS::Initialize()
{
	m_computeShader = ShaderSystem->ComputeShaders["LifeModule"].GetShader();
	InitializeCompute();
}

void LifeModuleCS::Update(float delta, std::vector<ParticleData>& particles)
{
    DirectX11::BeginEvent(L"LifeModuleCS");

    // 파티클 배열 크기 저장
    m_particlesCapacity = particles.size();

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(delta);

    // 컴퓨트 셰이더 설정
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 설정
    ID3D11Buffer* constantBuffers[] = { m_lifeParamsBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // 입력 버퍼 설정
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // 출력 버퍼 설정 - 파티클 데이터, 비활성 인덱스, 비활성 카운터, 활성 카운터
    ID3D11UnorderedAccessView* uavs[] = {
     m_outputUAV,           // 파티클 데이터 출력
     m_inactiveIndicesUAV,  // 비활성 파티클 인덱스 (Life 모듈에서 수명이 다한 파티클을 여기에 추가)
     m_inactiveCountUAV,    // 비활성 파티클 카운터 (비활성 파티클의 수를 추적)
     m_activeCountUAV       // 활성 파티클 카운터 (활성 파티클의 수를 추적)
    };
    UINT initCounts[] = { 0, 0, 0, 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, uavs, initCounts);

    // 컴퓨트 셰이더 실행
    UINT numThreadGroups = (std::max<UINT>)(1, (static_cast<UINT>(particles.size()) + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE);
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 해제
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();

}

void LifeModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particlesCapacity)
    {
        m_particlesCapacity = max;
        m_paramsDirty = true;
    }
}

bool LifeModuleCS::InitializeCompute()
{
    // 상수 버퍼 생성
    D3D11_BUFFER_DESC lifeParamsDesc = {};
    lifeParamsDesc.ByteWidth = sizeof(LifeParams);
    lifeParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    lifeParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lifeParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&lifeParamsDesc, nullptr, &m_lifeParamsBuffer);
    if (FAILED(hr))
        return false;

    // CPU 읽기용 스테이징 버퍼 생성
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = sizeof(UINT);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    hr = DeviceState::g_pDevice->CreateBuffer(&stagingDesc, nullptr, &m_activeCountStagingBuffer);
    if (FAILED(hr))
        return false;

    m_isInitialized = true;
    return true;
}

UINT LifeModuleCS::GetActiveParticleCount()
{
    // 아직 초기화되지 않았거나 활성 카운터 UAV가 없는 경우
    if (!m_isInitialized || !m_activeCountUAV)
        return 0;

    // 스테이징 버퍼가 없으면 생성
    if (!m_activeCountStagingBuffer)
    {
        D3D11_BUFFER_DESC stagingDesc = {};
        stagingDesc.ByteWidth = sizeof(UINT);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&stagingDesc, nullptr, &m_activeCountStagingBuffer);
        if (FAILED(hr))
            return 0;
    }

    // 카운터 버퍼에서 스테이징 버퍼로 복사
    // 이 부분은 ParticleSystem에서 공유하는 m_activeCountBuffer를 사용
    // 활성 카운터 UAV의 기반이 되는 버퍼를 가져와야 함
    ID3D11Resource* activeCountResource = nullptr;
    m_activeCountUAV->GetResource(&activeCountResource);

    if (activeCountResource)
    {
        DeviceState::g_pDeviceContext->CopyResource(m_activeCountStagingBuffer, activeCountResource);
        activeCountResource->Release();

        // 스테이징 버퍼에서 CPU로 데이터 읽기
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_activeCountStagingBuffer, 0, D3D11_MAP_READ, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            UINT count = *reinterpret_cast<UINT*>(mappedResource.pData);
            DeviceState::g_pDeviceContext->Unmap(m_activeCountStagingBuffer, 0);
            return count;
        }
    }

    return 0;
}

void LifeModuleCS::UpdateConstantBuffers(float delta)
{
    // 파라미터 업데이트가 필요한 경우에만 상수 버퍼 업데이트
    if (m_paramsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_lifeParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            LifeParams* params = reinterpret_cast<LifeParams*>(mappedResource.pData);
            params->deltaTime = delta;
            params->maxParticles = m_particlesCapacity;
            params->resetInactiveCounter = 1; // 프레임마다 비활성 카운터 리셋

            DeviceState::g_pDeviceContext->Unmap(m_lifeParamsBuffer, 0);
            m_paramsDirty = false;
        }
    }
}

void LifeModuleCS::Release()
{
    // 리소스 해제
    if (m_computeShader) m_computeShader->Release();
    if (m_lifeParamsBuffer) m_lifeParamsBuffer->Release();

    // 포인터 초기화
    m_computeShader = nullptr;
    m_lifeParamsBuffer = nullptr;
    m_inputSRV = nullptr;
    m_outputUAV = nullptr;
    m_inactiveIndicesUAV = nullptr;
    m_inactiveCountUAV = nullptr;
    m_activeCountUAV = nullptr;

    m_isInitialized = false;
}
