# 메모리 프로파일러 확장

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/MemoryProfilerPlan.md)

## 남은 작업과 완료 판정

- MP2~6: allocator 소유자/생애·CoreCLR 객체/루트 그래프·GPU 개별 리소스/residency를 출처가 있는 스냅샷으로 연결한다.
- 버전/무결성 있는 저장·재열람·A/B 차이·큰 스냅샷 비차단 UI와 실제 D/R 비용/지연을 수용한다. 구현된 수동 스냅샷/Memory 탭과 집계 VRAM을 객체별 소유권 완료로 보지 않는다.

## 적용할 구조와 경계

`producer counters/events → bounded profiler records → snapshot/stream → viewer`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `14-MP2` · PHASE 14 | MP2 — 네이티브 할당 소유자 | todo |
| `14-MP3` · PHASE 14 | MP3 — 관리 객체 참조 그래프 | todo |
| `14-MP4` · PHASE 14 | MP4 — GPU 개별 리소스 메모리 | todo |
| `14-MP5` · PHASE 14 | MP5 — 스냅샷 저장과 장기 비교 | todo |
| `14-MP6` · PHASE 14 | MP6 — 제품 정확성·비용 게이트 | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/EngineDiagnostics/ProfilerViewerTransport.h:127](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/EngineDiagnostics/ProfilerViewerTransport.h#L127) — `codec(timing.frame_close_ticks) && codec(timing.snapshot_ticks) && codec(timing.replenish_ticks) &&`
- [Editor/EngineGUIWindow/MemoryProfilerSnapshot.cpp:180](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MemoryProfilerSnapshot.cpp#L180) — `auto result = std::make_shared<snapshot>();`
- [Editor/EngineGUIWindow/MemoryProfilerSnapshot.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MemoryProfilerSnapshot.h#L13) — `using snapshot = ce::profiler_viewer::diagnostics::memory_snapshot;`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="sec-검증-기록"></span>
<span id="sec-아직-같지-않은-것"></span>
<span id="sec-진행-순서와-완료-조건"></span>
<span id="sec-현재-계약"></span>
<span id="검증-기록"></span>
<span id="아직-같지-않은-것"></span>
<span id="진행-순서와-완료-조건"></span>
<span id="현재-계약"></span>

</details>
