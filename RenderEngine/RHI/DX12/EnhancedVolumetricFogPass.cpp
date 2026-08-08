#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedVolumetricFogPass.h"
#include "EnhancedVolumetricFogShaders.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string FogHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    /// 산란·누적이 함께 쓰는 상수(셰이더 b0). DX11 MainCB와 같은 배치다.
    struct FogConstants
    {
        Mathf::Matrix  inverseViewProjection{};
        Mathf::Matrix  previousViewProjection{};
        Mathf::Matrix  shadowMatrix{};
        Mathf::Vector4 sunDirection{};    // 죽어 있다(원본 ①)
        Mathf::Vector4 sunColor{};        // 죽어 있다(원본 ①)
        Mathf::Vector4 cameraPosition{};
        Mathf::Vector4 nearFarFrameBlend{};
        Mathf::Vector4 volumeSize{};
        float          anisotropy{ 0.f };
        float          density{ 0.f };
        float          strength{ 0.f };
        float          thicknessFactor{ 0.f };   // 죽어 있다(원본 ④)
    };

    /// 구름 그림자 상수(셰이더 b1).
    struct FogCloudConstants
    {
        Mathf::Matrix viewProjection{};
        float         cloudMapSize[2]{};
        float         size[2]{};
        float         direction[2]{};
        uint32_t      frameIndex{ 0 };
        float         moveSpeed{ 0.f };
        float         alpha{ 0.f };
        int32_t       isOn{ 0 };
        float         padding[2]{};
    };

    /// 셰이더가 읽는 광원 하나(b2의 원소). HLSL 배치와 같아야 한다 —
    /// 어긋나면 값이 조용히 밀려 '포그가 이상하다'로만 드러난다.
    struct FogLight
    {
        Mathf::Vector4 position{};
        Mathf::Vector4 direction{};
        Mathf::Vector4 color{};

        float constantAtt{ 0.f };
        float linearAtt{ 0.f };
        float quadAtt{ 0.f };
        float spotAngle{ 0.f };

        int32_t lightType{ 0 };
        int32_t status{ 0 };
        float   range{ 0.f };
        float   intencity{ 0.f };   // 셰이더가 읽지 않는다
    };

    struct FogLightConstants
    {
        Mathf::Vector4 eyePosition{};
        Mathf::Vector4 globalAmbient{};
        FogLight       lights[EnhancedVolumetricFogPass::kMaxLights]{};
    };

    /// 합성 상수(셰이더 b0). DX11 CompositeCB와 같은 배치다.
    struct FogCompositeConstants
    {
        Mathf::Matrix  viewProjection{};
        Mathf::Matrix  inverseView{};
        Mathf::Matrix  inverseProjection{};
        Mathf::Vector4 cameraNearFar{};
        Mathf::Vector4 volumeSize{};
        float          blendingWithSceneColorFactor{ 0.f };
        float          padding[3]{};
    };

    bool CompileFogShader(const std::string& source, const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("볼류메트릭 포그 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += FogHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedVolumetricFogPass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "볼류메트릭 포그 컨텍스트가 불완전하다";
        return false;
    }

    if (!CreatePipelines(context, outError)) return false;
    return CreateVolumes(context, outError);
}

