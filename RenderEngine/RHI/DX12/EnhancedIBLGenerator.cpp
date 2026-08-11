#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedIBLGenerator.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"

#include <cstring>
#include <sstream>
#include <string>
#include "DX12ShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string IblHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // D3D 큐브 면 기저(+X -X +Y -Y +Z -Z). DX11의 면별 카메라(forward/up)에서
    // right = cross(up, forward)로 유도한 것과 같고, D3D 텍셀 규약과도 같다 —
    // 하드웨어 샘플이 (방향 → 텍셀)을 정하므로 여기가 어긋나면 하늘이 뒤집힌다.
    struct IblFaceBasis
    {
        float forward[3];
        float right[3];
        float up[3];
    };
    constexpr IblFaceBasis kIblFaces[6] = {
        { {  1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1, 0, 0 }, { 0, 0, 1 },  { 0, 1, 0 } },
        { { 0, 1, 0 },  { 1, 0, 0 },  { 0, 0, -1 } },
        { { 0, -1, 0 }, { 1, 0, 0 },  { 0, 0, 1 } },
        { { 0, 0, 1 },  { 1, 0, 0 },  { 0, 1, 0 } },
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    };

    // VS·PS가 공유하는 드로우 상수. HLSL cbuffer와 배치가 같아야 한다.
    struct IblDrawConstants
    {
        float forward[4];
        float right[4];
        float up[4];
        float params[4];   // x = roughness
    };

    // ── 면 방향 VS — 풀스크린 삼각형의 uv에서 면 방향을 만든다 ──
    constexpr const char* kIblFaceVSFile = "IblFace.hlsl";

    // ── BRDF LUT용 풀스크린 VS — uv가 곧 (NdotV, roughness)다 ──
    constexpr const char* kIblFullscreenVSFile = "IblFullscreen.hlsl";

    // ── rect→cube (DX11 RectToCubeMap.ps의 이식) ──
    constexpr const char* kIblRectToCubePSFile = "IblRectToCube.hlsl";

    // ── 조도 맵 (DX11 IrradianceMap.ps의 이식) ──
    //
    // ★ 원본의 수식·quirk를 그대로 둔다(이식 검수에서 발견·기록):
    //   · color * NoL — 코사인 샘플링의 pdf(cosθ/π)에 이미 cosθ가 있어
    //     수학적으로는 이중 가중(cos² 편향, 전체적으로 어둡다)이다.
    //   · SampleLevel(…, 7) — 소스 큐브맵이 밉 1장이라 밉0으로 클램프되어
    //     의도한 사전 블러가 무동작이다.
    //   · 휘도 상한 초과 샘플의 전량 폐기, 로그 공간 평균 — 톤 정책.
    // 그림의 기준선이 이 결과물이라 여기서 고치면 대조가 성립하지 않는다.
    constexpr const char* kIblIrradiancePSFile = "IblIrradiance.hlsl";

    // ── 프리필터 스페큘러 (DX11 SpecularPreFilter.ps의 이식) ──
    //
    // ★ Sample(자동 LOD)을 그대로 둔다 — 발산하는 중요도 샘플 방향에 화면
    //   미분 기반 LOD는 관행(SampleLevel 0)에서 벗어나지만 원본이 그렇다.
    //   휘도 폐기·로그 공간도 조도 맵과 같은 정책.
    constexpr const char* kIblPrefilterPSFile = "IblPrefilter.hlsl";

    // ── BRDF LUT (DX11 IntegrateBRDF.ps의 이식) ──
    constexpr const char* kIblBrdfPSFile = "IblBrdf.hlsl";

    bool CompileIblShader(const char* file, const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(file, entry, target, outBlob, outError);
    }
}

