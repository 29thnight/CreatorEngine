#ifndef DYNAMICCPP_EXPORTS
#include "DX12PipelineLayoutTranslate.h"
#include "DX12PSOManager.h"
#include "DX12DeviceResources.h"

#include <fstream>
#include <sstream>
#include <iomanip>

namespace
{
    std::string PsoHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // FNV-1a 64. 암호학적 강도는 필요 없고, 셰이더 바이트코드까지 훑을 만큼
    // 빠르며 충돌이 실질적으로 없으면 된다.
    constexpr uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t HashPsoBytes(const void* data, size_t size, uint64_t seed = kFnvOffset)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
        return hash;
    }

    template <typename T>
    uint64_t HashValue(const T& value, uint64_t seed)
    {
        return HashPsoBytes(&value, sizeof(T), seed);
    }

    // 캐시 파일 머리에 찍는 도장. 자세한 사연은 Initialize의 주석에 있다.
#pragma pack(push, 1)
    struct PsoCacheHeader
    {
        uint64_t magic{ 0 };
        uint64_t schemaStamp{ 0 };
    };
#pragma pack(pop)

    constexpr uint64_t kPsoCacheMagic = 0x4345'5053'4F'00'01ull;   // "CEPSO" + 포맷 1

    PsoCacheHeader MakePsoCacheHeader()
    {
        // 스키마 도장 = 이 파일의 컴파일 시각.
        //
        // desc를 D3D12 desc로 옮기는 코드가 이 파일에 있으므로, 여기가 다시
        // 컴파일됐으면 변환이 바뀌었을 수 있다고 본다. 주석 한 줄만 고쳐도
        // 캐시가 버려지지만 그 대가는 PSO 몇 개 재컴파일뿐이고, 반대 방향의
        // 대가는 '낡은 캐시로 조용히 다른 PSO를 쓰는 것'이라 비교가 안 된다.
        //
        // 손으로 올리는 버전 번호를 쓰지 않는 이유도 같다 — 올리는 것을
        // 잊는 순간 같은 증상으로 돌아오고, 그때는 원인이 캐시라는 것부터
        // 다시 알아내야 한다.
        PsoCacheHeader header{};
        header.magic = kPsoCacheMagic;
        header.schemaStamp = HashPsoBytes(__DATE__ __TIME__, sizeof(__DATE__ __TIME__) - 1);
        return header;
    }
}

uint64_t DX12PSOManager::ComputeHash(const RHIGraphicsPipelineDesc& desc) const
{
    uint64_t hash = kFnvOffset;

    // 셰이더는 내용으로 — 이것이 핫리로드 시 자동 무효화의 근거다.
    if (desc.vsBytecode && desc.vsSize > 0) hash = HashPsoBytes(desc.vsBytecode, desc.vsSize, hash);
    if (desc.psBytecode && desc.psSize > 0) hash = HashPsoBytes(desc.psBytecode, desc.psSize, hash);

    // ★ 핸들이 아니라 **표가 든 안정 해시**를 넣는다(A-1). 핸들은 슬롯+세대라
    //   실행마다 달라지고, 그것을 넣으면 PSO 디스크 캐시의 키가 매 실행 바뀌어
    //   라이브러리가 통째로 논다 — 예전에 호출부가 `rootSignatureId` 를 손으로
    //   주던 이유가 정확히 이것이었고, 그 이유는 핸들로 바꿔도 사라지지 않는다.
    //   `dx12.psocache` 의 "2회차 컴파일 0건"이 이 줄을 지키는 판정이다.
    hash = HashValue(ResolveStableHash(desc.layout), hash);

    // 입력 레이아웃도 내용으로. semantic은 포인터라 문자열을 따라 들어간다.
    hash = HashValue(desc.inputElementCount, hash);
    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const RHIInputElement& element = desc.inputElements[i];
        if (element.semantic)
        {
            hash = HashPsoBytes(element.semantic, strlen(element.semantic), hash);
        }
        hash = HashValue(element.semanticIndex, hash);
        hash = HashValue(element.format, hash);
        hash = HashValue(element.inputSlot, hash);
        hash = HashValue(element.alignedByteOffset, hash);
        hash = HashValue(element.instanceDataStepRate, hash);
    }

    hash = HashValue(desc.fillMode, hash);
    hash = HashValue(desc.cullMode, hash);
    hash = HashValue(desc.depthEnable, hash);
    hash = HashValue(desc.blendEnable, hash);

    // 파이프라인을 실제로 가르는 값은 전부 해시에 들어가야 한다. 빠뜨리면
    // 서로 다른 PSO가 같은 키를 갖게 되고, 먼저 만들어진 쪽이 조용히
    // 재사용된다 — '블렌드가 가끔 이상하다'로만 드러나는 종류의 버그다.
    hash = HashValue(desc.depthWriteMask, hash);
    hash = HashValue(desc.depthFunc, hash);
    hash = HashValue(desc.independentBlend, hash);
    if (desc.independentBlend)
    {
        for (uint32_t i = 0; i < desc.numRenderTargets && i < 8; ++i)
        {
            hash = HashPsoBytes(&desc.renderTargetBlend[i], sizeof(desc.renderTargetBlend[i]), hash);
        }
    }

    hash = HashValue(desc.topologyType, hash);
    hash = HashValue(desc.numRenderTargets, hash);
    for (uint32_t i = 0; i < desc.numRenderTargets && i < 8; ++i)
    {
        hash = HashValue(desc.rtvFormats[i], hash);
    }
    hash = HashValue(desc.dsvFormat, hash);
    hash = HashValue(desc.sampleCount, hash);
    return hash;
}

