#include "MeshColorModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"

MeshColorModuleCS::MeshColorModuleCS()
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

    m_colorGradient = {
        {0.0f, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f)},
        {0.3f, Mathf::Vector4(1.0f, 1.0f, 0.0f, 0.9f)},
        {0.7f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 0.7f)},
        {1.0f, Mathf::Vector4(0.5f, 0.0f, 0.0f, 0.0f)},
    };
    m_colorParams.gradientSize = static_cast<int>(m_colorGradient.size());
}

MeshColorModuleCS::~MeshColorModuleCS()
{
    Release();
}

void MeshColorModuleCS::Initialize()
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

void MeshColorModuleCS::Update(float deltaTime)
{
    if (!m_enabled) return;

    if (!m_isInitialized)
        return;

    DirectX11::BeginEvent(L"MeshColorModule Update");

    if (!m_inputSRV || !m_outputUAV) {
        DirectX11::EndEvent();
        return;
    }

    m_colorParams.maxParticles = m_particleCapacity;

    float processedDeltaTime = deltaTime;
    if (m_easingEnable)
    {
        float easingValue = m_easingModule.Update(deltaTime);
        processedDeltaTime = deltaTime * easingValue;
    }
    m_colorParams.deltaTime = processedDeltaTime;
    m_colorParamsDirty = true;

    UpdateConstantBuffers();
    UpdateResourceBuffers();

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    ID3D11Buffer* constantBuffers[] = { m_colorParamsBuffer };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    ID3D11ShaderResourceView* srvs[3] = { nullptr, nullptr, nullptr };

    srvs[0] = m_inputSRV;

    if (m_gradientSRV && m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Gradient)) {
        srvs[1] = m_gradientSRV;
    }

    if (m_discreteColorsSRV &&
        (m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Discrete) ||
            m_colorParams.transitionMode == static_cast<int>(ColorTransitionMode::Custom))) {
        srvs[2] = m_discreteColorsSRV;
    }

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 3, srvs);

    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
    UINT initCounts[] = { 0 };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
    DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 3, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void MeshColorModuleCS::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

void MeshColorModuleCS::OnSystemResized(UINT maxParticles)
{
    if (maxParticles != m_particleCapacity)
    {
        m_particleCapacity = maxParticles;
        m_colorParams.maxParticles = maxParticles;
        m_colorParamsDirty = true;
    }
}

bool MeshColorModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["MeshColorModule"].GetShader();
    return m_computeShader != nullptr;
}

bool MeshColorModuleCS::CreateConstantBuffers()
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(MeshColorParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_colorParamsBuffer);
    return SUCCEEDED(hr);
}

bool MeshColorModuleCS::CreateResourceBuffers()
{
    D3D11_BUFFER_DESC gradientDesc = {};
    gradientDesc.ByteWidth = sizeof(MeshGradientPoint) * 16;
    gradientDesc.Usage = D3D11_USAGE_DYNAMIC;
    gradientDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    gradientDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    gradientDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    gradientDesc.StructureByteStride = sizeof(MeshGradientPoint);

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&gradientDesc, nullptr, &m_gradientBuffer);
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC gradientSRVDesc = {};
    gradientSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    gradientSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    gradientSRVDesc.Buffer.NumElements = 16;

    hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_gradientBuffer, &gradientSRVDesc, &m_gradientSRV);
    if (FAILED(hr))
        return false;

    D3D11_BUFFER_DESC discreteDesc = {};
    discreteDesc.ByteWidth = sizeof(Mathf::Vector4) * 32;
    discreteDesc.Usage = D3D11_USAGE_DYNAMIC;
    discreteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    discreteDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    discreteDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    discreteDesc.StructureByteStride = sizeof(Mathf::Vector4);

    hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&discreteDesc, nullptr, &m_discreteColorsBuffer);
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC discreteSRVDesc = {};
    discreteSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    discreteSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    discreteSRVDesc.Buffer.NumElements = 32;

    hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_discreteColorsBuffer, &discreteSRVDesc, &m_discreteColorsSRV);
    return SUCCEEDED(hr);
}

void MeshColorModuleCS::UpdateConstantBuffers()
{
    if (m_colorParamsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_colorParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &m_colorParams, sizeof(MeshColorParams));
            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_colorParamsBuffer, 0);
            m_colorParamsDirty = false;
        }
    }
}

