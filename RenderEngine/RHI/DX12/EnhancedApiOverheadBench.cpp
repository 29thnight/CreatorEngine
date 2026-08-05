#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"
#include "DX12DeviceResources.h"
#include "DX12CommandListPool.h"
#include "DX12GpuProfiler.h"

#include <d3d11.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// DX11 vs DX12 API 오버헤드 실측 (마이그레이션 전제 검증).
//
// ── 왜 이 비교를 하는가 ──
//
// 기존 Scale 테스트들은 "DX11과 직접 재지 않는다"가 원칙이었다 — API·드라이버
// 차이가 수에 섞이면 알고리즘 개선을 분리할 수 없기 때문이다. 여기는 정반대다.
// 이 마이그레이션 전체의 전제가 "DX12가 실제로 빠른가"이고, 그 전제에서
// 측정 대상이 바로 그 API·드라이버 차이다. 섞이면 안 되는 것이 아니라
// 그것만 남겨야 한다.
//
// ── 그래서 무엇을 고정하는가 ──
//
// 갈리는 것이 API 하나뿐이도록 나머지를 전부 맞춘다:
//   · 같은 물리 어댑터(LUID 일치 — 공유 텍스처 검증과 같은 방법)
//   · 같은 기하(단위 쿼드) · 같은 드로우 수 · 같은 해상도 · 깊이/블렌드 없음
//   · 같은 HLSL 소스(vs_5_0/ps_5_0으로 양쪽 컴파일)
//   · 같은 드로우당 페이로드(80바이트: 월드 행렬 + 색)
//   · 드로우당 상수 갱신 경로만 각 엔진의 실제 패턴을 따른다:
//     DX11 = Map(DISCARD) 상수버퍼(SceneRenderer의 per-object 패턴),
//     DX12 = 업로드 링에서 잘라 루트 CBV(Enhanced 패스들의 패턴)
//
// ── 무엇을 재는가 ──
//
//   기록 CPU  드로우 N건을 발행하는 데 드는 스레드 시간 — 마이그레이션의
//             핵심 주장("드로우콜이 싸진다")이 바로 이 수다
//   제출 CPU  DX11 Flush / DX12 Close+Execute
//   GPU       타임스탬프(DX11 disjoint 쿼리 / DX12 프로파일러)
//
// DX12는 순차 기록과 병렬 기록(워커 4)을 둘 다 잰다 — 병렬 기록은 DX11에
// 없는 능력이라 전제의 일부다.
//
// ── 이 측정이 말하지 않는 것 ──
//
// 워크로드가 "상태 변경 없는 최소 드로우"다. 실제 씬은 재질·텍스처 전환이
// 섞이고, 그때 DX11 드라이버의 상태 추적 비용은 더 커진다 — 즉 이 수는
// DX11에 유리한 쪽으로 치우친 하한이다. 또 DX11 드라이버는 자체 스레드로
// 일을 미루므로 여기서 잰 CPU는 앱 스레드 몫만이다(전체 CPU 소비가 아니다).
namespace
{
    using Microsoft::WRL::ComPtr;
    using namespace DirectX;

    constexpr uint32_t kBenchWidth = 1280;
    constexpr uint32_t kBenchHeight = 720;

    // 드로우 수 스윕. 최대치가 곧 격자 한 변(128²)을 정한다.
    constexpr uint32_t kDrawCounts[] = { 256, 1024, 4096, 16384 };
    constexpr uint32_t kMaxDraws = 16384;
    constexpr uint32_t kGridSide = 128;

    constexpr uint32_t kBenchWarmupFrames = 3;
    constexpr uint32_t kBenchMeasureFrames = 9;   // 홀수 — 중앙값이 실제 표본이 되게

    constexpr uint32_t kParallelWorkers = 4;

    // 양쪽이 같은 소스를 컴파일한다. row_major를 명시해 전치 없이 XMFLOAT4X4를
    // 그대로 올린다 — 규약 변환이 한쪽에만 끼면 그 비용이 측정에 섞인다.
    constexpr const char* kBenchShader = R"(
cbuffer PerDraw : register(b0)
{
    row_major float4x4 gWorld;
    float4             gColor;
};

struct VSIn  { float3 position : POSITION; };
struct VSOut { float4 position : SV_POSITION; float4 color : COLOR0; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.position = mul(float4(input.position, 1.0f), gWorld);
    output.color = gColor;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
)";

    // 드로우당 페이로드. 80바이트 — 실제 프레임의 per-object 상수(월드 행렬 +
    // 부가 데이터)에 해당하는 크기다. 상수버퍼 규칙(16바이트 배수)도 만족한다.
    struct PerDraw
    {
        XMFLOAT4X4 world;
        XMFLOAT4   color;
    };
    static_assert(sizeof(PerDraw) == 80, "PerDraw는 80바이트여야 한다");

    struct BenchVertex { float x, y, z; };

