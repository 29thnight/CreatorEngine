#include "GpuDiagnostics.h"
#include "RHI/IRHIDeviceResources.h"
#include "EngineResourceCensus.h"
#include "LogSystem.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <wrl/client.h>
#include <dxgi1_3.h>     // DXGIGetDebugInterface1 — dxgidebug.h 는 타입만 준다
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")

namespace
{
    // 장부. 구 DX11 DeviceResources의 m_baselineCensus/m_baselineResources/
    // m_hasBaseline을 그대로 옮긴 것이다 — 디바이스 수명과 무관한 상태라
    // 디바이스가 교체돼도(라이브 파이프라인 재생성) 기준선은 유지된다.
    RHIGpuObjectCensus            g_baselineCensus;
    Diagnostics::ResourceSnapshot g_baselineResources;
    bool                          g_hasBaseline = false;

    // 집계를 "타입:개수" 목록으로 만든다. 개수가 많은 순으로 상위 항목만 남긴다.
    std::string SummarizeByType(const std::map<std::string, uint32_t>& byType, size_t maxEntries)
    {
        std::vector<std::pair<std::string, uint32_t>> sorted(byType.begin(), byType.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });

        std::string out;
        const size_t limit = (std::min)(maxEntries, sorted.size());
        for (size_t i = 0; i < limit; ++i)
        {
            if (!out.empty()) out += ", ";
            out += sorted[i].first;
            out += ':';
            out += std::to_string(sorted[i].second);
        }
        if (sorted.size() > limit)
        {
            out += " 외 " + std::to_string(sorted.size() - limit) + "종";
        }
        return out;
    }

    void WriteCensus(const RHIGpuObjectCensus& census, std::string_view label)
    {
        if (!census.available)
        {
            // 디버그 레이어를 못 쓰는 실행 중에는 엔진이 직접 센 에셋 수로 대신한다.
            Debug->Log(std::format("[GPU 진단] {} · VRAM {}MB / {}MB · 엔진 에셋 {}",
                label, census.vramUsedMB, census.vramBudgetMB,
                Diagnostics::FormatSnapshot(Diagnostics::CaptureResourceSnapshot())));
            return;
        }

        Debug->Log(std::format("[GPU 진단] {} · 라이브 객체 {}개 · VRAM {}MB / {}MB",
            label, census.totalObjects, census.vramUsedMB, census.vramBudgetMB));
        Debug->Log(std::format("[GPU 진단]   타입별: {}", SummarizeByType(census.byType, 12)));
    }

    void WriteDelta(const RHIGpuObjectCensus& current, std::string_view label)
    {
        const Diagnostics::ResourceSnapshot resources = Diagnostics::CaptureResourceSnapshot();

        if (!g_hasBaseline)
        {
            g_baselineCensus = current;
            g_baselineResources = resources;
            g_hasBaseline = true;
            WriteCensus(current, label);
            return;
        }

        const int64_t objectDelta =
            static_cast<int64_t>(current.totalObjects) - static_cast<int64_t>(g_baselineCensus.totalObjects);
        const int64_t vramDelta =
            static_cast<int64_t>(current.vramUsedMB) - static_cast<int64_t>(g_baselineCensus.vramUsedMB);

        if (!current.available)
        {
            // 실행 중 경로. 씬을 오갔을 때 이 에셋 수가 제자리로 돌아오는지가 핵심 지표다.
            Debug->Log(std::format("[GPU 진단] {} · VRAM {}MB ({:+}MB) · 엔진 에셋 {}",
                label, current.vramUsedMB, vramDelta,
                Diagnostics::FormatDelta(resources, g_baselineResources)));

            g_baselineCensus = current;
            g_baselineResources = resources;
            return;
        }

        // 증가한 타입만 추린다. 회수되지 않은 리소스가 여기에 그대로 드러난다.
        std::map<std::string, uint32_t> increased;
        for (const auto& [type, count] : current.byType)
        {
            const auto previous = g_baselineCensus.byType.find(type);
            const uint32_t before = (previous == g_baselineCensus.byType.end()) ? 0u : previous->second;
            if (count > before)
            {
                increased[type] = count - before;
            }
        }

        Debug->Log(std::format("[GPU 진단] {} · 라이브 객체 {}개 ({:+}) · VRAM {}MB ({:+}MB) · 엔진 에셋 {}",
            label, current.totalObjects, objectDelta, current.vramUsedMB, vramDelta,
            Diagnostics::FormatDelta(resources, g_baselineResources)));

        if (increased.empty())
        {
            Debug->Log("[GPU 진단]   증가한 객체 타입 없음");
        }
        else
        {
            // 누수 후보이므로 경고 레벨로 남긴다(경고 이상은 즉시 디스크에 기록된다).
            Debug->LogWarning(std::format("[GPU 진단]   증가: {}", SummarizeByType(increased, 12)));
        }

        g_baselineCensus = current;
        g_baselineResources = resources;
    }
}

bool GpuDiagnostics::Capture(Snapshot& out, bool advanceBaseline)
{
    auto* device = GetDiagnosticsDeviceResources();
    if (!device) return false;
    out.current = device->CaptureLiveObjectCensus(false);
    out.resources = Diagnostics::CaptureResourceSnapshot();
    out.hasBaseline = g_hasBaseline;
    out.baseline = g_baselineCensus;
    out.baselineResources = g_baselineResources;
    if (advanceBaseline)
    {
        g_baselineCensus = out.current;
        g_baselineResources = out.resources;
        g_hasBaseline = true;
    }
    return true;
}

void GpuDiagnostics::LogCensus(std::string_view label, bool allowDeviceEnumeration)
{
    auto* resources = GetDiagnosticsDeviceResources();
    if (nullptr == resources)
    {
        return;
    }
    WriteCensus(resources->CaptureLiveObjectCensus(allowDeviceEnumeration), label);
}

void GpuDiagnostics::LogDelta(std::string_view label, bool allowDeviceEnumeration)
{
    auto* resources = GetDiagnosticsDeviceResources();
    if (nullptr == resources)
    {
        return;
    }
    WriteDelta(resources->CaptureLiveObjectCensus(allowDeviceEnumeration), label);
}

void GpuDiagnostics::ResetBaseline()
{
    auto* resources = GetDiagnosticsDeviceResources();
    if (nullptr == resources)
    {
        return;
    }
    g_baselineCensus = resources->CaptureLiveObjectCensus(false);
    g_baselineResources = Diagnostics::CaptureResourceSnapshot();
    g_hasBaseline = true;
}

void GpuDiagnostics::ReportLiveObjects()
{
#if defined(_DEBUG)
    // 디바이스가 아직 있으면 그쪽 보고를 먼저 낸다(백엔드가 자기 객체를 안다).
    if (auto* resources = GetDiagnosticsDeviceResources())
    {
        resources->ReportLiveObjectsToDebugOutput();
        return;
    }

    // 디바이스가 없는 경우 — 종료 최종 지점이 그렇다. DXGI 디버그 계층은
    // 프로세스 범위라 여기서도 남은 것을 훑는다.
    //
    // ★ 이 계층이 여기 있는 이유: DXGI는 디바이스가 아니라 프로세스에 붙는다.
    //   IRHIDeviceResources에 두면 '디바이스가 죽은 뒤의 보고'라는, 그 인터페이스가
    //   답할 수 없는 질문이 된다.
    Microsoft::WRL::ComPtr<IDXGIDebug> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
    }
#endif
}