bool EnhancedIBLGenerator::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "IBL 생성기 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedIBLGenerator::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 드로우 상수 · t0 소스 텍스처(테이블) · s0 선형 샘플러.
    // Equirect는 경도 U만 순환하고 위도 V는 극에서 멈춰야 한다. V까지
    // WRAP하면 +Y/-Y 극점에서 반대편 행이 선형 필터에 섞인다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(1, 0),
    };

    // U만 WRAP이다 — 등장방형 소스의 경도는 이어지고 위도는 안 이어진다.
    RHISamplerDesc sourceSampler = RHISampler::Linear(RHIAddressMode::Clamp);
    sourceSampler.addressU = RHIAddressMode::Wrap;

    const RHIStaticSamplerDesc samplers[] = {
        { sourceSampler, 0, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    RHIShaderBlob faceVs;
    RHIShaderBlob fullscreenVs;
    RHIShaderBlob rectPs;
    RHIShaderBlob irradiancePs;
    RHIShaderBlob prefilterPs;
    RHIShaderBlob brdfPs;
    if (!CompileIblShader(kIblFaceVSFile, "VSMain", "vs_5_0", faceVs, outError) ||
        !CompileIblShader(kIblFullscreenVSFile, "VSMain", "vs_5_0", fullscreenVs, outError) ||
        !CompileIblShader(kIblRectToCubePSFile, "PSMain", "ps_5_0", rectPs, outError) ||
        !CompileIblShader(kIblIrradiancePSFile, "PSMain", "ps_5_0", irradiancePs, outError) ||
        !CompileIblShader(kIblPrefilterPSFile, "PSMain", "ps_5_0", prefilterPs, outError) ||
        !CompileIblShader(kIblBrdfPSFile, "PSMain", "ps_5_0", brdfPs, outError))
    {
        return false;
    }

    const auto makePso = [&](const RHIShaderBlob& vs, const RHIShaderBlob& ps) -> ID3D12PipelineState*
    {
        DX12GraphicsPipelineDesc desc{};
        desc.vsBytecode = vs.Data();
        desc.vsSize = vs.Size();
        desc.psBytecode = ps.Data();
        desc.psSize = ps.Size();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.depthEnable = false;
        desc.blendEnable = false;
        desc.cullMode = D3D12_CULL_MODE_NONE;
        desc.numRenderTargets = 1;
        desc.rtvFormats[0] = ToDXGI(kFormat);

        return context.psoManager->GetOrCreate(desc, outError);
    };

    m_rectToCubePso = makePso(faceVs, rectPs);
    if (nullptr == m_rectToCubePso) return false;
    m_irradiancePso = makePso(faceVs, irradiancePs);
    if (nullptr == m_irradiancePso) return false;
    m_prefilterPso = makePso(faceVs, prefilterPs);
    if (nullptr == m_prefilterPso) return false;
    m_brdfPso = makePso(fullscreenVs, brdfPs);
    if (nullptr == m_brdfPso) return false;

    return true;
}

bool EnhancedIBLGenerator::CreateTargets(ID3D12Device* device, uint32_t cubeSize,
    uint32_t brdfSize, std::string& outError)
{
    const auto makeCube = [&](uint32_t size, uint32_t mips,
        ComPtr<ID3D12Resource>& out, const wchar_t* name) -> bool
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = size;
        desc.Height = size;
        desc.DepthOrArraySize = 6;
        desc.MipLevels = static_cast<UINT16>(mips);
        desc.Format = ToDXGI(kFormat);
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&out));
        if (FAILED(hr))
        {
            outError = "IBL 타깃 생성 실패: " + IblHrToString(hr);
            return false;
        }
        out->SetName(name);
        return true;
    };

    if (!makeCube(cubeSize, 1, m_cubeMap, L"IBL.CubeMap")) return false;
    if (!makeCube(cubeSize, 1, m_irradianceMap, L"IBL.Irradiance")) return false;
    if (!makeCube(cubeSize, kPrefilterMips, m_prefilteredMap, L"IBL.Prefiltered")) return false;

    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = brdfSize;
        desc.Height = brdfSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = ToDXGI(kFormat);
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&m_brdfLut));
        if (FAILED(hr))
        {
            outError = "BRDF LUT 생성 실패: " + IblHrToString(hr);
            return false;
        }
        m_brdfLut->SetName(L"IBL.BrdfLut");
    }

    // RTV: 큐브 6 + 조도 6 + 프리필터 36 + LUT 1.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 6 + 6 + 6 * kPrefilterMips + 1;
    const HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        outError = "IBL RTV 힙 생성 실패: " + IblHrToString(hr);
        return false;
    }
    m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    return true;
}