    double BenchMedian(std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    struct Stopwatch
    {
        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        Stopwatch() { ::QueryPerformanceFrequency(&frequency); }
        void Start() { ::QueryPerformanceCounter(&started); }
        double ElapsedMs() const
        {
            LARGE_INTEGER now;
            ::QueryPerformanceCounter(&now);
            return static_cast<double>(now.QuadPart - started.QuadPart)
                * 1000.0 / static_cast<double>(frequency.QuadPart);
        }
    };

    // 드로우당 페이로드를 미리 만들어 둔다. 행렬 생성은 양쪽 공통 비용이라
    // 측정 구간에 넣지 않는다 — 재려는 것은 API 발행 비용이지 산술이 아니다.
    std::vector<PerDraw> MakePayloads()
    {
        std::vector<PerDraw> payloads(kMaxDraws);
        const float cellWidth = 2.f / kGridSide;
        const float cellHeight = 2.f / kGridSide;

        for (uint32_t i = 0; i < kMaxDraws; ++i)
        {
            const uint32_t cellX = i % kGridSide;
            const uint32_t cellY = i / kGridSide;
            const float centerX = -1.f + (cellX + 0.5f) * cellWidth;
            const float centerY = -1.f + (cellY + 0.5f) * cellHeight;

            // 쿼드가 셀의 절반을 덮는다 — 이웃과 겹치지 않아 커버리지 픽셀 수가
            // 드로우 수에 정확히 비례하고, 그 수가 양쪽 대조의 기준이 된다.
            const XMMATRIX world = XMMatrixScaling(cellWidth * 0.5f, cellHeight * 0.5f, 1.f)
                * XMMatrixTranslation(centerX, centerY, 0.5f);
            XMStoreFloat4x4(&payloads[i].world, world);

            payloads[i].color = XMFLOAT4(
                0.25f + 0.75f * static_cast<float>(i % 7) / 6.f,
                0.25f + 0.75f * static_cast<float>(i % 5) / 4.f,
                0.25f + 0.75f * static_cast<float>(i % 3) / 2.f,
                1.f);
        }
        return payloads;
    }

    bool CompileBenchShader(const char* entry, const char* target,
        ComPtr<ID3DBlob>& outBlob, std::string& outLog)
    {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kBenchShader, strlen(kBenchShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outLog += "셰이더 컴파일 실패(";
            outLog += entry;
            outLog += "): ";
            if (errors) outLog += static_cast<const char*>(errors->GetBufferPointer());
            outLog += "\n";
            return false;
        }
        return true;
    }

    // 한 지점의 측정치. 중앙값으로 채운다.
    struct Sample
    {
        double recordMs{ 0.0 };
        double submitMs{ 0.0 };
        double gpuMs{ 0.0 };
        uint32_t coverage{ 0 };   // 클리어 색이 아닌 픽셀 수 — '실제로 그렸는가'의 증거
    };

    // ── DX11 쪽 ──────────────────────────────────────────────────────────
    //
    // 에디터의 살아 있는 디바이스를 쓰지 않는다. 그 디바이스는 CB/CE 스레드가
    // 프레임마다 쓰고 있어서, 여기서 같이 쓰면 측정에 에디터 프레임이 섞이고
    // 최악에는 immediate context를 두 스레드가 만진다. 전용 디바이스를 새로
    // 세운다 — DX12 쪽도 전용 디바이스라 조건이 오히려 대칭이다.
    struct Dx11Bench
    {
        ComPtr<ID3D11Device>           device;
        ComPtr<ID3D11DeviceContext>    context;
        ComPtr<ID3D11Texture2D>        renderTarget;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11Texture2D>        staging;
        ComPtr<ID3D11Buffer>           vertexBuffer;
        ComPtr<ID3D11Buffer>           indexBuffer;
        ComPtr<ID3D11Buffer>           constantBuffer;
        ComPtr<ID3D11VertexShader>     vs;
        ComPtr<ID3D11PixelShader>      ps;
        ComPtr<ID3D11InputLayout>      inputLayout;
        ComPtr<ID3D11RasterizerState>  rasterizer;
        ComPtr<ID3D11Query>            disjointQuery;
        ComPtr<ID3D11Query>            timestampBegin;
        ComPtr<ID3D11Query>            timestampEnd;
        LUID                           adapterLuid{};

        bool Initialize(std::string& outLog)
        {
            // 디버그 레이어를 켜지 않는다. DX12 쪽도 Release에서 안 켠다 —
            // 한쪽만 검증 비용을 내면 측정이 그 자리에서 거짓이 된다.
            const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
            D3D_FEATURE_LEVEL got{};
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                0, wanted, 2, D3D11_SDK_VERSION, &device, &got, &context);
            if (FAILED(hr))
            {
                outLog += "DX11 디바이스 생성 실패\n";
                return false;
            }

            {
                ComPtr<IDXGIDevice> dxgiDevice;
                ComPtr<IDXGIAdapter> adapter;
                if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
                    FAILED(dxgiDevice->GetAdapter(&adapter)))
                {
                    outLog += "DX11 어댑터 조회 실패\n";
                    return false;
                }
                DXGI_ADAPTER_DESC desc{};
                adapter->GetDesc(&desc);
                adapterLuid = desc.AdapterLuid;

                char line[192]{};
                char name[128]{};
                ::WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                    name, sizeof(name), nullptr, nullptr);
                std::snprintf(line, sizeof(line), "어댑터: %s\n", name);
                outLog += line;
            }

