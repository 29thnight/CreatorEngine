#ifndef DYNAMICCPP_EXPORTS
#include "../../../../Render/Scene/EnhancedSceneRenderer.h"
#include "../../DX12DeviceResources.h"
#include "../../DX12Encoder.h"   // A-4 — 경로 ④ 가 실물 인코더를 지난다

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// 인코더 오버헤드 실측 (PHASE 3-1 재정의, R3 착수 조건).
//
// ── 왜 R3 앞에 이것이 있는가 ──
//
// 계획서 §5가 R3를 시작하기 전 조건으로 못박아 둔 것이다. 인코더 호출은
// 프레임당 수백~수천이고, 그 자리를 가상 함수로 바꾸면 호출마다 vtable을
// 한 번 더 지난다. 그것이 재는 값에 나타나는지를 먼저 알아야 "드로우 루프
// 안쪽만 비가상으로 둔다"는 판단을 근거 있게 할 수 있다.
//
// 재지 않고 넘어가면 두 가지 중 하나가 된다: 필요 없는 최적화를 미리 해서
// 인터페이스를 비틀거나, 실제로 비싼데 모른 채 17종을 다 옮긴 뒤에 발견한다.
//
// ── 무엇을 재는가 ──
//
// 같은 커맨드 기록을 세 경로로 돌리고 CPU 기록 시간만 잰다:
//
//   Direct   commandList->X()          원시 경로 — 나머지의 기준선
//   Virtual  가상 인터페이스 경유       vtable 한 번의 값
//   Inline   비가상 래퍼 경유           대안(호출부 모양은 같고 dispatch만 정적)
//   Real     DX12Encoder + 핸들         ★ 지금 패스가 실제로 하는 것 (A-4)
//
// 넷의 기록 내용은 바이트 단위로 같다. 다른 것은 호출이 어디를 거치는가뿐이다.
//
// ── ★ 경로 ④ 가 A-4 에서 생긴 이유 ──
//
// 앞의 셋은 **모형**이다 — 셋 다 `D3D12_VERTEX_BUFFER_VIEW` 를 손에 들고
// 있으므로 재는 것이 "dispatch 가 얼마인가" 하나뿐이고, 실물 `DX12Encoder`
// 는 한 번도 지나지 않는다. 그래서 A-5a 도 이 벤치로는 자기 변경을 못
// 쟀다(업로드 슬라이스로 바꾼 것이 여기 안 나타난다).
//
// A-4 가 메시 캐시를 `RHIMeshBinding` 으로 바꾸면서 그 간극이 답해야 할
// 질문이 됐다: 정점·인덱스를 **핸들**로 받으면 인코더가 드로우마다 표를
// 두 번 뒤진다(`ResolveSlice`). 드로우 루프 안이라 A-5a 때와 자리가 다르다.
// 그래서 실물 경로를 넷째로 둔다 — 이 벤치가 재려던 것이 원래 그것이다.
//
// ★ 제출하지 않는다. GPU 시간은 이 질문과 무관하고, 섞이면 잡음만 는다.
//   재는 것은 "CPU가 커맨드를 적어 넣는 데 드는 시간"이다.
//
// ── ★ Release 전용이다 ──
//
// Debug에서는 commandList->X() 한 번이 D3D12 검증 레이어를 지난다. 그 비용이
// vtable 한 번(수 나노초)을 통째로 덮어서, Debug로 재면 어느 경로든 같은
// 수가 나온다 — "가상 호출은 공짜"라는 거짓 음성이다. 그 답을 믿고 R3를
// 진행하면 Release에서 비싼 것을 못 보고 지나친다.
//
// bench11이 Release 전용인 이유(DX11/DX12 비교에서 검증 레이어가 한쪽에만
// 붙는다)와는 다른 이유이고, 이쪽이 더 강하다.

namespace
{
    using Microsoft::WRL::ComPtr;

    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr uint32_t kEncBenchDrawCounts[] = { 256, 1024, 4096, 16384 };

