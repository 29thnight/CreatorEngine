#include "ColorModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"

ColorModuleCS::ColorModuleCS()
    : m_computeShader(nullptr)
    , m_colorParamsBuffer(nullptr)
    , m_gradientBuffer(nullptr)
    , m_discreteColorsBuffer(nullptr)
    , m_gradientSRV(nullptr)
    , m_discreteColorsSRV(nullptr)
    , m_colorParamsDirty(true)
    , m_gradientDirty(true)
    , m_discreteColorsDirty(true)
    , m_particleCapacity(0)
    , m_easingEnable(false)
{
    // 기본값 설정
    m_colorParams.deltaTime = 0.0f;
    m_colorParams.transitionMode = static_cast<int>(ColorTransitionMode::Gradient);
    m_colorParams.gradientSize = 0;
    m_colorParams.discreteColorsSize = 0;
    m_colorParams.customFunctionType = 0;
    m_colorParams.customParam1 = 0.0f;
    m_colorParams.customParam2 = 0.0f;
    m_colorParams.customParam3 = 0.0f;
    m_colorParams.customParam4 = 0.0f;
    m_colorParams.maxParticles = 0;

    // 기본 색상 그라데이션 설정
    m_colorGradient = {
        {0.0f, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f)},
        {0.3f, Mathf::Vector4(1.0f, 1.0f, 0.0f, 0.9f)},
        {0.7f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 0.7f)},
        {1.0f, Mathf::Vector4(0.5f, 0.0f, 0.0f, 0.0f)},
    };
    m_colorParams.gradientSize = static_cast<int>(m_colorGradient.size());
}

ColorModuleCS::~ColorModuleCS()
{
    Release();
}

void ColorModuleCS::Initialize()
{

    if (m_isInitialized)
        return;

    if (!InitializeComputeShader())
        return;

    if (!CreateConstantBuffers())
        return;

    if (!CreateResourceBuffers())
        return;

    m_isInitialized = true;
}

void ColorModuleCS::Update(float deltaTime)
{
    if (!m_enabled) return;


    if (!m_isInitialized)
        return;

    DirectX11::BeginEvent(L"ColorModule Update");

    if (!m_inputSRV || !m_outputUAV) {
        DirectX11::EndEvent();
        return;
    }

    // 파티클 용량 업데이트
    m_colorParams.maxParticles = m_particleCapacity;

    // 이징 처리
    float processedDeltaTime = deltaTime;
    if (m_easingEnable)
    {
        float easingValue = m_easingModule.Update(deltaTime);
        processedDeltaTime = deltaTime * easingValue;
    }
    m_colorParams.deltaTime = processedDeltaTime;
    m_colorParamsDirty = true;

    // 상수 버퍼 및 리소스 업데이트
    UpdateConstantBuffers();
    UpdateResourceBuffers();

    // 컴퓨트 셰이더 바인딩
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 바인딩
    ID3D11Buffer* constantBuffers[] = { m_colorParamsBuffer };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // 입력 리소스 바인딩
    ID3D11ShaderResourceView* srvs[3] = { nullptr, nullptr, nullptr };

    // t0: InputParticles (항상 바인딩)
    srvs[0] = m_inputSRV;

    // t1: GradientBuffer (Gradient 모드일 때만)
    if (m_gradientSRV && m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Gradient)) {
        srvs[1] = m_gradientSRV;
    }

    // t2: DiscreteColors (Discrete 또는 Custom 모드일 때)
    if (m_discreteColorsSRV &&
        (m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Discrete) ||
            m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Custom))) {
        srvs[2] = m_discreteColorsSRV;
    }

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 3, srvs);

    // 출력 리소스 바인딩
    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
    UINT initCounts[] = { 0 };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    // 디스패치 실행
    UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
    DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 정리
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 3, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void ColorModuleCS::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

void ColorModuleCS::OnSystemResized(UINT maxParticles)
{
    if (maxParticles != m_particleCapacity)
    {
        m_particleCapacity = maxParticles;
        m_colorParams.maxParticles = maxParticles;
        m_colorParamsDirty = true;
    }
}

bool ColorModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["ColorModule"].GetShader();
    return m_computeShader != nullptr;
}