            D3D11_TEXTURE2D_DESC rtDesc{};
            rtDesc.Width = kBenchWidth;
            rtDesc.Height = kBenchHeight;
            rtDesc.MipLevels = 1;
            rtDesc.ArraySize = 1;
            rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtDesc.SampleDesc.Count = 1;
            rtDesc.Usage = D3D11_USAGE_DEFAULT;
            rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(device->CreateTexture2D(&rtDesc, nullptr, &renderTarget)) ||
                FAILED(device->CreateRenderTargetView(renderTarget.Get(), nullptr, &rtv)))
            {
                outLog += "DX11 렌더 타깃 생성 실패\n";
                return false;
            }

            rtDesc.Usage = D3D11_USAGE_STAGING;
            rtDesc.BindFlags = 0;
            rtDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device->CreateTexture2D(&rtDesc, nullptr, &staging)))
            {
                outLog += "DX11 스테이징 생성 실패\n";
                return false;
            }

            const BenchVertex vertices[4] = {
                { -0.5f, -0.5f, 0.f }, { -0.5f, 0.5f, 0.f },
                {  0.5f,  0.5f, 0.f }, {  0.5f, -0.5f, 0.f },
            };
            const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.ByteWidth = sizeof(vertices);
            vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData{ vertices };
            if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer)))
            {
                outLog += "DX11 정점 버퍼 생성 실패\n";
                return false;
            }

            D3D11_BUFFER_DESC ibDesc{};
            ibDesc.ByteWidth = sizeof(indices);
            ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
            ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ibData{ indices };
            if (FAILED(device->CreateBuffer(&ibDesc, &ibData, &indexBuffer)))
            {
                outLog += "DX11 인덱스 버퍼 생성 실패\n";
                return false;
            }

            // 드로우마다 Map(DISCARD)으로 갱신한다 — SceneRenderer가 오브젝트마다
            // 하는 그 패턴이다. 이 갱신 경로가 DX11 드로우 비용의 본체다.
            D3D11_BUFFER_DESC cbDesc{};
            cbDesc.ByteWidth = sizeof(PerDraw);
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &constantBuffer)))
            {
                outLog += "DX11 상수 버퍼 생성 실패\n";
                return false;
            }

            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            if (!CompileBenchShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
            if (!CompileBenchShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

            if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(),
                    vsBlob->GetBufferSize(), nullptr, &vs)) ||
                FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(),
                    psBlob->GetBufferSize(), nullptr, &ps)))
            {
                outLog += "DX11 셰이더 생성 실패\n";
                return false;
            }

            const D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            if (FAILED(device->CreateInputLayout(layout, 1,
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout)))
            {
                outLog += "DX11 입력 레이아웃 생성 실패\n";
                return false;
            }

            // 컬링 없음 — DX12 PSO와 맞춘다. 감김 방향 차이가 커버리지 차이로
            // 둔갑하는 것을 원천에서 막는다.
            D3D11_RASTERIZER_DESC rasterDesc{};
            rasterDesc.FillMode = D3D11_FILL_SOLID;
            rasterDesc.CullMode = D3D11_CULL_NONE;
            rasterDesc.DepthClipEnable = TRUE;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &rasterizer)))
            {
                outLog += "DX11 래스터라이저 생성 실패\n";
                return false;
            }

            D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_TIMESTAMP_DISJOINT };
            if (FAILED(device->CreateQuery(&queryDesc, &disjointQuery)))
            {
                outLog += "DX11 disjoint 쿼리 생성 실패\n";
                return false;
            }
            queryDesc.Query = D3D11_QUERY_TIMESTAMP;
            if (FAILED(device->CreateQuery(&queryDesc, &timestampBegin)) ||
                FAILED(device->CreateQuery(&queryDesc, &timestampEnd)))
            {
                outLog += "DX11 타임스탬프 쿼리 생성 실패\n";
                return false;
            }
            return true;
        }

        // 한 프레임: 클리어 + 드로우 N건 + 제출 + GPU 완료 대기.
        // outValid가 거짓이면 disjoint(클록 변동)라 그 표본은 버려야 한다.
        bool RenderFrame(const std::vector<PerDraw>& payloads, uint32_t drawCount,
            Sample& outSample, bool& outValid, std::string& outLog)
        {
            outValid = true;
            Stopwatch watch;

            context->Begin(disjointQuery.Get());
            context->End(timestampBegin.Get());

            const float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
            context->ClearRenderTargetView(rtv.Get(), clearColor);

            watch.Start();

            // 상태 바인딩도 측정 구간에 넣는다 — DX12 쪽도 PSO/루트 바인딩을
            // 측정 구간에 넣는다. 프레임당 한 번이라 지배 항은 아니다.
            const UINT stride = sizeof(BenchVertex);
            const UINT offset = 0;
            ID3D11RenderTargetView* rtvs[] = { rtv.Get() };
            context->OMSetRenderTargets(1, rtvs, nullptr);
            D3D11_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(kBenchWidth), static_cast<float>(kBenchHeight), 0.f, 1.f };
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rasterizer.Get());
            context->IASetInputLayout(inputLayout.Get());
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
            context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->PSSetShader(ps.Get(), nullptr, 0);
            ID3D11Buffer* cbs[] = { constantBuffer.Get() };
            context->VSSetConstantBuffers(0, 1, cbs);

            for (uint32_t i = 0; i < drawCount; ++i)
            {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context->Map(constantBuffer.Get(), 0,
                    D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    outLog += "DX11 상수 버퍼 Map 실패\n";
                    return false;
                }
                memcpy(mapped.pData, &payloads[i], sizeof(PerDraw));
                context->Unmap(constantBuffer.Get(), 0);
                context->DrawIndexed(6, 0, 0);
            }
            outSample.recordMs = watch.ElapsedMs();

            context->End(timestampEnd.Get());
            context->End(disjointQuery.Get());

            watch.Start();
            context->Flush();
            outSample.submitMs = watch.ElapsedMs();

            // GPU 완료 대기 — disjoint 쿼리는 프레임의 마지막 명령 뒤에 End
            // 됐으므로 이것이 나오면 프레임이 끝난 것이다.
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
            while (S_FALSE == context->GetData(disjointQuery.Get(),
                &disjointData, sizeof(disjointData), 0))
            {
                ::Sleep(0);
            }
            if (disjointData.Disjoint)
            {
                outValid = false;   // 클록이 흔들린 프레임 — 표본에서 뺀다
                return true;
            }

            UINT64 beginTicks = 0;
            UINT64 endTicks = 0;
            while (S_FALSE == context->GetData(timestampBegin.Get(),
                &beginTicks, sizeof(beginTicks), 0)) { ::Sleep(0); }
            while (S_FALSE == context->GetData(timestampEnd.Get(),
                &endTicks, sizeof(endTicks), 0)) { ::Sleep(0); }

            outSample.gpuMs = static_cast<double>(endTicks - beginTicks)
                * 1000.0 / static_cast<double>(disjointData.Frequency);
            return true;
        }

        // 커버리지(클리어 색이 아닌 픽셀 수). 직전 RenderFrame 결과를 읽는다.
        bool ReadCoverage(uint32_t& outCoverage, std::string& outLog)
        {
            context->CopyResource(staging.Get(), renderTarget.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                outLog += "DX11 스테이징 Map 실패\n";
                return false;
            }
            outCoverage = 0;
            for (uint32_t y = 0; y < kBenchHeight; ++y)
            {
                const auto* row = static_cast<const uint8_t*>(mapped.pData)
                    + static_cast<size_t>(y) * mapped.RowPitch;
                for (uint32_t x = 0; x < kBenchWidth; ++x)
                {
                    if (row[x * 4 + 0] | row[x * 4 + 1] | row[x * 4 + 2]) ++outCoverage;
                }
            }
            context->Unmap(staging.Get(), 0);
            return true;
        }
    };

    // ── DX12 쪽 ──────────────────────────────────────────────────────────
    struct Dx12Bench
    {
        DX12DeviceResources* resources{ nullptr };
        DX12GpuProfiler* profiler{ nullptr };
        DX12CommandListPool* pool{ nullptr };

        ComPtr<ID3D12RootSignature>  rootSignature;
        ComPtr<ID3D12PipelineState>  pso;
        ComPtr<ID3D12Resource>       renderTarget;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        ComPtr<ID3D12Resource>       vertexBuffer;
        ComPtr<ID3D12Resource>       indexBuffer;
        ComPtr<ID3D12Resource>       readback;
        D3D12_VERTEX_BUFFER_VIEW     vbView{};
        D3D12_INDEX_BUFFER_VIEW      ibView{};
        uint32_t frameCounter{ 0 };

        static constexpr uint32_t kReadbackRowPitch = kBenchWidth * 4;   // 5120 — 256 정렬 만족
        static_assert(0 == (kReadbackRowPitch % 256), "리드백 행 피치는 256 정렬이어야 한다");

        bool Initialize(DX12DeviceResources& res, DX12GpuProfiler& prof,
            DX12CommandListPool& listPool, std::string& outLog)
        {
            resources = &res;
            profiler = &prof;
            pool = &listPool;
            ID3D12Device* device = res.GetDevice();

            // 루트 시그니처: 루트 CBV 하나(b0, VS 전용). Enhanced 패스들이 쓰는
            // 드로우당 상수 경로와 같다.
            {
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.Descriptor.ShaderRegister = 0;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

                D3D12_ROOT_SIGNATURE_DESC desc{};
                desc.NumParameters = 1;
                desc.pParameters = &param;
                desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                ComPtr<ID3DBlob> blob;
                ComPtr<ID3DBlob> errors;
                if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                        &blob, &errors)) ||
                    FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                        blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature))))
                {
                    outLog += "DX12 루트 시그니처 생성 실패\n";
                    return false;
                }
            }

            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            if (!CompileBenchShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
            if (!CompileBenchShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

            {
                const D3D12_INPUT_ELEMENT_DESC layout[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };

                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
                desc.pRootSignature = rootSignature.Get();
                desc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
                desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
                desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                desc.SampleMask = UINT_MAX;
                desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                desc.RasterizerState.DepthClipEnable = TRUE;
                desc.InputLayout = { layout, 1 };
                desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                desc.NumRenderTargets = 1;
                desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;

                if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso))))
                {
                    outLog += "DX12 PSO 생성 실패\n";
                    return false;
                }
            }

            // 전용 렌더 타깃. DeviceResources의 내장 타깃을 쓰지 않는 이유는
            // 상태 전이 시점을 이 파일이 전부 소유하기 위해서다 — RENDER_TARGET
            // 상태로 만들어 두고 검증 프레임에서만 잠깐 COPY_SOURCE로 오간다.
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = kBenchWidth;
                desc.Height = kBenchHeight;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                D3D12_CLEAR_VALUE clearValue{};
                clearValue.Format = desc.Format;
                clearValue.Color[3] = 1.f;

                if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                    &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                    IID_PPV_ARGS(&renderTarget))))
                {
                    outLog += "DX12 렌더 타깃 생성 실패\n";
                    return false;
                }

                D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heapDesc.NumDescriptors = 1;
                if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap))))
                {
                    outLog += "DX12 RTV 힙 생성 실패\n";
                    return false;
                }
                device->CreateRenderTargetView(renderTarget.Get(), nullptr,
                    rtvHeap->GetCPUDescriptorHandleForHeapStart());
            }

            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_READBACK;

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Width = static_cast<uint64_t>(kReadbackRowPitch) * kBenchHeight;
                desc.Height = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                    &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&readback))))
                {
                    outLog += "DX12 리드백 생성 실패\n";
                    return false;
                }
            }

            // 정점·인덱스는 DEFAULT 힙 — DX11의 IMMUTABLE과 같은 등급. 업로드
            // 힙에 두면 GPU가 매 드로우 PCIe 너머를 읽어 DX12만 불리해진다.
            return UploadGeometry(outLog);
        }

        bool UploadGeometry(std::string& outLog)
        {
            ID3D12Device* device = resources->GetDevice();

            const BenchVertex vertices[4] = {
                { -0.5f, -0.5f, 0.f }, { -0.5f, 0.5f, 0.f },
                {  0.5f,  0.5f, 0.f }, {  0.5f, -0.5f, 0.f },
            };
            const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            desc.Width = sizeof(vertices);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer))))
            {
                outLog += "DX12 정점 버퍼 생성 실패\n";
                return false;
            }
            desc.Width = sizeof(indices);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&indexBuffer))))
            {
                outLog += "DX12 인덱스 버퍼 생성 실패\n";
                return false;
            }

            std::string error;
            if (!resources->BeginFrame(error))
            {
                outLog += "기하 업로드 Begin 실패: " + error + "\n";
                return false;
            }
            ++frameCounter;

            auto* commandList = resources->GetCommandList();
            auto& ring = resources->GetUploadRing();

            const auto vbStaging = ring.Allocate(sizeof(vertices), 4);
            const auto ibStaging = ring.Allocate(sizeof(indices), 4);
            if (!vbStaging.IsValid() || !ibStaging.IsValid())
            {
                outLog += "기하 업로드 링 할당 실패\n";
                return false;
            }
            memcpy(vbStaging.cpuAddress, vertices, sizeof(vertices));
            memcpy(ibStaging.cpuAddress, indices, sizeof(indices));
            commandList->CopyBufferRegion(vertexBuffer.Get(), 0,
                vbStaging.resource, vbStaging.offset, sizeof(vertices));
            commandList->CopyBufferRegion(indexBuffer.Get(), 0,
                ibStaging.resource, ibStaging.offset, sizeof(indices));

            D3D12_RESOURCE_BARRIER barriers[2]{};
            barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource = vertexBuffer.Get();
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[1] = barriers[0];
            barriers[1].Transition.pResource = indexBuffer.Get();
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
            commandList->ResourceBarrier(2, barriers);

            if (!resources->EndFrame(error))
            {
                outLog += "기하 업로드 End 실패: " + error + "\n";
                return false;
            }
            resources->WaitForGpu();

            vbView = { vertexBuffer->GetGPUVirtualAddress(), sizeof(vertices), sizeof(BenchVertex) };
            ibView = { indexBuffer->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R32_UINT };
            return true;
        }

        void BindState(ID3D12GraphicsCommandList* commandList) const
        {
            commandList->SetGraphicsRootSignature(rootSignature.Get());
            commandList->SetPipelineState(pso.Get());
            D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(kBenchWidth), static_cast<float>(kBenchHeight), 0.f, 1.f };
            D3D12_RECT scissor{ 0, 0, kBenchWidth, kBenchHeight };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
            const auto rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 1, &vbView);
            commandList->IASetIndexBuffer(&ibView);
        }

        void RecordDraws(ID3D12GraphicsCommandList* commandList,
            const std::vector<PerDraw>& payloads, uint32_t begin, uint32_t end)
        {
            auto& ring = resources->GetUploadRing();
            for (uint32_t i = begin; i < end; ++i)
            {
                const auto allocation = ring.Allocate(sizeof(PerDraw),
                    DX12UploadRing::kConstantBufferAlignment);
                if (!allocation.IsValid()) return;   // 넘침은 호출부가 통계로 잡는다
                memcpy(allocation.cpuAddress, &payloads[i], sizeof(PerDraw));
                commandList->SetGraphicsRootConstantBufferView(0, allocation.gpuAddress);
                commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
            }
        }

        // 순차 기록 한 프레임.
        bool RenderFrameSequential(const std::vector<PerDraw>& payloads,
            uint32_t drawCount, Sample& outSample, std::string& outLog)
        {
            std::string error;
            if (!resources->BeginFrame(error))
            {
                outLog += "DX12 Begin 실패: " + error + "\n";
                return false;
            }
            profiler->BeginFrame(frameCounter % DX12DeviceResources::kFrameCount);
            ++frameCounter;

            auto* commandList = resources->GetCommandList();
            Stopwatch watch;
            watch.Start();

            const uint32_t slot = profiler->BeginPass(commandList, "Seq");
            const float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
            commandList->ClearRenderTargetView(
                rtvHeap->GetCPUDescriptorHandleForHeapStart(), clearColor, 0, nullptr);
            BindState(commandList);
            RecordDraws(commandList, payloads, 0, drawCount);
            profiler->EndPass(commandList, slot);
            profiler->ResolveFrame(commandList);

            outSample.recordMs = watch.ElapsedMs();

            watch.Start();
            if (!resources->EndFrame(error))
            {
                outLog += "DX12 End 실패: " + error + "\n";
                return false;
            }
            outSample.submitMs = watch.ElapsedMs();

            resources->WaitForGpu();

            std::vector<DX12GpuProfiler::PassTiming> timings;
            if (!profiler->Collect(timings, error))
            {
                outLog += "DX12 타이밍 수집 실패: " + error + "\n";
                return false;
            }
            outSample.gpuMs = profiler->GetLastTotalMilliseconds();
            return true;
        }

        // 병렬 기록 한 프레임(워커 kParallelWorkers).
        //
        // 순서: 메인 리스트(클리어)를 먼저 제출(FlushCommandList) → 워커들이
        // 병렬 기록 → 워커 리스트를 워커 번호 순으로 제출 → 다시 열린 메인
        // 리스트에 Resolve를 실어 EndFrame. 큐가 제출 순서를 지키므로 클리어 →
        // 드로우 → 타임스탬프 해석 순서가 보존된다.
        bool RenderFrameParallel(const std::vector<PerDraw>& payloads,
            uint32_t drawCount, Sample& outSample, std::string& outLog)
        {
            std::string error;
            if (!resources->BeginFrame(error))
            {
                outLog += "DX12 병렬 Begin 실패: " + error + "\n";
                return false;
            }
            const uint32_t frameSlot = frameCounter % DX12DeviceResources::kFrameCount;
            profiler->BeginFrame(frameSlot);
            pool->BeginFrame(frameSlot);
            ++frameCounter;

            Stopwatch watch;
            watch.Start();

            auto* mainList = resources->GetCommandList();
            const float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
            mainList->ClearRenderTargetView(
                rtvHeap->GetCPUDescriptorHandleForHeapStart(), clearColor, 0, nullptr);
            if (!resources->FlushCommandList(error))
            {
                outLog += "DX12 병렬 중간 제출 실패: " + error + "\n";
                return false;
            }

            const uint32_t perWorker = (drawCount + kParallelWorkers - 1) / kParallelWorkers;
            std::string workerErrors[kParallelWorkers];

            pool->RunParallel([&](uint32_t worker)
            {
                ID3D12GraphicsCommandList* list = pool->Open(worker, workerErrors[worker]);
                if (nullptr == list) return;

                const uint32_t begin = worker * perWorker;
                const uint32_t end = std::min(drawCount, begin + perWorker);
                if (begin >= end) return;

                const uint32_t slot = profiler->BeginPass(list,
                    "W" + std::to_string(worker));
                BindState(list);
                RecordDraws(list, payloads, begin, end);
                profiler->EndPass(list, slot);
            }, kParallelWorkers);

            if (!pool->CloseAll(error))
            {
                outLog += "DX12 병렬 CloseAll 실패: " + error + "\n";
                return false;
            }
            outSample.recordMs = watch.ElapsedMs();

            for (const std::string& workerError : workerErrors)
            {
                if (!workerError.empty())
                {
                    outLog += "DX12 워커 실패: " + workerError + "\n";
                    return false;
                }
            }

            watch.Start();
            ID3D12CommandList* lists[kParallelWorkers]{};
            uint32_t listCount = 0;
            for (uint32_t worker = 0; worker < kParallelWorkers; ++worker)
            {
                if (pool->HasRecorded(worker)) lists[listCount++] = pool->Get(worker);
            }
            if (0 != listCount)
            {
                resources->GetCommandQueue()->ExecuteCommandLists(listCount, lists);
            }

            profiler->ResolveFrame(resources->GetCommandList());
            if (!resources->EndFrame(error))
            {
                outLog += "DX12 병렬 End 실패: " + error + "\n";
                return false;
            }
            outSample.submitMs = watch.ElapsedMs();

            resources->WaitForGpu();

            std::vector<DX12GpuProfiler::PassTiming> timings;
            if (!profiler->Collect(timings, error))
            {
                outLog += "DX12 병렬 타이밍 수집 실패: " + error + "\n";
                return false;
            }
            outSample.gpuMs = profiler->GetLastTotalMilliseconds();
            return true;
        }

        // 커버리지 검증 프레임(측정 밖). 직전 프레임의 타깃을 리드백으로 복사한다.
        bool ReadCoverage(uint32_t& outCoverage, std::string& outLog)
        {
            std::string error;
            if (!resources->BeginFrame(error))
            {
                outLog += "DX12 검증 Begin 실패: " + error + "\n";
                return false;
            }
            ++frameCounter;

            auto* commandList = resources->GetCommandList();

            D3D12_RESOURCE_BARRIER toCopy{};
            toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopy.Transition.pResource = renderTarget.Get();
            toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &toCopy);

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = renderTarget.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = kBenchWidth;
            dst.PlacedFootprint.Footprint.Height = kBenchHeight;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = kReadbackRowPitch;
            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER back = toCopy;
            back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            back.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList->ResourceBarrier(1, &back);

            if (!resources->EndFrame(error))
            {
                outLog += "DX12 검증 End 실패: " + error + "\n";
                return false;
            }
            resources->WaitForGpu();

            void* mapped = nullptr;
            if (FAILED(readback->Map(0, nullptr, &mapped)))
            {
                outLog += "DX12 리드백 Map 실패\n";
                return false;
            }
            outCoverage = 0;
            for (uint32_t y = 0; y < kBenchHeight; ++y)
            {
                const auto* row = static_cast<const uint8_t*>(mapped)
                    + static_cast<size_t>(y) * kReadbackRowPitch;
                for (uint32_t x = 0; x < kBenchWidth; ++x)
                {
                    if (row[x * 4 + 0] | row[x * 4 + 1] | row[x * 4 + 2]) ++outCoverage;
                }
            }
            readback->Unmap(0, nullptr);
            return true;
        }
    };
}