bool EnhancedVolumetricFogPass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // ── 컴퓨트 루트 시그니처(산란·누적 공용) ──
    //
    // 둘이 나눠 가질 수도 있지만 누적이 쓰는 것이 산란의 부분집합이라
    // 하나로 둔다. 누적에서 안 쓰는 슬롯에도 유효한 디스크립터를 채워
    // 넣으면 되고, 그 편이 루트 시그니처를 둘 관리하는 것보다 싸다.
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 4;   // t0 그림자 · t1 블루노이즈 · t2 격자 · t3 구름
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].Descriptor.ShaderRegister = 1;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor.ShaderRegister = 2;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &srvRange;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_STATIC_SAMPLER_DESC samplers[3]{};
        // s0 — 선형 클램프. 시간축 재투영이 격자 밖을 짚을 때 반대편이
        // 딸려 오지 않게 한다.
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;

        // s1 — 선형 랩. 구름이 흘러가야 하므로 랩이다.
        samplers[1] = samplers[0];
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[1].ShaderRegister = 1;

        // s2 — 그림자 비교. 테두리를 흰색(=가려지지 않음)으로 둬야 맵 밖이
        // 그림자로 물들지 않는다(DX11의 BorderColor 1,1,1,1과 같다).
        samplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplers[2].ShaderRegister = 2;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = _countof(params);
        rootDesc.pParameters = params;
        rootDesc.NumStaticSamplers = _countof(samplers);
        rootDesc.pStaticSamplers = samplers;

        const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
        if (!root.IsValid()) return false;
        m_computeRootSignature = root.signature;

        const std::string scatterSource = std::string(kFogCommonHlsl) + kFogScatterHlsl;
        const std::string accumulateSource = kFogAccumulateHlsl;

        ComPtr<ID3DBlob> scatterBlob;
        ComPtr<ID3DBlob> accumulateBlob;
        if (!CompileFogShader(scatterSource, "main", "cs_5_0", scatterBlob, outError)) return false;
        if (!CompileFogShader(accumulateSource, "main", "cs_5_0", accumulateBlob, outError)) return false;

        DX12ComputePipelineDesc scatterDesc{};
        scatterDesc.csBytecode = scatterBlob->GetBufferPointer();
        scatterDesc.csSize = scatterBlob->GetBufferSize();
        scatterDesc.rootSignature = root.signature;
        scatterDesc.rootSignatureId = root.id;
        m_scatterPSO = context.psoManager->GetOrCreateCompute(scatterDesc, outError);
        if (nullptr == m_scatterPSO) return false;

        DX12ComputePipelineDesc accumulateDesc{};
        accumulateDesc.csBytecode = accumulateBlob->GetBufferPointer();
        accumulateDesc.csSize = accumulateBlob->GetBufferSize();
        accumulateDesc.rootSignature = root.signature;
        accumulateDesc.rootSignatureId = root.id;
        m_accumulatePSO = context.psoManager->GetOrCreateCompute(accumulateDesc, outError);
        if (nullptr == m_accumulatePSO) return false;
    }

    // ── 합성 루트 시그니처 ──
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 3;   // t0 씬 색 · t1 깊이 · t2 격자
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // DX11 합성 PSO의 0번 샘플러가 선형 랩이다. 격자 UV가 [0,1]을
        // 벗어날 수 있어 주소 모드가 그림을 가른다 — 그대로 랩을 쓴다.
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = _countof(params);
        rootDesc.pParameters = params;
        rootDesc.NumStaticSamplers = 1;
        rootDesc.pStaticSamplers = &sampler;

        const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
        if (!root.IsValid()) return false;
        m_compositeRootSignature = root.signature;

        const std::string compositeSource = std::string(kFogCommonHlsl) + kFogCompositeHlsl;
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        if (!CompileFogShader(compositeSource, "VSMain", "vs_5_0", vsBlob, outError)) return false;
        if (!CompileFogShader(compositeSource, "PSMain", "ps_5_0", psBlob, outError)) return false;

        DX12GraphicsPipelineDesc desc{};
        desc.vsBytecode = vsBlob->GetBufferPointer();
        desc.vsSize = vsBlob->GetBufferSize();
        desc.psBytecode = psBlob->GetBufferPointer();
        desc.psSize = psBlob->GetBufferSize();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;
        desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        // 숨어 있던 암묵 상태를 명시한다(SSS·Decal·SSR과 같은 부류).
        desc.depthEnable = false;
        desc.blendEnable = false;
        desc.cullMode = D3D12_CULL_MODE_NONE;
        desc.numRenderTargets = 1;
        desc.rtvFormats[0] = kOutputFormat;

        m_compositePSO = context.psoManager->GetOrCreate(desc, outError);
        if (nullptr == m_compositePSO) return false;
    }

    return true;
}

