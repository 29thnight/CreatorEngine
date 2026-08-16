#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedIBLGenerator.h"
#include "../RHIEncoder.h"

#include <cstring>
#include <string>
#include "../RHIShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
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
        RHIShaderBlob& outBlob, std::string& outError, bool strictMath = false)
    {
        RHIShaderCompileOptions options{};
        options.strictMath = strictMath;
        return RHIShaderCompiler::CompileFile(file, entry, target, outBlob, outError,
            options);
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

    m_resources = context.resources;

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
        !CompileIblShader(kIblBrdfPSFile, "PSMain", "ps_5_0", brdfPs, outError, true))
    {
        return false;
    }

    const auto makePso = [&](const RHIShaderBlob& vs, const RHIShaderBlob& ps) -> RHIPipelineHandle
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vsBytecode = vs.Data();
        desc.vsSize = vs.Size();
        desc.psBytecode = ps.Data();
        desc.psSize = ps.Size();
        desc.layout = root;
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.topologyType = RHITopologyType::Triangle;
        desc.depthEnable = false;
        desc.blendEnable = false;
        desc.cullMode = RHICullMode::None;
        desc.numRenderTargets = 1;
        desc.rtvFormats[0] = kFormat;

        return context.psoManager->GetOrCreate(desc, outError);
    };

    m_rectToCubePso = makePso(faceVs, rectPs);
    if (!m_rectToCubePso.IsValid()) return false;
    m_irradiancePso = makePso(faceVs, irradiancePs);
    if (!m_irradiancePso.IsValid()) return false;
    m_prefilterPso = makePso(faceVs, prefilterPs);
    if (!m_prefilterPso.IsValid()) return false;
    m_brdfPso = makePso(fullscreenVs, brdfPs);
    if (!m_brdfPso.IsValid()) return false;

    return true;
}

bool EnhancedIBLGenerator::CreateTargets(uint32_t cubeSize, uint32_t brdfSize,
    std::string& outError)
{
    if (nullptr == m_resources)
    {
        outError = "IBL 생성기가 초기화되지 않았다";
        return false;
    }

    m_resources->ReleaseTexture(m_cubeMapHandle);
    m_resources->ReleaseTexture(m_irradianceHandle);
    m_resources->ReleaseTexture(m_prefilteredHandle);
    m_resources->ReleaseTexture(m_brdfLutHandle);
    m_cubeMapHandle = {};
    m_irradianceHandle = {};
    m_prefilteredHandle = {};
    m_brdfLutHandle = {};

    const auto makeTarget = [&](uint32_t size, uint32_t arraySize, uint32_t mips,
        const wchar_t* name, RHITextureHandle& out) -> bool
    {
        RHITextureDesc desc{};
        desc.width = size;
        desc.height = size;
        desc.depthOrArraySize = arraySize;
        desc.mipLevels = mips;
        desc.format = kFormat;
        desc.allowRenderTarget = true;
        desc.initialState = RHIResourceState::RenderTarget;
        desc.debugName = name;
        return m_resources->CreateTexture(desc, out, outError);
    };

    if (!makeTarget(cubeSize, 6, 1, L"IBL.CubeMap", m_cubeMapHandle) ||
        !makeTarget(cubeSize, 6, 1, L"IBL.Irradiance", m_irradianceHandle) ||
        !makeTarget(cubeSize, 6, kPrefilterMips, L"IBL.Prefiltered", m_prefilteredHandle) ||
        !makeTarget(brdfSize, 1, 1, L"IBL.BrdfLut", m_brdfLutHandle))
    {
        m_resources->ReleaseTexture(m_cubeMapHandle);
        m_resources->ReleaseTexture(m_irradianceHandle);
        m_resources->ReleaseTexture(m_prefilteredHandle);
        m_resources->ReleaseTexture(m_brdfLutHandle);
        m_cubeMapHandle = {};
        m_irradianceHandle = {};
        m_prefilteredHandle = {};
        m_brdfLutHandle = {};
        return false;
    }

    return true;
}