uint64_t DX12PSOManager::ComputeHash(const RHIComputePipelineDesc& desc) const
{
    // 그래픽과 다른 시드로 시작한다 — 같은 해시 공간을 쓰지만 종류가 섞이지 않는다.
    constexpr uint64_t kComputeTag = 0x43'4F'4D'50'55'54'45'00ull; // "COMPUTE"
    uint64_t hash = HashValue(kComputeTag, kFnvOffset);

    if (desc.csBytecode && desc.csSize > 0) hash = HashPsoBytes(desc.csBytecode, desc.csSize, hash);

    hash = HashValue(ResolveStableHash(desc.layout), hash);
    return hash;
}

ID3D12RootSignature* DX12PSOManager::ResolveSignature(RHIPipelineLayoutHandle layout) const
{
    return (nullptr != m_resources) ? m_resources->Resolve(layout).signature : nullptr;
}

uint64_t DX12PSOManager::ResolveStableHash(RHIPipelineLayoutHandle layout) const
{
    return (nullptr != m_resources) ? m_resources->Resolve(layout).stableHash : 0;
}

std::wstring DX12PSOManager::MakeLibraryName(uint64_t hash)
{
    std::wostringstream oss;
    oss << L"PSO_" << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return oss.str();
}

bool DX12PSOManager::Initialize(DX12DeviceResources* resources,
    const std::wstring& cacheFilePath, std::string& outError)
{
    if (nullptr == resources || nullptr == resources->GetDevice())
    {
        outError = "디바이스가 없다";
        return false;
    }

    m_resources = resources;
    m_cachePath = cacheFilePath;

    // PipelineLibrary는 ID3D12Device1부터다. 없으면 메모리 캐시만으로 동작한다 —
    // 기능이 죽는 게 아니라 디스크 캐시만 빠지는 것이므로 실패로 취급하지 않는다.
    if (FAILED(resources->GetDevice()->QueryInterface(IID_PPV_ARGS(&m_device))))
    {
        outError = "ID3D12Device1 미지원 — 디스크 캐시 없이 진행";
        return true;
    }

    // 기존 캐시 파일을 읽어 라이브러리를 복원한다. 드라이버나 어댑터가 바뀌었으면
    // 여기서 실패하고(런타임이 판정한다) 빈 라이브러리로 시작한다.
    //
    // ★ 런타임의 판정만으로는 모자란다. 그쪽은 드라이버·어댑터가 바뀐 것만
    //   알지, 우리가 desc를 D3D12 desc로 옮기는 방식이 바뀐 것은 모른다.
    //   실제로 그것으로 한 번 물렸다: 래스터라이저의 DepthClipEnable을 FALSE
    //   에서 TRUE로 고쳤는데, 그 필드는 우리 desc 구조체에 없어 해시에도 안
    //   들어간다. 그래서 이름(해시)은 그대로인데 내용이 달라졌고, 예전 캐시
    //   파일을 쥔 채로 실행하면 매번 이렇게 경고가 났다:
    //
    //     Load*Pipeline: The pipeline state desc provided does not match
    //                    the one used to create the PSO stored with name "..."
    //     StorePipeline: A pipeline with name "..." already exists
    //
    //   dx12.selftest는 검증 레이어 메시지 0건이 통과 조건이라 이것만으로
    //   실패했다. 그림은 멀쩡했고 캐시 파일만 낡았던 것이다.
    //
    // 그래서 파일 앞에 스키마 도장을 찍는다. 도장이 다르면 파일을 버린다.
    // 도장 값은 이 파일의 컴파일 시각이다 — 변환 코드가 여기 있으므로,
    // 여기가 다시 컴파일됐으면 변환이 바뀌었을 수 있다고 본다. 사람이
    // 버전을 올려 주기를 기대하는 방식은 잊는 순간 같은 증상으로 돌아온다.
    std::ifstream file(m_cachePath, std::ios::binary | std::ios::ate);
    if (file)
    {
        const auto fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0);

        PsoCacheHeader stored{};
        bool headerOk = false;
        if (fileSize > sizeof(PsoCacheHeader))
        {
            file.read(reinterpret_cast<char*>(&stored), sizeof(stored));
            const PsoCacheHeader expected = MakePsoCacheHeader();
            headerOk = (stored.magic == expected.magic)
                && (stored.schemaStamp == expected.schemaStamp);
        }

        if (headerOk)
        {
            m_libraryBlob.resize(fileSize - sizeof(PsoCacheHeader));
            file.read(reinterpret_cast<char*>(m_libraryBlob.data()),
                static_cast<std::streamsize>(m_libraryBlob.size()));
            file.close();

            const HRESULT hr = m_device->CreatePipelineLibrary(m_libraryBlob.data(),
                m_libraryBlob.size(), IID_PPV_ARGS(&m_library));
            if (SUCCEEDED(hr))
            {
                m_libraryLoadedFromDisk = true;
            }
            else
            {
                // D3D12_ERROR_DRIVER_VERSION_MISMATCH / ADAPTER_NOT_FOUND 등 — 정상 경로다.
                m_libraryBlob.clear();
            }
        }
        else
        {
            file.close();
            m_libraryBlob.clear();
            ++m_stats.cacheDiscarded;
        }
    }

    if (!m_library)
    {
        const HRESULT hr = m_device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_library));
        if (FAILED(hr))
        {
            outError = "파이프라인 라이브러리 생성 실패 " + PsoHrToString(hr);
            // 라이브러리가 없어도 메모리 캐시로는 동작한다.
        }
    }

    return true;
}