bool EnhancedVolumetricFogPass::CreateVolumes(const EnhancedFrameContext& context,
    std::string& outError)
{
    RHITextureDesc desc{};
    desc.dim = RHITextureDesc::Dim::Texture3D;
    desc.width = kVolumeWidth;
    desc.height = kVolumeHeight;
    desc.depthOrArraySize = kVolumeDepth;
    desc.format = kVoxelFormat;
    desc.allowUnorderedAccess = true;

    const auto makeVolume = [&](ComPtr<ID3D12Resource>& out, const wchar_t* name) -> bool
    {
        desc.debugName = name;
        if (!context.resources->CreateTexture(desc, out, outError))
        {
            outError = "포그 격자 — " + outError;
            return false;
        }
        return true;
    };

    if (!makeVolume(m_voxelTemp[0], L"Fog.VoxelTemp0")) return false;
    if (!makeVolume(m_voxelTemp[1], L"Fog.VoxelTemp1")) return false;
    if (!makeVolume(m_voxelFinal, L"Fog.VoxelFinal")) return false;

    m_voxelTempState[0] = RGResourceState::Common;
    m_voxelTempState[1] = RGResourceState::Common;
    m_voxelFinalState = RGResourceState::Common;
    m_volumesCleared = false;
    m_readIndex = 0;
    return true;
}