    // 반복 횟수. 중앙값을 쓰므로 홀수로 둔다.
    constexpr uint32_t kEncBenchRepeats = 9;

    // 드로우 하나가 거는 호출 수. 실제 패스에서 세어 온 값이다 —
    // GBuffer·Forward+의 드로우 루프가 상수 · 테이블 · 정점 · 인덱스 · 드로우
    // 다섯을 건다. 이 수가 곧 vtable을 지나는 횟수다.
    constexpr uint32_t kEncBenchCallsPerDraw = 5;

    struct EncBenchStopwatch
    {
        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};

        EncBenchStopwatch() { ::QueryPerformanceFrequency(&frequency); }
        void Start() { ::QueryPerformanceCounter(&started); }
        double ElapsedMs() const
        {
            LARGE_INTEGER now{};
            ::QueryPerformanceCounter(&now);
            return (now.QuadPart - started.QuadPart) * 1000.0
                / static_cast<double>(frequency.QuadPart);
        }
    };

    // 한 드로우가 거는 것. 네 경로가 **같은 리소스**를 가리킨다 — 원시 셋은
    // 주소·뷰로, 실물은 핸들로. 가리키는 대상이 같아야 비교가 성립한다.
    struct EncBenchDraw
    {
        D3D12_GPU_VIRTUAL_ADDRESS   constants{};
        D3D12_GPU_DESCRIPTOR_HANDLE table{};
        D3D12_VERTEX_BUFFER_VIEW    vertexView{};
        D3D12_INDEX_BUFFER_VIEW     indexView{};
        uint32_t                    indexCount{ 0 };

        // ── 경로 ④ 가 쓰는 중립 인자 (A-4) ──
        RHIBufferSlice   constantSlice;
        RHIBindingTable  bindingTable;
        RHIBufferSlice   vertexSlice;
        RHIBufferSlice   indexSlice;
        uint32_t         vertexStride{ 0 };
    };

    // ── 경로 ④ 실물 ──
    //
    // `DX12Encoder` 를 `RHIEncoder&` 로 지난다. 앞의 셋과 다른 점은 vtable 이
    // 아니라 **핸들 해소**다 — 상수·정점·인덱스 셋이 각각 표를 한 번 뒤진다.
    inline void EncBenchRecordReal(RHIEncoder& encoder, const EncBenchDraw& draw)
    {
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constantSlice);
        encoder.SetBindings(RHIBindPoint::Graphics, 1, draw.bindingTable);
        encoder.SetVertexBuffer(draw.vertexSlice, draw.vertexStride);
        encoder.SetIndexBuffer(draw.indexSlice, RHIFormat::R32Uint);
        encoder.DrawIndexed(draw.indexCount, 1);
    }

    // ── 경로 ① 직접 ──
    //
    // 지금 패스가 하는 것 그대로. 나머지 둘의 기준선이다.
    inline void EncBenchRecordDirect(ID3D12GraphicsCommandList* commandList,
        const EncBenchDraw& draw)
    {
        commandList->SetGraphicsRootConstantBufferView(0, draw.constants);
        commandList->SetGraphicsRootDescriptorTable(1, draw.table);
        commandList->IASetVertexBuffers(0, 1, &draw.vertexView);
        commandList->IASetIndexBuffer(&draw.indexView);
        commandList->DrawIndexedInstanced(draw.indexCount, 1, 0, 0, 0);
    }

    // ── 경로 ② 가상 인터페이스 ──
    //
    // R3가 만들려는 모양이다. 메서드마다 vtable을 한 번 지난다.
    class EncBenchEncoder
    {
    public:
        virtual ~EncBenchEncoder() = default;

        virtual void SetConstants(uint32_t slot, D3D12_GPU_VIRTUAL_ADDRESS address) = 0;
        virtual void SetBindings(uint32_t slot, D3D12_GPU_DESCRIPTOR_HANDLE table) = 0;
        virtual void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view) = 0;
        virtual void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) = 0;
        virtual void DrawIndexed(uint32_t indexCount) = 0;
    };

    class EncBenchDx12Encoder final : public EncBenchEncoder
    {
    public:
        explicit EncBenchDx12Encoder(ID3D12GraphicsCommandList* commandList)
            : m_commandList(commandList) {}

        void SetConstants(uint32_t slot, D3D12_GPU_VIRTUAL_ADDRESS address) override
        {
            m_commandList->SetGraphicsRootConstantBufferView(slot, address);
        }
        void SetBindings(uint32_t slot, D3D12_GPU_DESCRIPTOR_HANDLE table) override
        {
            m_commandList->SetGraphicsRootDescriptorTable(slot, table);
        }
        void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view) override
        {
            m_commandList->IASetVertexBuffers(0, 1, &view);
        }
        void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) override
        {
            m_commandList->IASetIndexBuffer(&view);
        }
        void DrawIndexed(uint32_t indexCount) override
        {
            m_commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
        }

    private:
        ID3D12GraphicsCommandList* m_commandList{ nullptr };
    };

    // ── 경로 ③ 비가상 래퍼 ──
    //
    // 호출부가 보는 모양은 ②와 같고 dispatch만 정적이다. ①과 같은 수가
    // 나와야 정상이고, 아니면 래퍼 자체가 비용이라는 뜻이다.
    struct EncBenchInlineEncoder
    {
        ID3D12GraphicsCommandList* commandList{ nullptr };

        void SetConstants(uint32_t slot, D3D12_GPU_VIRTUAL_ADDRESS address)
        {
            commandList->SetGraphicsRootConstantBufferView(slot, address);
        }
        void SetBindings(uint32_t slot, D3D12_GPU_DESCRIPTOR_HANDLE table)
        {
            commandList->SetGraphicsRootDescriptorTable(slot, table);
        }
        void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view)
        {
            commandList->IASetVertexBuffers(0, 1, &view);
        }
        void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view)
        {
            commandList->IASetIndexBuffer(&view);
        }
        void DrawIndexed(uint32_t indexCount)
        {
            commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
        }
    };

    template <typename EncoderT>
    inline void EncBenchRecordThrough(EncoderT& encoder, const EncBenchDraw& draw)
    {
        encoder.SetConstants(0, draw.constants);
        encoder.SetBindings(1, draw.table);
        encoder.SetVertexBuffer(draw.vertexView);
        encoder.SetIndexBuffer(draw.indexView);
        encoder.DrawIndexed(draw.indexCount);
    }

    double EncBenchMedian(std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    // ★ 루트 시그니처가 반드시 걸려 있어야 한다.
    //
    //   SetGraphicsRootConstantBufferView·SetGraphicsRootDescriptorTable은
    //   드라이버가 루트 레이아웃을 봐야 슬롯을 어디에 적을지 안다. 안 걸고
    //   부르면 Debug에서는 검증 레이어가 경고만 남기고 넘어가지만 Release는
    //   그 자리에서 죽는다 — 이 벤치를 처음 돌렸을 때 실제로 그렇게 죽었고,
    //   Debug로만 확인했으면 못 봤을 부류다(이 벤치가 Release 전용인 이유와
    //   같은 뿌리다).
    //
    //   거는 것은 측정 구간 밖에서 한다. 재려는 것은 드로우 루프의 비용이지
    //   준비 비용이 아니다.
    bool EncBenchCreateRootSignature(ID3D12Device* device,
        ComPtr<ID3D12RootSignature>& outSignature, std::string& outError)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob, errors;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &blob, &errors)))
        {
            outError = "루트 시그니처 직렬화 실패";
            if (errors) outError += std::string(": ")
                + static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
            blob->GetBufferSize(), IID_PPV_ARGS(&outSignature))))
        {
            outError = "루트 시그니처 생성 실패";
            return false;
        }
        return true;
    }
}