bool ColorModuleCS::CreateConstantBuffers()
{
    // 색상 파라미터 상수 버퍼
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(ColorParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_colorParamsBuffer);
    return SUCCEEDED(hr);
}

bool ColorModuleCS::CreateResourceBuffers()
{
    // 그라데이션 버퍼 생성
    D3D11_BUFFER_DESC gradientDesc = {};
    gradientDesc.ByteWidth = sizeof(GradientPoint) * 16; // 최대 16개 그라데이션 포인트
    gradientDesc.Usage = D3D11_USAGE_DYNAMIC;
    gradientDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    gradientDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    gradientDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    gradientDesc.StructureByteStride = sizeof(GradientPoint);

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&gradientDesc, nullptr, &m_gradientBuffer);
    if (FAILED(hr))
        return false;

    // 그라데이션 SRV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC gradientSRVDesc = {};
    gradientSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    gradientSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    gradientSRVDesc.Buffer.NumElements = 16;

    hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_gradientBuffer, &gradientSRVDesc, &m_gradientSRV);
    if (FAILED(hr))
        return false;

    // 이산 색상 버퍼 생성
    D3D11_BUFFER_DESC discreteDesc = {};
    discreteDesc.ByteWidth = sizeof(Mathf::Vector4) * 32; // 최대 32개 색상
    discreteDesc.Usage = D3D11_USAGE_DYNAMIC;
    discreteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    discreteDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    discreteDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    discreteDesc.StructureByteStride = sizeof(Mathf::Vector4);

    hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&discreteDesc, nullptr, &m_discreteColorsBuffer);
    if (FAILED(hr))
        return false;

    // 이산 색상 SRV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC discreteSRVDesc = {};
    discreteSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    discreteSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    discreteSRVDesc.Buffer.NumElements = 32;

    hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_discreteColorsBuffer, &discreteSRVDesc, &m_discreteColorsSRV);
    return SUCCEEDED(hr);
}

void ColorModuleCS::UpdateConstantBuffers()
{
    if (m_colorParamsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_colorParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &m_colorParams, sizeof(ColorParams));
            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_colorParamsBuffer, 0);
            m_colorParamsDirty = false;
        }
    }
}

void ColorModuleCS::UpdateResourceBuffers()
{
    // 그라데이션 버퍼 업데이트
    if (m_gradientDirty && !m_colorGradient.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_gradientBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            GradientPoint* gradientPoints = reinterpret_cast<GradientPoint*>(mappedResource.pData);

            size_t count = std::min(m_colorGradient.size(), static_cast<size_t>(16));
            for (size_t i = 0; i < count; ++i)
            {
                gradientPoints[i].time = m_colorGradient[i].first;
                gradientPoints[i].color = m_colorGradient[i].second;
            }

            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_gradientBuffer, 0);
            m_gradientDirty = false;
        }
    }

    // 이산 색상 버퍼 업데이트
    if (m_discreteColorsDirty && !m_discreteColors.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_discreteColorsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            Mathf::Vector4* colors = reinterpret_cast<Mathf::Vector4*>(mappedResource.pData);

            size_t count = std::min(m_discreteColors.size(), static_cast<size_t>(32));
            for (size_t i = 0; i < count; ++i)
            {
                colors[i] = m_discreteColors[i];
            }

            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_discreteColorsBuffer, 0);
            m_discreteColorsDirty = false;
        }
    }
}

void ColorModuleCS::ReleaseResources()
{
    if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
    if (m_colorParamsBuffer) { m_colorParamsBuffer->Release(); m_colorParamsBuffer = nullptr; }
    if (m_gradientBuffer) { m_gradientBuffer->Release(); m_gradientBuffer = nullptr; }
    if (m_discreteColorsBuffer) { m_discreteColorsBuffer->Release(); m_discreteColorsBuffer = nullptr; }
    if (m_gradientSRV) { m_gradientSRV->Release(); m_gradientSRV = nullptr; }
    if (m_discreteColorsSRV) { m_discreteColorsSRV->Release(); m_discreteColorsSRV = nullptr; }
}

// 설정 메서드들
void ColorModuleCS::SetColorGradient(const std::vector<std::pair<float, Mathf::Vector4>>& gradient)
{
    m_colorGradient = gradient;
    m_colorParams.gradientSize = static_cast<int>(gradient.size());
    m_gradientDirty = true;
    m_colorParamsDirty = true;
}