void DX12PSOManager::Shutdown()
{
    // 진행 중인 비동기 컴파일을 모두 회수한 뒤에 해제해야 한다 —
    // future가 잡고 있는 디바이스 참조가 남으면 종료 순서가 꼬인다.
    std::vector<std::shared_future<ComPtr<ID3D12PipelineState>>> inFlight;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto& [hash, future] : m_pending) inFlight.push_back(future);
        m_pending.clear();
    }
    for (auto& future : inFlight)
    {
        if (future.valid()) future.wait();
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.clear();
    m_library.Reset();
    m_libraryBlob.clear();
    m_device.Reset();
    m_resources = nullptr;
}

DX12PSOManager::ComPtr<ID3D12PipelineState> DX12PSOManager::CreateOne(
    const RHIGraphicsPipelineDesc& desc, uint64_t hash, std::string& outError)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d3dDesc{};
    d3dDesc.pRootSignature = ResolveSignature(desc.layout);
    if (nullptr == d3dDesc.pRootSignature)
    {
        // ★ 조용히 넘기지 않는다. 예전에는 호출부가 포인터를 직접 넣었으므로
        //   널이면 D3D12 가 잡아 줬는데, 이제 표가 사이에 있어 "이미 놓인
        //   핸들"이라는 새 실패 모드가 생겼다.
        outError = "파이프라인 레이아웃 핸들이 유효하지 않다";
        ++m_stats.failures;
        return nullptr;
    }
    d3dDesc.VS = { desc.vsBytecode, desc.vsSize };
    d3dDesc.PS = { desc.psBytecode, desc.psSize };
    d3dDesc.RasterizerState.FillMode = DX12Translate::ToD3D12(desc.fillMode);
    d3dDesc.RasterizerState.CullMode = DX12Translate::ToD3D12(desc.cullMode);

    // ★ DepthClipEnable을 명시한다.
    //
    // D3D12_GRAPHICS_PIPELINE_STATE_DESC를 0으로 초기화하면 이 값이 FALSE가
    // 된다. D3D11의 CD3D11_DEFAULT()는 TRUE이므로, 아무것도 쓰지 않는 것이
    // 곧 '두 경로가 다르게 동작한다'는 뜻이었다.
    //
    // 0 초기화가 기본값을 준다는 착각이 부르는 종류의 차이다. D3D12는
    // 구조체를 그대로 받으므로 '안 쓴 필드'가 0이 되고, 0이 API의 기본값과
    // 같으리라는 보장이 없다.
    //
    // DX11 대조에서 바닥 평면의 먼 쪽이 DX12에서만 잘리는 것을 쫓다가 찾았다.
    // 경계가 완전한 직선(계단 0곳)이라 클리핑이 의심됐고, 클리핑 관련 설정
    // 중 두 경로가 갈리는 것이 이것뿐이었다.
    d3dDesc.RasterizerState.DepthClipEnable = TRUE;
    if (desc.independentBlend)
    {
        // 호출부가 타깃마다 정한 것을 그대로 쓴다. IndependentBlendEnable을
        // 켜야 RenderTarget[1..7]이 읽히고, 끄면 D3D12가 [0]만 보고 나머지를
        // 무시한다 — 켜는 것을 잊으면 채널별 마스크가 조용히 사라진다.
        d3dDesc.BlendState.IndependentBlendEnable = TRUE;
        for (uint32_t i = 0; i < 8; ++i)
        {
            d3dDesc.BlendState.RenderTarget[i] = DX12Translate::ToD3D12(desc.renderTargetBlend[i]);
        }
    }
    else
    {
        d3dDesc.BlendState.RenderTarget[0].BlendEnable = desc.blendEnable ? TRUE : FALSE;
        d3dDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        d3dDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        d3dDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        // ★ 대상의 알파를 보존한다 (ZERO·ONE — 예전에는 ONE·ZERO였다).
        //
        //   반투명 패스(아이콘·라인·투명 큐)가 src.a를 대상 알파에 그대로
        //   써 넣으면, 그 대상이 표시 경로의 공유 텍스처로 복사된 뒤 ImGui가
        //   알파 블렌드로 그릴 때 뷰포트에 구멍이 뚫린다 — 기즈모 아이콘의
        //   투명 영역(a≈0)이 에디터 패널 배경색 사각형으로 보이던 버그가
        //   그것이다(2026-08-09, 픽셀 실측 56,56,56 = ImGui 배경). 색은 내내
        //   정상이었고 알파 채널만 오염이었다. 화면에 남는 알파는 '덮어 쓴
        //   비율'이 아니라 '이 텍스처를 표시할 때의 불투명도'다 — 1로 남아야
        //   한다.
        d3dDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
        d3dDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        d3dDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        d3dDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    d3dDesc.DepthStencilState.DepthEnable = desc.depthEnable ? TRUE : FALSE;
    d3dDesc.DepthStencilState.DepthWriteMask = DX12Translate::ToD3D12(desc.depthWriteMask);
    d3dDesc.DepthStencilState.DepthFunc = DX12Translate::ToD3D12Depth(desc.depthFunc);
    d3dDesc.SampleMask = UINT_MAX;
    // 중립 원소를 DX12 배열로 편다. PSO 생성이 끝날 때까지만 살면 된다.
    std::vector<D3D12_INPUT_ELEMENT_DESC> nativeElements;
    nativeElements.reserve(desc.inputElementCount);
    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        nativeElements.push_back(DX12Translate::ToD3D12(desc.inputElements[i]));
    }
    d3dDesc.InputLayout = { nativeElements.data(), desc.inputElementCount };
    d3dDesc.PrimitiveTopologyType = DX12Translate::ToD3D12(desc.topologyType);
    d3dDesc.NumRenderTargets = desc.numRenderTargets;
    for (uint32_t i = 0; i < desc.numRenderTargets && i < 8; ++i)
    {
        d3dDesc.RTVFormats[i] = ToDXGI(desc.rtvFormats[i]);
    }
    d3dDesc.DSVFormat = ToDXGI(desc.dsvFormat);
    d3dDesc.SampleDesc.Count = desc.sampleCount;

    const std::wstring name = MakeLibraryName(hash);
    ComPtr<ID3D12PipelineState> pso;

    // 1) 라이브러리에서 복원 시도 — 성공하면 드라이버 컴파일이 없다.
    if (m_library)
    {
        const HRESULT hr = m_library->LoadGraphicsPipeline(name.c_str(), &d3dDesc,
            IID_PPV_ARGS(&pso));
        if (SUCCEEDED(hr))
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.libraryHits;
            return pso;
        }
        // E_INVALIDARG = 그 이름이 라이브러리에 없음. 정상적인 첫 요청 경로다.
    }

    // 2) 실제 컴파일
    if (!m_device)
    {
        outError = "디바이스가 없다";
        return nullptr;
    }

    const HRESULT hr = m_device->CreateGraphicsPipelineState(&d3dDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
    {
        outError = "PSO 생성 실패 " + PsoHrToString(hr);
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.failures;
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.compiles;
    }

    // 3) 라이브러리에 등록 — 다음 실행에서 복원된다.
    if (m_library)
    {
        m_library->StorePipeline(name.c_str(), pso.Get());
    }

    return pso;
}

