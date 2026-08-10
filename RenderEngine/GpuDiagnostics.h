#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <string_view>

// GPU 진단 장부 (DX11 DeviceResources에서 이관, 2026-08-10).
//
// ── 왜 RHI 밖인가 ──
//
// 옮겨 온 것 중 절반은 디바이스가 아니라 장부의 일이었다: 기준선을 들고,
// 증감을 내고, 로그 문장을 짓는다. 백엔드가 바뀌어도 그대로인 코드라
// IRHIDeviceResources에 넣으면 구현마다 복사된다.
//
// 그래서 경계가 이렇게 갈렸다:
//   IRHIDeviceResources — 원자료(QueryVideoMemory · CaptureLiveObjectCensus)
//   여기                — 그 원자료로 하는 일(기준선·증감·서술)
//
// 대상 디바이스는 GetDiagnosticsDeviceResources()가 정한다(상시 러너).
// 아직 서기 전이거나 해체된 뒤면 조용히 아무것도 하지 않는다 — 부팅과 종료에
// 실제로 그런 구간이 있고, 진단이 없다고 해서 진행을 막을 이유는 없다.
namespace GpuDiagnostics
{
    /// 현재 집계를 로그에 남긴다. label은 측정 시점을 식별하는 이름
    /// (예: "씬 로드 완료", "에디터 종료 시점").
    ///
    /// allowDeviceEnumeration은 "이후 렌더가 없다"는 약속이다 — 타입별 집계가
    /// 필요한 종료 지점에서만 true로 준다(IRHIDeviceResources 주석 참고).
    void LogCensus(std::string_view label, bool allowDeviceEnumeration = false);

    /// 직전 기준선 대비 증감을 로그에 남기고, 현재 값을 새 기준선으로 삼는다.
    /// 씬 전환 전후로 부르면 회수되지 않은 리소스가 그대로 드러난다.
    void LogDelta(std::string_view label, bool allowDeviceEnumeration = false);

    /// 기준선을 현재 상태로 초기화한다(측정 구간의 시작점 지정).
    void ResetBaseline();

    /// 살아있는 객체를 디버거 출력 창에 그대로 쏟는다(집계·파싱 없음).
    ///
    /// 위의 셋과 달리 진단 대상 디바이스를 요구하지 않는다 — DXGI 디버그
    /// 계층은 프로세스 범위라 디바이스가 이미 해체된 뒤에도 남은 것을 훑는다.
    /// 종료 최종 지점(App::Finalize)이 이것을 부르는 이유가 그것이다.
    ///
    /// 디바이스가 아직 살아 있으면 그 디바이스의 보고도 함께 낸다.
    void ReportLiveObjects();
}

#endif