void MeshColorModuleCS::UpdateResourceBuffers()
{
    if (m_gradientDirty && !m_colorGradient.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_gradientBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            MeshGradientPoint* gradientPoints = reinterpret_cast<MeshGradientPoint*>(mappedResource.pData);

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

void MeshColorModuleCS::ReleaseResources()
{
    if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
    if (m_colorParamsBuffer) { m_colorParamsBuffer->Release(); m_colorParamsBuffer = nullptr; }
    if (m_gradientBuffer) { m_gradientBuffer->Release(); m_gradientBuffer = nullptr; }
    if (m_discreteColorsBuffer) { m_discreteColorsBuffer->Release(); m_discreteColorsBuffer = nullptr; }
    if (m_gradientSRV) { m_gradientSRV->Release(); m_gradientSRV = nullptr; }
    if (m_discreteColorsSRV) { m_discreteColorsSRV->Release(); m_discreteColorsSRV = nullptr; }
}

void MeshColorModuleCS::SetColorGradient(const std::vector<std::pair<float, Mathf::Vector4>>& gradient)
{
    m_colorGradient = gradient;
    m_colorParams.gradientSize = static_cast<int>(gradient.size());
    m_gradientDirty = true;
    m_colorParamsDirty = true;
}

void MeshColorModuleCS::SetTransitionMode(ColorTransitionMode mode)
{
    int modeInt = static_cast<int>(mode);
    if (m_colorParams.transitionMode != modeInt)
    {
        m_colorParams.transitionMode = modeInt;
        m_colorParamsDirty = true;
    }
}

void MeshColorModuleCS::SetDiscreteColors(const std::vector<Mathf::Vector4>& colors)
{
    m_discreteColors = colors;
    m_colorParams.discreteColorsSize = static_cast<int>(colors.size());
    m_discreteColorsDirty = true;
    m_colorParamsDirty = true;
}

void MeshColorModuleCS::SetCustomFunction(int functionType, float param1, float param2, float param3, float param4)
{
    m_colorParams.customFunctionType = functionType;
    m_colorParams.customParam1 = param1;
    m_colorParams.customParam2 = param2;
    m_colorParams.customParam3 = param3;
    m_colorParams.customParam4 = param4;
    m_colorParamsDirty = true;
}

void MeshColorModuleCS::SetPulseColor(const Mathf::Vector4& baseColor, const Mathf::Vector4& pulseColor, float frequency)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 0;
    m_colorParams.customParam1 = baseColor.x;
    m_colorParams.customParam2 = baseColor.y;
    m_colorParams.customParam3 = baseColor.z;
    m_colorParams.customParam4 = frequency;

    SetDiscreteColors({ baseColor, pulseColor });
}

void MeshColorModuleCS::SetSineWaveColor(const Mathf::Vector4& color1, const Mathf::Vector4& color2, float frequency, float phase)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 1;
    m_colorParams.customParam1 = frequency;
    m_colorParams.customParam2 = phase;

    SetDiscreteColors({ color1, color2 });
}

void MeshColorModuleCS::SetFlickerColor(const std::vector<Mathf::Vector4>& colors, float speed)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 2;
    m_colorParams.customParam1 = speed;

    SetDiscreteColors(colors);
}

void MeshColorModuleCS::SetRandomColors(const std::vector<Mathf::Vector4>& colors)
{
    SetTransitionMode(ColorTransitionMode::Custom);
    m_colorParams.customFunctionType = 3; // RANDOM_COLOR_FUNCTION

    m_colorParams.customParam1 = 0.0f; // 시간 변화 강도 (나중에 사용)
    m_colorParams.customParam2 = 0.0f; // 변화 속도 (나중에 사용) 
    m_colorParams.customParam3 = 0.0f; // 예약
    m_colorParams.customParam4 = 0.0f; // 예약

    SetDiscreteColors(colors);
}


void MeshColorModuleCS::SetEasing(EasingEffect easingType, StepAnimation animationType, float duration)
{
    m_easingModule.SetEasingType(easingType);
    m_easingModule.SetAnimationType(animationType);
    m_easingModule.SetDuration(duration);
    m_easingEnable = true;
}

void MeshColorModuleCS::ResetForReuse()
{
    if (!m_enabled) return;

    m_colorParams.deltaTime = 0.0f;

    m_colorParamsDirty = true;
    m_gradientDirty = true;
    m_discreteColorsDirty = true;

    if (m_easingEnable) {
        m_easingModule.Reset();
    }
}

bool MeshColorModuleCS::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_colorParamsBuffer != nullptr &&
        m_gradientBuffer != nullptr &&
        m_discreteColorsBuffer != nullptr &&
        m_gradientSRV != nullptr &&
        m_discreteColorsSRV != nullptr;
}

nlohmann::json MeshColorModuleCS::SerializeData() const
{
    nlohmann::json json;

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

    json["easing"] = {
        {"enabled", m_easingEnable}
    };

    if (m_easingEnable)
    {
        json["easing"]["easingType"] = static_cast<int>(m_easingModule.GetEasingType());
        json["easing"]["animationType"] = static_cast<int>(m_easingModule.GetAnimationType());
        json["easing"]["duration"] = m_easingModule.GetDuration();
    }

    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}

void MeshColorModuleCS::DeserializeData(const nlohmann::json& json)
{
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

    if (json.contains("state"))
    {
        const auto& stateJson = json["state"];

        if (stateJson.contains("particleCapacity"))
            m_particleCapacity = stateJson["particleCapacity"];
    }

    if (!m_isInitialized)
        Initialize();

    m_colorParamsDirty = true;
    m_gradientDirty = true;
    m_discreteColorsDirty = true;
}

std::string MeshColorModuleCS::GetModuleType() const
{
    return "MeshColorModuleCS";
}