DX12PSOManager::ComPtr<ID3D12PipelineState> DX12PSOManager::CreateOneCompute(
    const RHIComputePipelineDesc& desc, uint64_t hash, std::string& outError)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC d3dDesc{};
    d3dDesc.pRootSignature = ResolveSignature(desc.layout);
    if (nullptr == d3dDesc.pRootSignature)
    {
        outError = "파이프라인 레이아웃 핸들이 유효하지 않다";
        ++m_stats.failures;
        return nullptr;
    }
    d3dDesc.CS = { desc.csBytecode, desc.csSize };

    const std::wstring name = MakeLibraryName(hash);
    ComPtr<ID3D12PipelineState> pso;

    if (m_library)
    {
        const HRESULT hr = m_library->LoadComputePipeline(name.c_str(), &d3dDesc,
            IID_PPV_ARGS(&pso));
        if (SUCCEEDED(hr))
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.libraryHits;
            return pso;
        }
    }

    if (!m_device)
    {
        outError = "디바이스가 없다";
        return nullptr;
    }

    const HRESULT hr = m_device->CreateComputePipelineState(&d3dDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
    {
        outError = "컴퓨트 PSO 생성 실패 " + PsoHrToString(hr);
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.failures;
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.compiles;
    }

    if (m_library)
    {
        m_library->StorePipeline(name.c_str(), pso.Get());
    }

    return pso;
}