void ColorModuleCS::SetTransitionMode(ColorTransitionMode mode)
{
    int modeInt = static_cast<int>(mode);
    if (m_colorParams.transitionMode != modeInt)
    {
        m_colorParams.transitionMode = modeInt;
        m_colorParamsDirty = true;
    }
}

void ColorModuleCS::SetDiscreteColors(const std::vector<Mathf::Vector4>& colors)
{
    m_discreteColors = colors;
    m_colorParams.discreteColorsSize = static_cast<int>(colors.size());
    m_discreteColorsDirty = true;
    m_colorParamsDirty = true;
}

void ColorModuleCS::SetCustomFunction(int functionType, float param1, float param2, float param3, float param4)
{
    m_colorParams.customFunctionType = functionType;
    m_colorParams.customParam1 = param1;
    m_colorParams.customParam2 = param2;
    m_colorParams.customParam3 = param3;
    m_colorParams.customParam4 = param4;
    m_colorParamsDirty = true;
}

void ColorModuleCS::SetPulseColor(const Mathf::Vector4& baseColor, const Mathf::Vector4& pulseColor, float frequency)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 0; // PULSE_FUNCTION
    m_colorParams.customParam1 = baseColor.x;  // baseColor.r
    m_colorParams.customParam2 = baseColor.y;  // baseColor.g
    m_colorParams.customParam3 = baseColor.z;  // baseColor.b
    m_colorParams.customParam4 = frequency;

    // pulseColor는 discrete colors의 첫 번째로 저장
    SetDiscreteColors({ baseColor, pulseColor });
}

void ColorModuleCS::SetSineWaveColor(const Mathf::Vector4& color1, const Mathf::Vector4& color2, float frequency, float phase)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 1; // SINE_WAVE_FUNCTION
    m_colorParams.customParam1 = frequency;
    m_colorParams.customParam2 = phase;

    // 두 색상을 discrete colors에 저장
    SetDiscreteColors({ color1, color2 });
}

void ColorModuleCS::SetFlickerColor(const std::vector<Mathf::Vector4>& colors, float speed)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 2; // FLICKER_FUNCTION
    m_colorParams.customParam1 = speed;

    SetDiscreteColors(colors);
}

void ColorModuleCS::SetEasing(EasingEffect easingType, StepAnimation animationType, float duration)
{
    m_easingModule.SetEasingType(easingType);
    m_easingModule.SetAnimationType(animationType);
    m_easingModule.SetDuration(duration);
    m_easingEnable = true;
}

void ColorModuleCS::ResetForReuse()
{
    if (!m_enabled) return;

    // 시간 관련 상태 초기화
    m_colorParams.deltaTime = 0.0f;

    // 더티 플래그 설정
    m_colorParamsDirty = true;
    m_gradientDirty = true;
    m_discreteColorsDirty = true;

    // 이징 모듈 리셋
    if (m_easingEnable) {
        m_easingModule.Reset();
    }
}

bool ColorModuleCS::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_colorParamsBuffer != nullptr &&
        m_gradientBuffer != nullptr &&
        m_discreteColorsBuffer != nullptr &&
        m_gradientSRV != nullptr &&
        m_discreteColorsSRV != nullptr;
}

nlohmann::json ColorModuleCS::SerializeData() const
{
    nlohmann::json json;

    // ColorParams 직렬화
    json["colorParams"] = {
        {"transitionMode", m_colorParams.transitionMode},
        {"gradientSize", m_colorParams.gradientSize},
        {"discreteColorsSize", m_colorParams.discreteColorsSize},
        {"customFunctionType", m_colorParams.customFunctionType},
        {"customParam1", m_colorParams.customParam1},
        {"customParam2", m_colorParams.customParam2},
        {"customParam3", m_colorParams.customParam3},
        {"customParam4", m_colorParams.customParam4}
    };

    // 색상 그라데이션 직렬화
    json["colorGradient"] = nlohmann::json::array();
    for (const auto& gradientPoint : m_colorGradient)
    {
        nlohmann::json point;
        point["time"] = gradientPoint.first;
        point["color"] = {
            {"x", gradientPoint.second.x},
            {"y", gradientPoint.second.y},
            {"z", gradientPoint.second.z},
            {"w", gradientPoint.second.w}
        };
        json["colorGradient"].push_back(point);
    }

    // 이산 색상 직렬화
    json["discreteColors"] = nlohmann::json::array();
    for (const auto& color : m_discreteColors)
    {
        nlohmann::json colorJson = {
            {"x", color.x},
            {"y", color.y},
            {"z", color.z},
            {"w", color.w}
        };
        json["discreteColors"].push_back(colorJson);
    }

    // 이징 설정 직렬화
    json["easing"] = {
        {"enabled", m_easingEnable}
    };

    if (m_easingEnable)
    {
        json["easing"]["easingType"] = static_cast<int>(m_easingModule.GetEasingType());
        json["easing"]["animationType"] = static_cast<int>(m_easingModule.GetAnimationType());
        json["easing"]["duration"] = m_easingModule.GetDuration();
    }

    // 상태 정보
    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}