bool EnhancedIBLGenerator::Generate(const EnhancedFrameContext& context,
    RHITextureHandle equirect, RHIFormat equirectFormat,
    uint32_t cubeSize, uint32_t brdfSize, std::string& outError)
{
    if (!equirect.IsValid() || RHIFormat::Unknown == equirectFormat ||
        0 == cubeSize || 0 == brdfSize)
    {
        outError = "IBL 입력이 불완전하다";
        return false;
    }

    if (nullptr == m_resources || context.resources != m_resources)
    {
        outError = "IBL 생성기가 초기화되지 않았거나 다른 디바이스 컨텍스트다";
        return false;
    }

    m_cubeSize = cubeSize;
    m_brdfSize = brdfSize;

    if (!CreateTargets(cubeSize, brdfSize, outError)) return false;

    RHIEncoder& encoder = m_resources->GetImmediateEncoder();
    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

    const auto drawFaces = [&](RHIPipelineHandle pso, RHITextureHandle target,
        uint32_t mip, uint32_t size, const RHIBindingTable& source,
        float roughness) -> bool
    {
        encoder.SetViewportAndScissor(size, size);
        encoder.SetPipeline(RHIBindPoint::Graphics, pso);

        for (uint32_t face = 0; face < 6; ++face)
        {
            const RHIColorTargetDesc color = RHIColorTargetDesc::Slice(
                target, kFormat, mip, face);
            const RHIRenderTargetBinding targets = m_resources->CreateRenderTargets(
                std::span<const RHIColorTargetDesc>{ &color, 1 });
            if (!targets.IsValid()) return false;

            IblDrawConstants constants{};
            memcpy(constants.forward, kIblFaces[face].forward, sizeof(float) * 3);
            memcpy(constants.right, kIblFaces[face].right, sizeof(float) * 3);
            memcpy(constants.up, kIblFaces[face].up, sizeof(float) * 3);
            constants.params[0] = roughness;

            const RHIBufferSlice cb = m_resources->UploadConstants(
                &constants, sizeof(IblDrawConstants));
            if (!cb.IsValid()) return false;

            encoder.BindRenderTargets(targets);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
            encoder.SetBindings(RHIBindPoint::Graphics, 1, source);
            encoder.Draw(3, 1);
        }
        return true;
    };

    const auto transition = [&](RHITextureHandle texture,
        RHIResourceState before, RHIResourceState after)
    {
        const RHITransition one[] = { { texture, before, after } };
        m_resources->TransitionResources(one);
    };

    // ── ① rect → cube ──
    const RHIBindingDesc equirectView = RHIBindingDesc::Srv2D(
        equirect, equirectFormat, 0, 1);
    const RHIBindingTable equirectTable = m_resources->CreateBindings(
        std::span<const RHIBindingDesc>{ &equirectView, 1 });
    if (!equirectTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    if (!drawFaces(m_rectToCubePso, m_cubeMapHandle, 0,
        cubeSize, equirectTable, 0.f))
    {
        outError = "IBL rect→cube 기록 실패(타깃/업로드)";
        return false;
    }

    transition(m_cubeMapHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    const RHIBindingDesc cubeView = RHIBindingDesc::SrvCube(
        m_cubeMapHandle, kFormat, 1);
    const RHIBindingTable cubeTable = m_resources->CreateBindings(
        std::span<const RHIBindingDesc>{ &cubeView, 1 });
    if (!cubeTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }

    // ── ② 조도 맵 ──
    if (!drawFaces(m_irradiancePso, m_irradianceHandle, 0,
        cubeSize, cubeTable, 0.f))
    {
        outError = "IBL 조도 기록 실패(타깃/업로드)";
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
            if (!drawFaces(m_prefilterPso, m_prefilteredHandle, mip,
                mipSize, cubeTable, roughness))
            {
                outError = "IBL 프리필터 기록 실패(타깃/업로드)";
                return false;
            }
            mipSize = (mipSize > 1) ? mipSize / 2 : 1;
        }
    }
    transition(m_prefilteredHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    // ── ④ BRDF LUT ──
    {
        // b0·t0을 형식상 채운다 — BRDF 셰이더는 읽지 않지만, 테이블 파라미터가
        // 선언된 루트를 쓰는 이상 유효한 핸들을 두는 쪽이 안전하다.
        IblDrawConstants constants{};
        const RHIBufferSlice cb = m_resources->UploadConstants(
            &constants, sizeof(IblDrawConstants));
        if (!cb.IsValid()) { outError = "IBL 업로드 링 부족(LUT)"; return false; }

        const RHIColorTargetDesc color = RHIColorTargetDesc::Texture(m_brdfLutHandle);
        const RHIRenderTargetBinding targets = m_resources->CreateRenderTargets(
            std::span<const RHIColorTargetDesc>{ &color, 1 });
        if (!targets.IsValid()) { outError = "IBL LUT 타깃 생성 실패"; return false; }

        encoder.SetViewportAndScissor(brdfSize, brdfSize);
        encoder.BindRenderTargets(targets);
        encoder.SetPipeline(RHIBindPoint::Graphics, m_brdfPso);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
        encoder.SetBindings(RHIBindPoint::Graphics, 1, cubeTable);
        encoder.Draw(3, 1);
    }
    transition(m_brdfLutHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    return true;
}

void EnhancedIBLGenerator::Shutdown()
{
    if (nullptr != m_resources)
    {
        m_resources->ReleaseTexture(m_cubeMapHandle);
        m_resources->ReleaseTexture(m_irradianceHandle);
        m_resources->ReleaseTexture(m_prefilteredHandle);
        m_resources->ReleaseTexture(m_brdfLutHandle);
        m_resources = nullptr;
    }
    m_cubeMapHandle = {};
    m_irradianceHandle = {};
    m_prefilteredHandle = {};
    m_brdfLutHandle = {};

    m_rectToCubePso = {};
    m_irradiancePso = {};
    m_prefilterPso = {};
    m_brdfPso = {};
    m_cubeSize = 0;
    m_brdfSize = 0;
}

#endif