bool EnhancedSceneRenderer::RunEncoderOverheadBench(std::string& outLog)
{
    outLog += "── 인코더 오버헤드 실측 (R3 착수 조건) ──\n";

#ifdef _DEBUG
    outLog += "★ Debug 빌드다 — 이 수는 판단 근거가 되지 못한다.\n"
              "  commandList 호출 하나가 검증 레이어를 지나므로 그 비용이\n"
              "  vtable 한 번을 통째로 덮는다. Release로 다시 잴 것.\n";
#endif

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    // 기록만 하고 제출하지 않으므로 리소스는 주소만 있으면 된다.
    // 실제 값을 읽는 것은 드라이버가 아니라 커맨드 버퍼다.
    RHIBufferDesc bufferDesc{};
    bufferDesc.bytes = 64 * 1024;
    bufferDesc.debugName = L"EncoderBench.Dummy";

    RHIBufferHandle dummy{};
    if (!resources.CreateBuffer(bufferDesc, dummy, error))
    {
        outLog += "더미 버퍼 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    if (!EncBenchCreateRootSignature(resources.GetDevice(), rootSignature, error))
    {
        outLog += error + "\n";
        resources.Shutdown();
        return false;
    }

    EncBenchDraw draw{};
    draw.constants = resources.Resolve(dummy)->GetGPUVirtualAddress();
    draw.table = resources.GetDescriptorRing().GetHeap()
        ->GetGPUDescriptorHandleForHeapStart();
    draw.vertexView.BufferLocation = resources.Resolve(dummy)->GetGPUVirtualAddress();
    draw.vertexView.SizeInBytes = 1024;
    draw.vertexView.StrideInBytes = 32;
    draw.indexView.BufferLocation = resources.Resolve(dummy)->GetGPUVirtualAddress();
    draw.indexView.SizeInBytes = 1024;
    draw.indexView.Format = DXGI_FORMAT_R32_UINT;
    draw.indexCount = 3;

    // 경로 ④ — 같은 더미 버퍼를 핸들로 가리킨다 (A-4).
    draw.constantSlice = RHIBufferSlice::Whole(dummy);
    draw.vertexSlice = RHIBufferSlice::Whole(dummy);
    draw.vertexSlice.size = 1024;
    draw.indexSlice = RHIBufferSlice::Whole(dummy);
    draw.indexSlice.size = 1024;
    draw.vertexStride = 32;
    draw.bindingTable = RHIBindingTable{ draw.table.ptr, 1 };

    bool passed = true;

    char header[192]{};
    std::snprintf(header, sizeof(header),
        "드로우당 호출 %u · 반복 %u회(중앙값) · 기록만 하고 제출하지 않는다\n",
        kEncBenchCallsPerDraw, kEncBenchRepeats);
    outLog += header;
    outLog += "  드로우      직접      가상      인라인     실물     가상-직접   실물-직접  호출당(ns)\n";

    for (uint32_t drawCount : kEncBenchDrawCounts)
    {
        std::vector<double> directMs, virtualMs, inlineMs, realMs;
        directMs.reserve(kEncBenchRepeats);
        virtualMs.reserve(kEncBenchRepeats);
        inlineMs.reserve(kEncBenchRepeats);
        realMs.reserve(kEncBenchRepeats);

        for (uint32_t repeat = 0; repeat < kEncBenchRepeats; ++repeat)
        {
            EncBenchStopwatch watch;

            // ★ 반복마다 순서를 돌린다.
            //
            //   처음에는 늘 직접 → 가상 → 인라인 순으로 돌렸는데, 그러면 첫
            //   번째가 캐시·페이지 워밍업을 뒤집어쓴다. 실제로 그 판이
            //   "인라인이 직접보다 13% 빠르다"는 말이 안 되는 수를 냈다 —
            //   둘은 같은 코드로 컴파일되므로 그럴 수가 없다. 그 이상이
            //   측정 방법의 결함을 알려 준 셈이고, 그것을 못 봤으면
            //   "가상-직접" 차이도 과소평가된 채로 판단했을 것이다.
            //
            //   넷을 회전시키면 워밍업 비용이 네 경로에 고르게 흩어진다.
            const uint32_t order = repeat % 4;

            // 한 경로를 한 프레임에 기록한다. 프레임을 나누는 이유는 커맨드
            // 리스트를 매번 새로 시작해야 앞 경로가 남긴 상태가 안 섞이기
            // 때문이다.
            const auto runPath = [&](uint32_t which) -> bool
            {
                if (!resources.BeginFrame(error))
                {
                    outLog += "BeginFrame 실패: " + error + "\n";
                    return false;
                }

                auto* commandList = resources.GetCommandList();
                commandList->SetGraphicsRootSignature(rootSignature.Get());
                resources.BindDescriptorHeaps(commandList);

                if (0 == which)
                {
                    watch.Start();
                    for (uint32_t i = 0; i < drawCount; ++i) EncBenchRecordDirect(commandList, draw);
                    directMs.push_back(watch.ElapsedMs());
                }
                else if (1 == which)
                {
                    EncBenchDx12Encoder encoder(commandList);
                    EncBenchEncoder& asInterface = encoder;   // vtable을 반드시 지나게 한다
                    watch.Start();
                    for (uint32_t i = 0; i < drawCount; ++i) EncBenchRecordThrough(asInterface, draw);
                    virtualMs.push_back(watch.ElapsedMs());
                }
                else if (2 == which)
                {
                    EncBenchInlineEncoder encoder{ commandList };
                    watch.Start();
                    for (uint32_t i = 0; i < drawCount; ++i) EncBenchRecordThrough(encoder, draw);
                    inlineMs.push_back(watch.ElapsedMs());
                }
                else
                {
                    // ★ 실물이다. BeginFrame 이 방금 인코더를 다시 만들었으므로
                    //   (A-3) 이것은 이 프레임의 리스트에 붙어 있다.
                    RHIEncoder& encoder = resources.GetImmediateEncoder();
                    watch.Start();
                    for (uint32_t i = 0; i < drawCount; ++i) EncBenchRecordReal(encoder, draw);
                    realMs.push_back(watch.ElapsedMs());
                }

                if (!resources.EndFrame(error)) { outLog += "EndFrame 실패\n"; return false; }
                resources.WaitForGpu();
                return true;
            };

            bool stepOk = true;
            for (uint32_t step = 0; step < 4 && stepOk; ++step)
            {
                stepOk = runPath((order + step) % 4);
            }
            if (!stepOk) { passed = false; break; }
        }

        if (!passed) break;

        const double d = EncBenchMedian(directMs);
        const double v = EncBenchMedian(virtualMs);
        const double n = EncBenchMedian(inlineMs);
        const double r = EncBenchMedian(realMs);

        // 호출당 차이. 이 수가 판단의 근거다 — 프레임당 호출 수를 곱하면
        // dx12.live status의 CPU ms에서 얼마를 먹을지가 나온다.
        const double calls = static_cast<double>(drawCount) * kEncBenchCallsPerDraw;
        const double perCallNs = (0.0 < calls) ? ((v - d) * 1e6 / calls) : 0.0;

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "  %6u  %8.3f  %8.3f  %8.3f  %8.3f  %+9.3f  %+9.3f  %+9.2f\n",
            drawCount, d, v, n, r, v - d, r - d, perCallNs);
        outLog += line;
    }

    resources.Shutdown();

    outLog += "\n읽는 법: '가상-직접'이 ms 차이, '호출당'이 그것을 호출 수로 나눈 값이다.\n"
              "프레임당 드로우가 1000이면 호출은 5000이므로, 호출당 1ns는 프레임당 5us다.\n"
              "dx12.live status의 CPU ms와 견주어 유의미한지 판단할 것.\n"
              "'실물-직접'이 지금 패스가 실제로 무는 값이다 (A-4) — vtable 넷에\n"
              "핸들 해소 셋(상수·정점·인덱스)이 더해진 것이고, '가상-직접'을\n"
              "빼면 해소만의 값이 나온다.\n";

    outLog += passed ? "인코더 오버헤드 실측 완료\n" : "인코더 오버헤드 실측 실패\n";
    return passed;
}

#endif
