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
        // x = roughness · y = 읽을 소스 밉 · z = 환경 큐브 한 변 ·
        // w = 환경 큐브 밉 수. 밉 다운샘플만 z를 "그리는 밉의 한 변"으로 쓴다.
        float params[4];
    };

    // ── 면 방향 VS — 풀스크린 삼각형의 uv에서 면 방향을 만든다 ──
    constexpr const char* kIblFaceVSFile = "IblFace.slang";

    // ── BRDF LUT용 풀스크린 VS — uv가 곧 (NdotV, roughness)다 ──
    constexpr const char* kIblFullscreenVSFile = "IblFullscreen.slang";

    // ── rect→cube (DX11 RectToCubeMap.ps의 이식) ──
    constexpr const char* kIblRectToCubePSFile = "IblRectToCube.slang";

    // ── 환경 큐브 밉 다운샘플 ──
    //
    // 중요도 격자가 사용할 환경 밉을 만든다. 원본 밉 0은 거울 반사에 남긴다.
    constexpr const char* kIblCubeDownsamplePSFile = "IblCubeDownsample.slang";

    // ── 조도 맵 (코사인 가중 반구 적분) ──
    //
    // 굽는 값은 E가 아니라 E/PI다(Includes/Ibl.slang의 조도 규약).
    constexpr const char* kIblIrradiancePSFile = "IblIrradiance.slang";

    // ── 프리필터 스페큘러 (Karis 2013 split-sum의 첫째 합) ──
    constexpr const char* kIblPrefilterPSFile = "IblPrefilter.slang";

    // ── BRDF LUT (DX11 IntegrateBRDF.ps의 이식) ──
    constexpr const char* kIblBrdfPSFile = "IblBrdf.slang";

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
        RHILayout::SrvTable(4, 0),
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
    RHIShaderBlob downsamplePs;
    RHIShaderBlob irradiancePs;
    RHIShaderBlob prefilterPs;
    RHIShaderBlob brdfPs;
    RHIShaderBlob rowsPs, marginalPs, samplesPs;
    if (!CompileIblShader(kIblFaceVSFile, "VSMain", "vs_5_0", faceVs, outError) ||
        !CompileIblShader(kIblFullscreenVSFile, "VSMain", "vs_5_0", fullscreenVs, outError) ||
        !CompileIblShader(kIblRectToCubePSFile, "PSMain", "ps_5_0", rectPs, outError) ||
        !CompileIblShader(kIblCubeDownsamplePSFile, "PSMain", "ps_5_0", downsamplePs, outError) ||
        !CompileIblShader(kIblIrradiancePSFile, "PSMain", "ps_5_0", irradiancePs, outError) ||
        !CompileIblShader(kIblPrefilterPSFile, "PSMain", "ps_5_0", prefilterPs, outError) ||
        !CompileIblShader("IblImportanceRows.slang", "PSMain", "ps_5_0", rowsPs, outError) ||
        !CompileIblShader("IblImportanceMarginal.slang", "PSMain", "ps_5_0", marginalPs, outError) ||
        !CompileIblShader("IblImportanceSamples.slang", "PSMain", "ps_5_0", samplesPs, outError) ||
        !CompileIblShader(kIblBrdfPSFile, "PSMain", "ps_5_0", brdfPs, outError, true))
    {
        return false;
    }

    const auto makePso = [&](const RHIShaderBlob& vs, const RHIShaderBlob& ps,
        RHIFormat format = kFormat) -> RHIPipelineHandle
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
        desc.rtvFormats[0] = format;

        return context.psoManager->GetOrCreate(desc, outError);
    };

    m_importanceRowsPso = makePso(fullscreenVs, rowsPs, kImportanceFormat);
    if (!m_importanceRowsPso.IsValid()) return false;
    m_importanceMarginalPso = makePso(fullscreenVs, marginalPs, kImportanceFormat);
    if (!m_importanceMarginalPso.IsValid()) return false;
    m_importanceSamplesPso = makePso(fullscreenVs, samplesPs, kImportanceFormat);
    if (!m_importanceSamplesPso.IsValid()) return false;
    m_rectToCubePso = makePso(faceVs, rectPs);
    if (!m_rectToCubePso.IsValid()) return false;
    m_cubeDownsamplePso = makePso(faceVs, downsamplePs);
    if (!m_cubeDownsamplePso.IsValid()) return false;
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

    ReleaseTargets();

    const auto makeTarget = [&](uint32_t size, uint32_t arraySize, uint32_t mips,
        const wchar_t* name, RHITextureHandle& out, uint32_t height = 0,
        RHIFormat format = kFormat) -> bool
    {
        RHITextureDesc desc{};
        desc.width = size;
        desc.height = height ? height : size;
        desc.depthOrArraySize = arraySize;
        desc.mipLevels = mips;
        desc.format = format;
        desc.allowRenderTarget = true;
        desc.initialState = RHIResourceState::RenderTarget;
        desc.debugName = name;
        return m_resources->CreateTexture(desc, out, outError);
    };

    // 환경 밉 체인과 convolution 산출물, 중요도 분포용 작업 텍스처.
    if (!makeTarget(cubeSize, 6, CubeMipCount(cubeSize), L"IBL.CubeMap", m_cubeMapHandle) ||
        !makeTarget(cubeSize, 6, 1, L"IBL.CubeSource", m_cubeSourceHandle) ||
        !makeTarget(GetIrradianceSize(), 6, 1, L"IBL.Irradiance", m_irradianceHandle) ||
        !makeTarget(cubeSize, 6, kPrefilterMips, L"IBL.Prefiltered", m_prefilteredHandle) ||
        !makeTarget(brdfSize, 1, 1, L"IBL.BrdfLut", m_brdfLutHandle) ||
        !makeTarget(m_importanceSize, 1, 1, L"IBL.ImportanceRows", m_importanceRows,
            6 * m_importanceSize, kImportanceFormat) ||
        !makeTarget(1, 1, 1, L"IBL.ImportanceMarginal", m_importanceMarginal,
            6 * m_importanceSize, kImportanceFormat) ||
        !makeTarget(kImportanceSampleCount, 1, 1, L"IBL.ImportanceSamples", m_importanceSamples,
            2, kImportanceFormat))
    {
        ReleaseTargets();
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
    m_importanceSize = cubeSize;
    m_importanceMip = 0;
    while (m_importanceSize > kImportanceMaxSize && m_importanceMip + 1 < CubeMipCount(cubeSize))
    {
        m_importanceSize >>= 1;
        ++m_importanceMip;
    }

    if (!CreateTargets(cubeSize, brdfSize, outError)) return false;

    RHIEncoder& encoder = m_resources->GetImmediateEncoder();
    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

    const uint32_t cubeMips = CubeMipCount(cubeSize);

    const auto drawFaces = [&](RHIPipelineHandle pso, RHITextureHandle target,
        uint32_t mip, uint32_t size, const RHIBindingTable& source,
        const float (&params)[4]) -> bool
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
            memcpy(constants.params, params, sizeof(float) * 4);

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

    const auto makeTable = [&](const RHIBindingDesc& source,
        RHITextureHandle rows = {}, RHITextureHandle marginal = {},
        RHITextureHandle samples = {})
    {
        const RHIBindingDesc views[] = {
            source,
            RHIBindingDesc::Srv2D(rows, kImportanceFormat).OrNull(),
            RHIBindingDesc::Srv2D(marginal, kImportanceFormat).OrNull(),
            RHIBindingDesc::Srv2D(samples, kImportanceFormat).OrNull(),
        };
        return m_resources->CreateBindings(views);
    };

    // ── ① rect → cube (밉 체인의 소스) ──
    const RHIBindingDesc equirectView = RHIBindingDesc::Srv2D(
        equirect, equirectFormat, 0, 1);
    const RHIBindingTable equirectTable = makeTable(equirectView);
    if (!equirectTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    {
        const float params[4]{ 0.f, 0.f, 0.f, 0.f };
        if (!drawFaces(m_rectToCubePso, m_cubeSourceHandle, 0,
            cubeSize, equirectTable, params))
        {
            outError = "IBL rect→cube 기록 실패(타깃/업로드)";
            return false;
        }
    }

    transition(m_cubeSourceHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    const RHIBindingDesc sourceView = RHIBindingDesc::SrvCube(
        m_cubeSourceHandle, kFormat, 1);
    const RHIBindingTable sourceTable = makeTable(sourceView);
    if (!sourceTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }

    // ── ①b 환경 큐브의 밉 체인 ──
    //
    // 밉 m은 소스의 2^m x 2^m 텍셀을 평균한다. 밉 m-1이 아니라 소스에서
    // 직접 뜨는 이유는 RHITransition이 텍스처 전체만 전이하기 때문이다 —
    // 한 리소스 안에서 밉을 읽으며 다른 밉에 쓸 수가 없다.
    {
        uint32_t mipSize = cubeSize;
        for (uint32_t mip = 0; mip < cubeMips; ++mip)
        {
            const float params[4]{
                0.f,
                static_cast<float>(1u << mip),      // 이 밉이 덮는 소스 텍셀 폭
                static_cast<float>(mipSize),        // 그리는 밉의 한 변
                0.f };
            if (!drawFaces(m_cubeDownsamplePso, m_cubeMapHandle, mip,
                mipSize, sourceTable, params))
            {
                outError = "IBL 큐브 밉 기록 실패(타깃/업로드)";
                return false;
            }
            mipSize = (mipSize > 1) ? mipSize / 2 : 1;
        }
    }

    transition(m_cubeMapHandle,
        RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);

    const RHIBindingDesc cubeView = RHIBindingDesc::SrvCube(
        m_cubeMapHandle, kFormat, cubeMips);
    const RHIBindingTable cubeTable = makeTable(cubeView);
    if (!cubeTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }

    // Build the luminance CDF and cache environment samples once per bake.
    const auto drawImportance = [&](RHIPipelineHandle pso, RHITextureHandle target,
        uint32_t width, uint32_t height, const RHIBindingTable& table) -> bool
    {
        if (!table.IsValid()) return false;
        IblDrawConstants constants{};
        constants.params[1] = static_cast<float>(m_importanceMip);
        constants.params[2] = static_cast<float>(m_importanceSize);
        const auto cb = m_resources->UploadConstants(&constants, sizeof(constants));
        const RHIColorTargetDesc color = RHIColorTargetDesc::Texture(target);
        const auto targets = m_resources->CreateRenderTargets(
            std::span<const RHIColorTargetDesc>{ &color, 1 });
        if (!cb.IsValid() || !targets.IsValid()) return false;
        encoder.SetViewportAndScissor(width, height);
        encoder.BindRenderTargets(targets);
        encoder.SetPipeline(RHIBindPoint::Graphics, pso);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
        encoder.SetBindings(RHIBindPoint::Graphics, 1, table);
        encoder.Draw(3, 1);
        transition(target, RHIResourceState::RenderTarget, RHIResourceState::PixelShaderResource);
        return true;
    };
    if (!drawImportance(m_importanceRowsPso, m_importanceRows,
            m_importanceSize, 6 * m_importanceSize, cubeTable) ||
        !drawImportance(m_importanceMarginalPso, m_importanceMarginal,
            1, 6 * m_importanceSize, makeTable(cubeView, m_importanceRows)) ||
        !drawImportance(m_importanceSamplesPso, m_importanceSamples,
            kImportanceSampleCount, 2, makeTable(cubeView, m_importanceRows, m_importanceMarginal)))
    {
        outError = "IBL importance distribution recording failed";
        return false;
    }
    const auto integrationTable = makeTable(cubeView, m_importanceRows,
        m_importanceMarginal, m_importanceSamples);
    if (!integrationTable.IsValid()) { outError = "IBL importance descriptors exhausted"; return false; }
    const float importanceSize = static_cast<float>(m_importanceSize);
    const float environmentSize = static_cast<float>(cubeSize);
    const float environmentMips = static_cast<float>(cubeMips);

    // ── ② 조도 맵 ──
    {
        const float params[4]{ 0.f, importanceSize, environmentSize, environmentMips };
        if (!drawFaces(m_irradiancePso, m_irradianceHandle, 0,
            GetIrradianceSize(), integrationTable, params))
        {
            outError = "IBL 조도 기록 실패(타깃/업로드)";
            return false;
        }
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
            const float params[4]{
                roughness, importanceSize, environmentSize, environmentMips };
            if (!drawFaces(m_prefilterPso, m_prefilteredHandle, mip,
                mipSize, integrationTable, params))
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

void EnhancedIBLGenerator::ReleaseTargets()
{
    RHITextureHandle* handles[] = { &m_cubeMapHandle, &m_cubeSourceHandle,
        &m_irradianceHandle, &m_prefilteredHandle, &m_brdfLutHandle,
        &m_importanceRows, &m_importanceMarginal, &m_importanceSamples };
    for (auto* handle : handles)
    {
        if (m_resources) m_resources->ReleaseTexture(*handle);
        *handle = {};
    }
}

void EnhancedIBLGenerator::Shutdown()
{
    ReleaseTargets();
    m_resources = nullptr;
    m_rectToCubePso = {};
    m_cubeDownsamplePso = {};
    m_irradiancePso = {};
    m_prefilterPso = {};
    m_brdfPso = {};
    m_importanceRowsPso = {};
    m_importanceMarginalPso = {};
    m_importanceSamplesPso = {};
    m_cubeSize = 0;
    m_brdfSize = 0;
    m_importanceSize = 0;
    m_importanceMip = 0;
}