bool EnhancedSceneRenderer::RunApiOverheadBench(std::string& outLog)
{
    outLog += "── DX11 vs DX12 API 오버헤드 실측 (마이그레이션 전제 검증) ──\n";
#if defined(_DEBUG)
    // 디버그 레이어는 DX12에만 있어 한쪽만 비용을 낸다. 그 수는 전제에 대해
    // 아무것도 말해 주지 않으므로 재지 않는 쪽이 낫다.
    outLog += "Debug 빌드 — DX12 검증 레이어가 켜져 비교가 성립하지 않는다. "
              "Release로 재야 한다\n";
    return false;
#else
    bool passed = true;

    Dx11Bench dx11;
    if (!dx11.Initialize(outLog)) return false;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kBenchWidth, kBenchHeight, error, dx11.adapterLuid))
    {
        outLog += "DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12GpuProfiler profiler;
    DX12CommandListPool pool;
    if (!profiler.Initialize(resources.GetDevice(), resources.GetCommandQueue(),
            16, DX12DeviceResources::kFrameCount, error) ||
        !pool.Initialize(resources.GetDevice(), kParallelWorkers,
            DX12DeviceResources::kFrameCount, error))
    {
        outLog += "DX12 프로파일러/풀 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    Dx12Bench dx12;
    if (!dx12.Initialize(resources, profiler, pool, outLog))
    {
        pool.Shutdown();
        profiler.Shutdown();
        resources.Shutdown();
        return false;
    }

    const std::vector<PerDraw> payloads = MakePayloads();

    struct ResultRow
    {
        uint32_t drawCount{ 0 };
        Sample dx11Median{};
        Sample dx12SeqMedian{};
        Sample dx12ParMedian{};
    };
    std::vector<ResultRow> rows;

    for (const uint32_t drawCount : kDrawCounts)
    {
        if (!passed) break;

        ResultRow row{};
        row.drawCount = drawCount;

        // ── DX11 ──
        {
            std::vector<double> record;
            std::vector<double> submit;
            std::vector<double> gpu;
            for (uint32_t i = 0; i < kBenchWarmupFrames + kBenchMeasureFrames && passed; ++i)
            {
                Sample sample{};
                bool valid = true;
                if (!dx11.RenderFrame(payloads, drawCount, sample, valid, outLog))
                {
                    passed = false;
                    break;
                }
                if (i >= kBenchWarmupFrames && valid)
                {
                    record.push_back(sample.recordMs);
                    submit.push_back(sample.submitMs);
                    gpu.push_back(sample.gpuMs);
                }
            }
            if (!passed) break;
            if (record.empty())
            {
                outLog += "DX11 유효 표본 0건(disjoint 연발) — 측정 불능\n";
                passed = false;
                break;
            }
            row.dx11Median.recordMs = BenchMedian(record);
            row.dx11Median.submitMs = BenchMedian(submit);
            row.dx11Median.gpuMs = BenchMedian(gpu);
            if (!dx11.ReadCoverage(row.dx11Median.coverage, outLog))
            {
                passed = false;
                break;
            }
        }

        // ── DX12 순차 ──
        {
            std::vector<double> record;
            std::vector<double> submit;
            std::vector<double> gpu;
            for (uint32_t i = 0; i < kBenchWarmupFrames + kBenchMeasureFrames && passed; ++i)
            {
                Sample sample{};
                if (!dx12.RenderFrameSequential(payloads, drawCount, sample, outLog))
                {
                    passed = false;
                    break;
                }
                if (i >= kBenchWarmupFrames)
                {
                    record.push_back(sample.recordMs);
                    submit.push_back(sample.submitMs);
                    gpu.push_back(sample.gpuMs);
                }
            }
            if (!passed) break;
            row.dx12SeqMedian.recordMs = BenchMedian(record);
            row.dx12SeqMedian.submitMs = BenchMedian(submit);
            row.dx12SeqMedian.gpuMs = BenchMedian(gpu);
            if (!dx12.ReadCoverage(row.dx12SeqMedian.coverage, outLog))
            {
                passed = false;
                break;
            }
        }

        // ── DX12 병렬 ──
        {
            std::vector<double> record;
            std::vector<double> submit;
            std::vector<double> gpu;
            for (uint32_t i = 0; i < kBenchWarmupFrames + kBenchMeasureFrames && passed; ++i)
            {
                Sample sample{};
                if (!dx12.RenderFrameParallel(payloads, drawCount, sample, outLog))
                {
                    passed = false;
                    break;
                }
                if (i >= kBenchWarmupFrames)
                {
                    record.push_back(sample.recordMs);
                    submit.push_back(sample.submitMs);
                    gpu.push_back(sample.gpuMs);
                }
            }
            if (!passed) break;
            row.dx12ParMedian.recordMs = BenchMedian(record);
            row.dx12ParMedian.submitMs = BenchMedian(submit);
            row.dx12ParMedian.gpuMs = BenchMedian(gpu);
            if (!dx12.ReadCoverage(row.dx12ParMedian.coverage, outLog))
            {
                passed = false;
                break;
            }
        }

        // ── 커버리지 대조 — '빨라진 것'과 '덜 그린 것'을 가르는 자물쇠 ──
        //
        // 세 경로가 같은 픽셀 수를 그렸어야 시간 비교가 성립한다. 하나라도
        // 어긋나면 그 시간은 다른 일을 한 시간이다.
        if (row.dx11Median.coverage != row.dx12SeqMedian.coverage ||
            row.dx11Median.coverage != row.dx12ParMedian.coverage ||
            0 == row.dx11Median.coverage)
        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "커버리지 불일치 — DX11 %u · DX12 순차 %u · DX12 병렬 %u "
                "(드로우 %u): 시간 비교가 성립하지 않는다\n",
                row.dx11Median.coverage, row.dx12SeqMedian.coverage,
                row.dx12ParMedian.coverage, row.drawCount);
            outLog += line;
            passed = false;
            break;
        }

        rows.push_back(row);

        char header[64]{};
        std::snprintf(header, sizeof(header),
            "드로우 %5u (커버리지 %u px)\n", row.drawCount, row.dx11Median.coverage);
        outLog += header;

        const auto appendLine = [&outLog](const char* name, const Sample& sample,
            double baselineCpu)
        {
            const double totalCpu = sample.recordMs + sample.submitMs;
            char line[224]{};
            if (baselineCpu > 0.0)
            {
                std::snprintf(line, sizeof(line),
                    "  %-9s 기록 %7.3f + 제출 %7.3f = CPU %7.3f ms (%4.2f배) · GPU %7.3f ms\n",
                    name, sample.recordMs, sample.submitMs, totalCpu,
                    baselineCpu / totalCpu, sample.gpuMs);
            }
            else
            {
                std::snprintf(line, sizeof(line),
                    "  %-9s 기록 %7.3f + 제출 %7.3f = CPU %7.3f ms · GPU %7.3f ms\n",
                    name, sample.recordMs, sample.submitMs, totalCpu, sample.gpuMs);
            }
            outLog += line;
        };

        const double dx11Cpu = row.dx11Median.recordMs + row.dx11Median.submitMs;
        appendLine("DX11", row.dx11Median, 0.0);
        appendLine("DX12 순차", row.dx12SeqMedian, dx11Cpu);
        appendLine("DX12 병렬", row.dx12ParMedian, dx11Cpu);
    }

    // ── 전제 판정 ──
    if (passed && !rows.empty())
    {
        outLog += "── 전제 판정 ──\n";

        // CPU 판정은 '그 지점부터 끝까지 계속 이긴다'로만 적는다.
        //
        // 첫 승리 지점만 찾으면 거짓말이 된다 — 작은 N에서 고정비 차이로 한 번
        // 이기고 큰 N에서 뒤집히는 경우, "N건부터 싸다"는 문장이 데이터와
        // 정반대를 말하게 된다(실측으로 그렇게 됐다).
        const auto suffixCrossover = [&rows](const auto& pickDx12Cpu) -> uint32_t
        {
            uint32_t crossover = 0;
            for (const ResultRow& row : rows)
            {
                const double dx11Cpu = row.dx11Median.recordMs + row.dx11Median.submitMs;
                if (pickDx12Cpu(row) < dx11Cpu)
                {
                    if (0 == crossover) crossover = row.drawCount;
                }
                else
                {
                    crossover = 0;   // 뒤집혔다 — 앞의 승리는 지속되지 않는다
                }
            }
            return crossover;
        };
        const uint32_t seqCrossover = suffixCrossover([](const ResultRow& row)
            { return row.dx12SeqMedian.recordMs + row.dx12SeqMedian.submitMs; });
        const uint32_t parCrossover = suffixCrossover([](const ResultRow& row)
            { return row.dx12ParMedian.recordMs + row.dx12ParMedian.submitMs; });

        if (0 != seqCrossover)
        {
            outLog += "CPU(순차): 드로우 " + std::to_string(seqCrossover)
                + "건부터 끝까지 DX12가 DX11보다 싸다\n";
        }
        else
        {
            outLog += "CPU(순차): DX12 순차 기록이 DX11을 지속적으로 이기지 못했다"
                      "(위 표의 배율 참조)\n";
        }
        if (0 != parCrossover)
        {
            outLog += "CPU(병렬): 드로우 " + std::to_string(parCrossover)
                + "건부터 끝까지 DX12 병렬 기록(워커 "
                + std::to_string(kParallelWorkers) + ")이 DX11보다 싸다\n";
        }
        else
        {
            outLog += "CPU(병렬): DX12 병렬 기록이 DX11을 지속적으로 이기지 못했다"
                      "(위 표의 배율 참조)\n";
        }

        const ResultRow& last = rows.back();
        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "GPU(드로우 %u): DX11 %.3f ms · DX12 %.3f ms — GPU는 같은 하드웨어라 "
                "큰 차이가 없어야 정상이고, 전제의 본체는 CPU 쪽 수다\n",
                last.drawCount, last.dx11Median.gpuMs, last.dx12SeqMedian.gpuMs);
            outLog += line;
        }

        outLog += "※ 이 워크로드는 상태 변경 없는 최소 드로우라 DX11에 유리한 "
                  "하한이다. 실제 씬(재질·텍스처 전환)에서는 DX11 드라이버의 "
                  "상태 추적 비용이 더 붙는다\n";
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    pool.Shutdown();
    profiler.Shutdown();
    resources.Shutdown();

    outLog += passed ? "API 오버헤드 실측 완료\n" : "API 오버헤드 실측 실패\n";
    return passed;
#endif
}

#endif