RHIPipelineHandle DX12PSOManager::GetOrCreateCompute(const RHIComputePipelineDesc& desc,
    std::string& outError)
{
    const uint64_t hash = ComputeHash(desc);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.memoryHits;
            return found->second.handle;
        }
    }

    ComPtr<ID3D12PipelineState> pso = CreateOneCompute(desc, hash, outError);
    if (!pso) return {};

    std::lock_guard<std::mutex> guard(m_mutex);
    return Publish(hash, pso, desc.layout, outError);
}

bool DX12PSOManager::SetFallback(const RHIGraphicsPipelineDesc& desc, std::string& outError)
{
    // 폴백은 동기로 만든다 — 폴백 자체가 준비되지 않으면 존재 이유가 없다.
    const RHIPipelineHandle handle = GetOrCreate(desc, outError);
    if (!handle.IsValid()) return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    const auto found = m_cache.find(ComputeHash(desc));
    if (found == m_cache.end()) return false;
    m_fallback = found->second.pso;
    return true;
}

DX12PSOManager::DrawDecision DX12PSOManager::Resolve(const RHIGraphicsPipelineDesc& desc,
    ID3D12PipelineState** outPso)
{
    ID3D12PipelineState* requested = nullptr;
    const RequestState state = Request(desc, &requested);

    if (state == RequestState::Ready && requested)
    {
        if (outPso) *outPso = requested;
        return DrawDecision::UseRequested;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_fallback)
    {
        ++m_stats.fallbackDraws;
        if (outPso) *outPso = m_fallback.Get();
        return DrawDecision::UseFallback;
    }

    ++m_stats.skippedDraws;
    if (outPso) *outPso = nullptr;
    return DrawDecision::Skip;
}

void DX12PSOManager::OnShaderReloaded()
{
    // 진행 중인 컴파일은 회수한다 — 옛 바이트코드를 참조하는 작업이 남으면
    // 리로드로 해제된 블롭을 읽을 수 있다.
    std::vector<std::shared_future<ComPtr<ID3D12PipelineState>>> inFlight;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (auto& [hash, future] : m_pending) inFlight.push_back(future);
        m_pending.clear();
    }
    for (auto& future : inFlight)
    {
        if (future.valid()) future.wait();
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.clear();
    m_fallback.Reset();
}

