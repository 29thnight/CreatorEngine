#ifndef DYNAMICCPP_EXPORTS
#include "DX12PSOManager.h"

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

    uint64_t HashBytes(const void* data, size_t size, uint64_t seed = kFnvOffset)
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
        return HashBytes(&value, sizeof(T), seed);
    }
}

uint64_t DX12GraphicsPipelineDesc::ComputeHash() const
{
    uint64_t hash = kFnvOffset;

    // 셰이더는 내용으로 — 이것이 핫리로드 시 자동 무효화의 근거다.
    if (vsBytecode && vsSize > 0) hash = HashBytes(vsBytecode, vsSize, hash);
    if (psBytecode && psSize > 0) hash = HashBytes(psBytecode, psSize, hash);

    hash = HashValue(rootSignatureId, hash);
    hash = HashValue(fillMode, hash);
    hash = HashValue(cullMode, hash);
    hash = HashValue(depthEnable, hash);
    hash = HashValue(blendEnable, hash);
    hash = HashValue(topologyType, hash);
    hash = HashValue(numRenderTargets, hash);
    for (uint32_t i = 0; i < numRenderTargets && i < 8; ++i)
    {
        hash = HashValue(rtvFormats[i], hash);
    }
    hash = HashValue(dsvFormat, hash);
    hash = HashValue(sampleCount, hash);
    return hash;
}

std::wstring DX12PSOManager::MakeLibraryName(uint64_t hash)
{
    std::wostringstream oss;
    oss << L"PSO_" << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return oss.str();
}

bool DX12PSOManager::Initialize(ID3D12Device* device, const std::wstring& cacheFilePath,
    std::string& outError)
{
    m_cachePath = cacheFilePath;

    // PipelineLibrary는 ID3D12Device1부터다. 없으면 메모리 캐시만으로 동작한다 —
    // 기능이 죽는 게 아니라 디스크 캐시만 빠지는 것이므로 실패로 취급하지 않는다.
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device))))
    {
        outError = "ID3D12Device1 미지원 — 디스크 캐시 없이 진행";
        return true;
    }

    // 기존 캐시 파일을 읽어 라이브러리를 복원한다. 드라이버나 어댑터가 바뀌었으면
    // 여기서 실패하고(런타임이 판정한다) 빈 라이브러리로 시작한다.
    std::ifstream file(m_cachePath, std::ios::binary | std::ios::ate);
    if (file)
    {
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        m_libraryBlob.resize(size);
        file.read(reinterpret_cast<char*>(m_libraryBlob.data()), size);
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
}

DX12PSOManager::ComPtr<ID3D12PipelineState> DX12PSOManager::CreateOne(
    const DX12GraphicsPipelineDesc& desc, uint64_t hash, std::string& outError)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d3dDesc{};
    d3dDesc.pRootSignature = desc.rootSignature;
    d3dDesc.VS = { desc.vsBytecode, desc.vsSize };
    d3dDesc.PS = { desc.psBytecode, desc.psSize };
    d3dDesc.RasterizerState.FillMode = desc.fillMode;
    d3dDesc.RasterizerState.CullMode = desc.cullMode;
    d3dDesc.BlendState.RenderTarget[0].BlendEnable = desc.blendEnable ? TRUE : FALSE;
    d3dDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    d3dDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    d3dDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    d3dDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    d3dDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    d3dDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d3dDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d3dDesc.DepthStencilState.DepthEnable = desc.depthEnable ? TRUE : FALSE;
    d3dDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d3dDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    d3dDesc.SampleMask = UINT_MAX;
    d3dDesc.PrimitiveTopologyType = desc.topologyType;
    d3dDesc.NumRenderTargets = desc.numRenderTargets;
    for (uint32_t i = 0; i < desc.numRenderTargets && i < 8; ++i)
    {
        d3dDesc.RTVFormats[i] = desc.rtvFormats[i];
    }
    d3dDesc.DSVFormat = desc.dsvFormat;
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

ID3D12PipelineState* DX12PSOManager::GetOrCreate(const DX12GraphicsPipelineDesc& desc,
    std::string& outError)
{
    const uint64_t hash = desc.ComputeHash();

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.memoryHits;
            return found->second.Get();
        }
    }

    ComPtr<ID3D12PipelineState> pso = CreateOne(desc, hash, outError);
    if (!pso) return nullptr;

    std::lock_guard<std::mutex> guard(m_mutex);
    // 경합으로 다른 스레드가 먼저 넣었으면 그것을 쓴다(중복 PSO를 남기지 않는다).
    auto [it, inserted] = m_cache.try_emplace(hash, pso);
    return it->second.Get();
}

DX12PSOManager::RequestState DX12PSOManager::Request(const DX12GraphicsPipelineDesc& desc,
    ID3D12PipelineState** outPso)
{
    if (outPso) *outPso = nullptr;
    const uint64_t hash = desc.ComputeHash();

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.memoryHits;
            if (outPso) *outPso = found->second.Get();
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

            auto [it, inserted] = m_cache.try_emplace(hash, pso);
            if (outPso) *outPso = it->second.Get();
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
    file.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(size));
    return true;
}

DX12PSOManager::Stats DX12PSOManager::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

#endif