void ColorModuleCS::DeserializeData(const nlohmann::json& json)
{
    // ColorParams 복원
    if (json.contains("colorParams"))
    {
        const auto& colorParamsJson = json["colorParams"];

        if (colorParamsJson.contains("transitionMode"))
            m_colorParams.transitionMode = colorParamsJson["transitionMode"];

        if (colorParamsJson.contains("gradientSize"))
            m_colorParams.gradientSize = colorParamsJson["gradientSize"];

        if (colorParamsJson.contains("discreteColorsSize"))
            m_colorParams.discreteColorsSize = colorParamsJson["discreteColorsSize"];

        if (colorParamsJson.contains("customFunctionType"))
            m_colorParams.customFunctionType = colorParamsJson["customFunctionType"];

        if (colorParamsJson.contains("customParam1"))
            m_colorParams.customParam1 = colorParamsJson["customParam1"];

        if (colorParamsJson.contains("customParam2"))
            m_colorParams.customParam2 = colorParamsJson["customParam2"];

        if (colorParamsJson.contains("customParam3"))
            m_colorParams.customParam3 = colorParamsJson["customParam3"];

        if (colorParamsJson.contains("customParam4"))
            m_colorParams.customParam4 = colorParamsJson["customParam4"];
    }

    // 색상 그라데이션 복원
    if (json.contains("colorGradient") && json["colorGradient"].is_array())
    {
        m_colorGradient.clear();
        for (const auto& pointJson : json["colorGradient"])
        {
            float time = pointJson.value("time", 0.0f);
            Mathf::Vector4 color(
                pointJson["color"].value("x", 1.0f),
                pointJson["color"].value("y", 1.0f),
                pointJson["color"].value("z", 1.0f),
                pointJson["color"].value("w", 1.0f)
            );
            m_colorGradient.emplace_back(time, color);
        }
        m_colorParams.gradientSize = static_cast<int>(m_colorGradient.size());
    }

    // 이산 색상 복원
    if (json.contains("discreteColors") && json["discreteColors"].is_array())
    {
        m_discreteColors.clear();
        for (const auto& colorJson : json["discreteColors"])
        {
            Mathf::Vector4 color(
                colorJson.value("x", 1.0f),
                colorJson.value("y", 1.0f),
                colorJson.value("z", 1.0f),
                colorJson.value("w", 1.0f)
            );
            m_discreteColors.push_back(color);
        }
        m_colorParams.discreteColorsSize = static_cast<int>(m_discreteColors.size());
    }

    // 이징 설정 복원
    if (json.contains("easing"))
    {
        const auto& easingJson = json["easing"];

        if (easingJson.contains("enabled"))
            m_easingEnable = easingJson["enabled"];

        if (m_easingEnable && easingJson.contains("easingType") &&
            easingJson.contains("animationType") && easingJson.contains("duration"))
        {
            EasingEffect easingType = static_cast<EasingEffect>(easingJson["easingType"]);
            StepAnimation animationType = static_cast<StepAnimation>(easingJson["animationType"]);
            float duration = easingJson["duration"];

            SetEasing(easingType, animationType, duration);
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
    m_colorParamsDirty = true;
    m_gradientDirty = true;
    m_discreteColorsDirty = true;
}

std::string ColorModuleCS::GetModuleType() const
{
    return "ColorModuleCS";
}