RHIPipelineHandle DX12PSOManager::GetOrCreate(const RHIGraphicsPipelineDesc& desc,
    std::string& outError)
{
    const uint64_t hash = ComputeHash(desc);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.memoryHits;
            return found->second.handle;
        }
    }

    ComPtr<ID3D12PipelineState> pso = CreateOne(desc, hash, outError);
    if (!pso) return {};

    std::lock_guard<std::mutex> guard(m_mutex);
    return Publish(hash, pso, desc.layout, outError);
}

/// 캐시에 넣고 핸들을 발급한다. **락을 쥔 채로** 부른다.
///
/// ★ 경합으로 다른 스레드가 먼저 넣었으면 그쪽 핸들을 쓴다 — 같은 desc 가
///   핸들 둘을 갖지 않는 것이 표가 자라지 않는 조건이다.
///
/// ★ 표 자체에는 락이 없다(DX12ResourceTable). 그래서 발급은 반드시 이 락
///   안에서, 즉 부르는 스레드에서만 한다 — 컴파일은 백그라운드로 가도
///   등록은 여기로 모인다.
RHIPipelineHandle DX12PSOManager::Publish(uint64_t hash, ComPtr<ID3D12PipelineState> pso,
    RHIPipelineLayoutHandle layout, std::string& outError)
{
    const auto found = m_cache.find(hash);
    if (found != m_cache.end()) return found->second.handle;

    CacheEntry entry{};
    entry.pso = pso;
    entry.handle = m_resources->RegisterPipeline(pso.Get(), ResolveSignature(layout));
    if (!entry.handle.IsValid())
    {
        ++m_stats.failures;
        outError = "파이프라인 핸들 발급 실패 — 표가 가득 찼다";
        return {};
    }

    m_cache.emplace(hash, entry);
    return entry.handle;
}

DX12PSOManager::RequestState DX12PSOManager::Request(const RHIGraphicsPipelineDesc& desc,
    ID3D12PipelineState** outPso)
{
    if (outPso) *outPso = nullptr;
    const uint64_t hash = ComputeHash(desc);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.memoryHits;
            if (outPso) *outPso = found->second.pso.Get();
            return RequestState::Ready;
        }

        auto pendingIt = m_pending.find(hash);
        if (pendingIt != m_pending.end())
        {
            // 아직 컴파일 중이면 이 프레임은 폴백으로 간다.
            if (pendingIt->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                return RequestState::Pending;
            }

            ComPtr<ID3D12PipelineState> pso = pendingIt->second.get();
            m_pending.erase(pendingIt);
            if (!pso) return RequestState::Failed;

            std::string ignored;
            if (!Publish(hash, pso, desc.layout, ignored).IsValid()) return RequestState::Failed;
            if (outPso) *outPso = m_cache.find(hash)->second.pso.Get();
            return RequestState::Ready;
        }

        // 미스 — 백그라운드 컴파일을 걸고 이 프레임은 넘긴다.
        // desc를 값으로 복사해 넘긴다. 셰이더 바이트코드 포인터는 호출부가
        // 컴파일 완료까지 살려 둬야 한다(블롭 수명은 PSOManager가 모른다).
        m_pending.emplace(hash, std::async(std::launch::async,
            [this, desc, hash]() -> ComPtr<ID3D12PipelineState>
            {
                std::string ignored;
                return CreateOne(desc, hash, ignored);
            }).share());
    }

    return RequestState::Pending;
}

bool DX12PSOManager::SaveCache(std::string& outError)
{
    if (!m_library) { outError = "라이브러리 없음"; return false; }

    const SIZE_T size = m_library->GetSerializedSize();
    if (0 == size) { outError = "직렬화 크기 0"; return false; }

    std::vector<uint8_t> blob(size);
    const HRESULT hr = m_library->Serialize(blob.data(), size);
    if (FAILED(hr)) { outError = "직렬화 실패 " + PsoHrToString(hr); return false; }

    std::ofstream file(m_cachePath, std::ios::binary | std::ios::trunc);
    if (!file) { outError = "캐시 파일 열기 실패"; return false; }

    const PsoCacheHeader header = MakePsoCacheHeader();
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(size));
    return true;
}

DX12PSOManager::Stats DX12PSOManager::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

#endif