bool EnhancedVolumetricFogPass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    if (nullptr != context.camera)
    {
        const Mathf::Matrix viewProjection =
            context.camera->view * context.camera->projection;

        m_inverseViewProjection = XMMatrixTranspose(
            XMMatrixInverse(nullptr, viewProjection));
        m_viewProjection = XMMatrixTranspose(viewProjection);
        m_inverseView = XMMatrixTranspose(context.camera->inverseView);
        m_inverseProjection = XMMatrixTranspose(context.camera->inverseProjection);
        m_cameraPosition = context.camera->eyePosition;
        m_cameraPosition.w = 1.f;

        // 이번 프레임이 쓸 '지난 프레임' 값을 밀봉하고, 다음 프레임을 위해
        // 이번 값을 저장한다.
        m_previousViewProjectionSealed = m_previousViewProjection;
        m_previousViewProjection = m_viewProjection;
    }

    m_lastLightCount = (nullptr != context.lights)
        ? static_cast<uint32_t>((std::min)(context.lights->size(),
            static_cast<size_t>(kMaxLights)))
        : 0u;

    // ★ 첫 프레임에 격자를 0으로 지운다.
    //
    // DX11은 초기값 없이 만들어 내용이 미정의인데, 산란이 지난 프레임을
    // 95%로 받으므로 그 미정의가 첫 몇 프레임의 그림을 정한다. 드라이버가
    // 0을 주는 것이 보통이라 결과는 같고, '보통'에 기대지 않게 된다.
    if (!m_volumesCleared)
    {
        auto* commandList = context.resources->GetCommandList();

        ID3D12Resource* const volumes[3] = {
            m_voxelTemp[0].Get(), m_voxelTemp[1].Get(), m_voxelFinal.Get() };

        D3D12_RESOURCE_BARRIER toUav[3]{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            toUav[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toUav[i].Transition.pResource = volumes[i];
            toUav[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            toUav[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            toUav[i].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        // ★ 이 전이는 인코더로 옮기지 않는다. 인코더에는 UavBarrier만 있고
        //   상태 전이는 그래프의 몫이라는 것이 R3의 계약인데, 여기는 그래프
        //   밖이고 이 리소스들이 그래프에 들어오기 전에 한 번 거치는 자리다.
        //   패스가 전이를 부를 수 있게 인코더를 넓히는 대신 원시로 남긴다 —
        //   드문 초기화 하나 때문에 계약을 무르는 쪽이 비싸다.
        commandList->ResourceBarrier(3, toUav);

        // ★ 여기가 비가시 힙을 들고 있던 이유였다(R3-2).
        //
        //   ClearUnorderedAccessViewFloat가 셰이더 가시 GPU 핸들과 비가시 CPU
        //   핸들을 짝으로 요구해서, 이 패스가 그것 하나 때문에 힙을 따로 만들고
        //   같은 뷰를 두 번 만들고 링 오프셋을 손으로 계산했다. 서비스가 짝을
        //   안에서 맞추므로 여기 남는 것은 "이 셋을 0으로 지운다"뿐이다.
        const float zero[4]{ 0.f, 0.f, 0.f, 0.f };
        for (ID3D12Resource* volume : volumes)
        {
            context.resources->ClearUnorderedAccess(commandList,
                RHIBindingDesc::Uav3D(volume, kVoxelFormat, kVolumeDepth), zero);
        }

        m_voxelTempState[0] = RGResourceState::UnorderedAccess;
        m_voxelTempState[1] = RGResourceState::UnorderedAccess;
        m_voxelFinalState = RGResourceState::UnorderedAccess;
        m_volumesCleared = true;
    }

    return true;
}

void EnhancedVolumetricFogPass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    // 꺼져 있으면 입력을 그대로 흘린다(SSR과 같은 처리).
    if (!m_enabled)
    {
        m_output = m_inputs.color;
        m_finalHandle = RGHandle{};
        return;
    }

    m_output = RGHandle{};
    m_finalHandle = RGHandle{};

    if (nullptr == m_scatterPSO || nullptr == m_accumulatePSO ||
        nullptr == m_compositePSO) return;
    if (0 == m_width || 0 == m_height) return;
    if (!m_inputs.color.IsValid() || !m_inputs.depth.IsValid() ||
        !m_inputs.shadowMap.IsValid() || !m_inputs.cloudShadow.IsValid() ||
        !m_inputs.blueNoise.IsValid())
    {
        m_output = m_inputs.color;
        return;
    }

    const uint32_t readIndex = m_readIndex;
    const uint32_t writeIndex = 1u - m_readIndex;

    // 격자 셋을 그래프에 들인다. 패스가 들고 있는 것이라 상태를 알려 줘야
    // 첫 전이를 맞게 만든다.
    //
    // 최종 상태는 그래프가 적어 준다. 패스가 '내가 마지막으로 쓴 상태'를
    // 적어 두는 것으로는 모자란다 — 뒤에 붙은 소비자가 한 번 더 전이시키면
    // 그 값이 어긋나고, 다음 프레임의 첫 배리어가 틀린 before로 나간다.
    const RGHandle readHandle = graph.ImportTexture(m_voxelTemp[readIndex].Get(),
        m_voxelTempState[readIndex], "Fog.VoxelRead", &m_voxelTempState[readIndex]);
    const RGHandle writeHandle = graph.ImportTexture(m_voxelTemp[writeIndex].Get(),
        m_voxelTempState[writeIndex], "Fog.VoxelWrite", &m_voxelTempState[writeIndex]);
    m_finalHandle = graph.ImportTexture(m_voxelFinal.Get(), m_voxelFinalState,
        "Fog.VoxelFinal", &m_voxelFinalState);

    RGTextureDesc outputDesc{};
    outputDesc.width = m_width;
    outputDesc.height = m_height;
    outputDesc.format = kOutputFormat;
    outputDesc.allowRenderTarget = true;
    outputDesc.name = "Fog.Output";
    m_output = graph.CreateTexture(outputDesc);

    // 산란·누적이 함께 쓰는 상수를 만든다. 두 패스가 같은 값을 읽는다.
    const auto fillFogConstants = [this]() -> FogConstants
    {
        FogConstants constants{};
        constants.inverseViewProjection = m_inverseViewProjection;
        constants.previousViewProjection = m_previousViewProjectionSealed;
        constants.shadowMatrix = XMMatrixTranspose(m_shadowMatrix);
        constants.cameraPosition = m_cameraPosition;
        constants.nearFarFrameBlend = {
            m_tuning.customNearPlane, m_tuning.customFarPlane,
            static_cast<float>(m_frameIndex), m_tuning.previousFrameBlendFactor };
        constants.volumeSize = { static_cast<float>(kVolumeWidth),
            static_cast<float>(kVolumeHeight), static_cast<float>(kVolumeDepth), 0.f };
        constants.anisotropy = m_tuning.anisotropy;
        constants.density = m_tuning.density;
        constants.strength = m_tuning.strength;
        constants.thicknessFactor = m_tuning.thicknessFactor;
        return constants;
    };

    // 컴퓨트 두 패스가 공유하는 바인딩. t0~t3 테이블과 u0 테이블을 만든다.
    const auto bindCompute = [this, &context](
        const EnhancedRenderGraph::ExecuteContext& executeContext,
        RGHandle voxelRead, RGHandle voxelWrite) -> bool
    {

        const RHIBindingDesc srvs[] = {
            // t0 — 캐스케이드 그림자맵(배열). 깊이 리소스라 포맷을 명시한다.
            RHIBindingDesc::SrvArray(executeContext.Resolve(m_inputs.shadowMap),
                DXGI_FORMAT_R32_FLOAT, kShadowCascadeCount),
            // t1 — 블루 노이즈.
            RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.blueNoise)),
            // t2 — 읽을 격자(3D).
            RHIBindingDesc::Srv3D(executeContext.Resolve(voxelRead), kVoxelFormat),
            // t3 — 구름 그림자.
            RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.cloudShadow)),
        };
        // u0 — 쓸 격자(3D).
        const RHIBindingDesc uavs[] = {
            RHIBindingDesc::Uav3D(executeContext.Resolve(voxelWrite),
                kVoxelFormat, kVolumeDepth),
        };
        const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
        const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
        if (!srvTable.IsValid() || !uavTable.IsValid()) return false;

        RHIEncoder& encoder = *executeContext.encoder;
        // PSO는 패스마다 다르므로 여기서는 루트 시그니처만 건다. 뒤에서 거는
        // SetPipeline이 같은 시그니처를 다시 넘기지만 인코더가 중복을 거른다.
        encoder.SetPipeline(RHIBindPoint::Compute, nullptr, m_computeRootSignature);
        encoder.SetBindings(RHIBindPoint::Compute, 3, srvTable);
        encoder.SetBindings(RHIBindPoint::Compute, 4, uavTable);
        return true;
    };

    // ── ① 산란 ──
    graph.AddPass("Fog.Scatter",
        {
            { m_inputs.shadowMap,   RGResourceState::ShaderResource },
            { m_inputs.blueNoise,   RGResourceState::ShaderResource },
            { m_inputs.cloudShadow, RGResourceState::ShaderResource },
            { readHandle,           RGResourceState::ShaderResource },
            { writeHandle,          RGResourceState::UnorderedAccess },
        },
        [this, &context, fillFogConstants, bindCompute, readHandle, writeHandle](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const FogConstants constants = fillFogConstants();
            const auto fogCb = context.resources->GetUploadRing().Allocate(
                sizeof(FogConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!fogCb.IsValid()) return;
            memcpy(fogCb.cpuAddress, &constants, sizeof(constants));

            FogCloudConstants cloud{};
            cloud.viewProjection = XMMatrixTranspose(m_cloud.viewProjection);
            cloud.cloudMapSize[0] = m_cloud.cloudMapSize[0];
            cloud.cloudMapSize[1] = m_cloud.cloudMapSize[1];
            cloud.size[0] = m_cloud.size[0];
            cloud.size[1] = m_cloud.size[1];
            cloud.direction[0] = m_cloud.direction[0];
            cloud.direction[1] = m_cloud.direction[1];
            cloud.frameIndex = m_cloud.frameIndex;
            cloud.moveSpeed = m_cloud.moveSpeed;
            cloud.alpha = m_cloud.alpha;
            cloud.isOn = m_cloud.isOn;

            const auto cloudCb = context.resources->GetUploadRing().Allocate(
                sizeof(FogCloudConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cloudCb.IsValid()) return;
            memcpy(cloudCb.cpuAddress, &cloud, sizeof(cloud));

            // 광원 목록을 셰이더 배치로 옮긴다. status가 0이면 셰이더가
            // 건너뛰므로, 채우지 않은 자리는 자동으로 꺼진 광원이 된다.
            FogLightConstants lights{};
            lights.eyePosition = m_cameraPosition;
            if (nullptr != context.lights)
            {
                const uint32_t count = (std::min)(
                    static_cast<uint32_t>(context.lights->size()), kMaxLights);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto& source = (*context.lights)[i];
                    FogLight& target = lights.lights[i];
                    target.position = source.position;
                    target.direction = source.direction;
                    target.color = source.color;
                    target.constantAtt = source.attenuation.x;
                    target.linearAtt = source.attenuation.y;
                    target.quadAtt = source.attenuation.z;
                    target.spotAngle = source.direction.w;
                    target.lightType = static_cast<int32_t>(source.position.w);
                    target.status = 1;
                    target.range = source.attenuation.w;
                    target.intencity = source.color.w;
                }
            }

            const auto lightCb = context.resources->GetUploadRing().Allocate(
                sizeof(FogLightConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!lightCb.IsValid()) return;
            memcpy(lightCb.cpuAddress, &lights, sizeof(lights));

            if (!bindCompute(executeContext, readHandle, writeHandle)) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_scatterPSO, m_computeRootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, fogCb.gpuAddress);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 1, cloudCb.gpuAddress);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 2, lightCb.gpuAddress);

            // z는 그룹당 스레드가 하나라 깊이만큼 그룹을 띄운다.
            encoder.Dispatch((kVolumeWidth + 7) / 8, (kVolumeHeight + 7) / 8,
                kVolumeDepth);
        });

    // ── ② 누적 ──
    //
    // 방금 쓴 격자를 읽어 z를 훑는다. DX11이 여기서 핑퐁을 뒤집고 읽는
    // 것과 같다 — 결국 '산란이 쓴 것'을 읽는다.
    graph.AddPass("Fog.Accumulate",
        {
            { m_inputs.shadowMap,   RGResourceState::ShaderResource },
            { m_inputs.blueNoise,   RGResourceState::ShaderResource },
            { m_inputs.cloudShadow, RGResourceState::ShaderResource },
            { writeHandle,          RGResourceState::ShaderResource },
            { m_finalHandle,        RGResourceState::UnorderedAccess },
        },
        [this, &context, fillFogConstants, bindCompute, writeHandle](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const FogConstants constants = fillFogConstants();
            const auto fogCb = context.resources->GetUploadRing().Allocate(
                sizeof(FogConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!fogCb.IsValid()) return;
            memcpy(fogCb.cpuAddress, &constants, sizeof(constants));

            if (!bindCompute(executeContext, writeHandle, m_finalHandle)) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_accumulatePSO, m_computeRootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, fogCb.gpuAddress);

            // 스레드마다 z를 통째로 훑으므로 z는 1이다.
            encoder.Dispatch((kVolumeWidth + 7) / 8, (kVolumeHeight + 7) / 8, 1);
        });

    // ── ③ 합성 ──
    graph.AddPass("Fog.Composite",
        {
            { m_inputs.color, RGResourceState::ShaderResource },
            { m_inputs.depth, RGResourceState::ShaderResource },
            { m_finalHandle,  RGResourceState::ShaderResource },
            { m_output,       RGResourceState::RenderTarget },
        },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.color)),
                RHIBindingDesc::SrvDepth(executeContext.Resolve(m_inputs.depth)),
                RHIBindingDesc::Srv3D(executeContext.Resolve(m_finalHandle), kVoxelFormat),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            if (!srvTable.IsValid()) return;

            FogCompositeConstants constants{};
            constants.viewProjection = m_viewProjection;
            constants.inverseView = m_inverseView;
            constants.inverseProjection = m_inverseProjection;
            constants.cameraNearFar = {
                m_tuning.customNearPlane, m_tuning.customFarPlane, 0.f, 0.f };
            constants.volumeSize = { static_cast<float>(kVolumeWidth),
                static_cast<float>(kVolumeHeight), static_cast<float>(kVolumeDepth), 0.f };
            constants.blendingWithSceneColorFactor = m_tuning.blendingWithSceneColorFactor;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(FogCompositeConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            encoder.SetPipeline(RHIBindPoint::Graphics, m_compositePSO, m_compositeRootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Graphics, 1, srvTable);

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
        },
        m_keepAlive);

    // 다음 프레임은 이번에 쓴 것을 읽는다 — 그것이 시간축 히스토리다.
    m_readIndex = writeIndex;
}

void EnhancedVolumetricFogPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_voxelTemp[0].Reset();
    m_voxelTemp[1].Reset();
    m_voxelFinal.Reset();
    m_volumesCleared = false;
    m_scatterPSO = nullptr;
    m_accumulatePSO = nullptr;
    m_compositePSO = nullptr;
    m_computeRootSignature = nullptr;
    m_compositeRootSignature = nullptr;
}

#endif