bool EnhancedIBLGenerator::Generate(const EnhancedFrameContext& context,
    ID3D12Resource* equirect, DXGI_FORMAT equirectFormat,
    uint32_t cubeSize, uint32_t brdfSize, std::string& outError)
{
    if (nullptr == equirect || 0 == cubeSize || 0 == brdfSize)
    {
        outError = "IBL 입력이 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();
    auto* commandList = context.resources->GetCommandList();

    m_cubeSize = cubeSize;
    m_brdfSize = brdfSize;

    if (!CreateTargets(device, cubeSize, brdfSize, outError)) return false;

    // 표에 빌려준다 — 소비처(SrvCube/Srv2D)가 핸들을 받으므로(V2-b).
    // 재생성이면 지난 핸들을 먼저 놓는다.
    {
        auto& services = *context.resources;
        m_services = &services;
        services.ReleaseTexture(m_cubeMapHandle);
        services.ReleaseTexture(m_irradianceHandle);
        services.ReleaseTexture(m_prefilteredHandle);
        services.ReleaseTexture(m_brdfLutHandle);
        m_cubeMapHandle = services.RegisterExternalTexture(m_cubeMap.Get());
        m_irradianceHandle = services.RegisterExternalTexture(m_irradianceMap.Get());
        m_prefilteredHandle = services.RegisterExternalTexture(m_prefilteredMap.Get());
        m_brdfLutHandle = services.RegisterExternalTexture(m_brdfLut.Get());
    }

    // RTV들을 미리 깔아 둔다. 배치: [0..5] 큐브, [6..11] 조도,
    // [12..47] 프리필터(밉 m 면 f → 12 + m*6 + f), [48] LUT.
    const auto rtvAt = [&](uint32_t index)
    {
        auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * m_rtvIncrement;
        return handle;
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = ToDXGI(kFormat);
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.FirstArraySlice = face;
        rtv.Texture2DArray.ArraySize = 1;
        device->CreateRenderTargetView(m_cubeMap.Get(), &rtv, rtvAt(face));
        device->CreateRenderTargetView(m_irradianceMap.Get(), &rtv, rtvAt(6 + face));

        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            rtv.Texture2DArray.MipSlice = mip;
            device->CreateRenderTargetView(m_prefilteredMap.Get(), &rtv,
                rtvAt(12 + mip * 6 + face));
        }
        rtv.Texture2DArray.MipSlice = 0;
    }
    device->CreateRenderTargetView(m_brdfLut.Get(), nullptr, rtvAt(48));

    // 면 6장을 그린다. 소스 SRV는 스테이지마다 하나라 테이블도 하나면 된다.
    const auto drawFaces = [&](ID3D12PipelineState* pso, uint32_t rtvBase,
        uint32_t size, D3D12_GPU_DESCRIPTOR_HANDLE source, float roughness)
    {
        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(size), static_cast<float>(size), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(size), static_cast<LONG>(size) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->SetPipelineState(pso);

        for (uint32_t face = 0; face < 6; ++face)
        {
            IblDrawConstants constants{};
            memcpy(constants.forward, kIblFaces[face].forward, sizeof(float) * 3);
            memcpy(constants.right, kIblFaces[face].right, sizeof(float) * 3);
            memcpy(constants.up, kIblFaces[face].up, sizeof(float) * 3);
            constants.params[0] = roughness;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(IblDrawConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return false;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            const auto rtvHandle = rtvAt(rtvBase + face);
            commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);
            commandList->SetGraphicsRootDescriptorTable(1, source);
            commandList->DrawInstanced(3, 1, 0, 0);
        }
        return true;
    };

    const auto transition = [&](RHITextureHandle texture,
        RHIResourceState before, RHIResourceState after)
    {
        const RHITransition one[] = { { texture, before, after } };
        context.resources->TransitionResources(one);
    };

    ID3D12DescriptorHeap* heaps[] = { context.resources->GetDescriptorRing().GetHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(m_rootSignature);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ── ① rect → cube ──
    const auto equirectTable = context.resources->GetDescriptorRing().Allocate(1);
    if (!equirectTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Format = equirectFormat;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(equirect, &srv, equirectTable.CpuAt(0));
    }
    if (!drawFaces(m_rectToCubePso, 0, cubeSize, equirectTable.gpu, 0.f))
    {
        outError = "IBL 업로드 링 부족(rect→cube)";
        return false;
    }

    transition(m_cubeMapHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    const auto cubeTable = context.resources->GetDescriptorRing().Allocate(1);
    if (!cubeTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.Format = ToDXGI(kFormat);
        srv.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(m_cubeMap.Get(), &srv, cubeTable.CpuAt(0));
    }

    // ── ② 조도 맵 ──
    if (!drawFaces(m_irradiancePso, 6, cubeSize, cubeTable.gpu, 0.f))
    {
        outError = "IBL 업로드 링 부족(조도)";
        return false;
    }
    transition(m_irradianceHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    // ── ③ 프리필터 스페큘러 — 밉 m의 거칠기 = m/(밉수-1), 크기는 절반씩 ──
    {
        uint32_t mipSize = cubeSize;
        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            const float roughness =
                static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);
            if (!drawFaces(m_prefilterPso, 12 + mip * 6, mipSize, cubeTable.gpu, roughness))
            {
                outError = "IBL 업로드 링 부족(프리필터)";
                return false;
            }
            mipSize = (mipSize > 1) ? mipSize / 2 : 1;
        }
    }
    transition(m_prefilteredHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    // ── ④ BRDF LUT ──
    {
        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(brdfSize), static_cast<float>(brdfSize), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(brdfSize), static_cast<LONG>(brdfSize) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->SetPipelineState(m_brdfPso);

        // b0·t0을 형식상 채운다 — BRDF 셰이더는 읽지 않지만, 테이블 파라미터가
        // 선언된 루트를 쓰는 이상 유효한 핸들을 두는 쪽이 안전하다.
        IblDrawConstants constants{};
        const auto cb = context.resources->GetUploadRing().Allocate(
            sizeof(IblDrawConstants), DX12UploadRing::kConstantBufferAlignment);
        if (!cb.IsValid()) { outError = "IBL 업로드 링 부족(LUT)"; return false; }
        memcpy(cb.cpuAddress, &constants, sizeof(constants));

        const auto rtvHandle = rtvAt(48);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);
        commandList->SetGraphicsRootDescriptorTable(1, cubeTable.gpu);
        commandList->DrawInstanced(3, 1, 0, 0);
    }
    transition(m_brdfLutHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    return true;
}

void EnhancedIBLGenerator::Shutdown()
{
    // 표에서 먼저 놓는다 — ComPtr을 놓기 전이어야 표에 죽은 포인터가 남지 않는다.
    if (nullptr != m_services)
    {
        m_services->ReleaseTexture(m_cubeMapHandle);
        m_services->ReleaseTexture(m_irradianceHandle);
        m_services->ReleaseTexture(m_prefilteredHandle);
        m_services->ReleaseTexture(m_brdfLutHandle);
        m_services = nullptr;
    }
    m_cubeMapHandle = {};
    m_irradianceHandle = {};
    m_prefilteredHandle = {};
    m_brdfLutHandle = {};

    m_cubeMap.Reset();
    m_irradianceMap.Reset();
    m_prefilteredMap.Reset();
    m_brdfLut.Reset();
    m_rtvHeap.Reset();
    m_rectToCubePso = nullptr;
    m_irradiancePso = nullptr;
    m_prefilterPso = nullptr;
    m_brdfPso = nullptr;
    m_rootSignature = nullptr;
    m_cubeSize = 0;
    m_brdfSize = 0;
}

#endif
