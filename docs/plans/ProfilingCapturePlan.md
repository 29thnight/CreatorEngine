# 프레임 프로파일러 수집·녹화·구간 분석 계획

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/ProfilingCapturePlan.md)

## 남은 작업과 완료 판정

- 14-REC/VIEW/LIVE/DX 및 미완료 트랙: 녹화/조회/UI·렌더 provenance·손실·비용을 실제 제품에서 수용한다. 현재 profiler 기록과 viewer 기반은 재구현하지 않는다.
- 14-7의 독립 .ceframe 수동 RenderCapture는 RenderFrameDebuggerPlan이 소유한다. .ceprof 연속 기록·프레임 ID·SDK Present 반환을 물리 표시시각/생성 프레임 신원으로 해석하지 않는다.

## 적용할 구조와 경계

`producer counters/events → bounded profiler records → snapshot/stream → viewer`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `14-6` · PHASE 14 | P6 — .ceprof 저장·불러오기 | todo |
| `14-7` · PHASE 14 | 독립 Render Frame Debugger (RF0~RF7) | todo |
| `14-LIVE` · PHASE 14 | Rendering - Live·Scene 통계의 녹화 독립 view 귀속 | progress |
| `14-REC` · PHASE 14 | 연속 녹화·CPU/GPU Stop 꼬리 수용 | progress |
| `14-VIEW` · PHASE 14 | 별도 프로파일러 뷰어·IPC·창 수명 수용 | progress |
| `14-DX` · PHASE 14 | DX12 심층 수집·세션 CLI 실행 수용 | progress |

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

<span id="-호출부-형태에-독립이어야-한다"></span>
<span id="0-한-줄-결론"></span>
<span id="05-2026-09-15-개정--무엇이-바뀌었나"></span>
<span id="051-실측으로-뒤집힌-전제"></span>
<span id="0510-2026-09-21-p4-착수-전-실측--수집의-83-가-남의-제출을-읽고-있었다"></span>
<span id="0511-2026-09-21-p4-첫-조각--gpuframetoken-83--0"></span>
<span id="0512-2026-09-21-p4-raw-구간-보존--정상인-것을-결함으로-세고-있었다"></span>
<span id="0513-2026-09-21-p4-clock-calibration--판정을-임계값이-아니라-인과에-걸었다"></span>
<span id="0514-2026-09-21-p4-gpu-귀속--늦게-온-것을-제-프레임-칸으로-돌려보냈다"></span>
<span id="0515-2026-09-21-p2-동시성-경계--자극을-세우자마자-죽었다"></span>
<span id="0516-2026-09-21-p2-수집-경계--세대와-짝을-자리로-맞췄다"></span>
<span id="0517-2026-09-21-p2-소유-경계--규약을-강제로-바꿨다"></span>
<span id="0518-2026-09-21-p2-제어-경계--기다림을-넣어-교착을-샀다"></span>
<span id="0519-2026-09-21-p2-종료-경계--세어만-두고-재지-않았다"></span>
<span id="052--지금은-녹화가-아니다--4프레임-실시간이다"></span>
<span id="0520-2026-09-22-p4-gpu-timeline-트랙--순서를-정하고-막대가-자기-출처를-말하게-했다"></span>
<span id="0521-2026-09-22-p4-프레임-경계-트랙--점에는-길이가-없다"></span>
<span id="0522-2026-09-22-p4-74-minp95frames--누적만으로는-안-나오는-셋"></span>
<span id="0523-2026-09-22-p4-녹화-중에-보이는-그래프--흐르지-않으면-그래프가-아니다"></span>
<span id="0524-2026-09-22-p4-타임라인이-보이는-창을-그린다--한-칸만-그리면-두-그림이-서로-다른-말을-한다"></span>
<span id="0525-2026-09-22-문서-부채--계획서가-남은-일을-잘못-세고-있었다"></span>
<span id="0526-2026-09-23-p6-1-캡처가-제-어휘와-시계를-들고-다닌다--파일보다-먼저"></span>
<span id="0527-2026-09-23-p6-2-ceprof--첫-판에-다-초록이었다는-것은-아무것도-증명하지-않는다"></span>
<span id="0528-2026-09-23-p6-3-에디터가-파일을-열고-쓴다--다음-프레임이-연-파일을-덮는다"></span>
<span id="0529-2026-09-23-p7-목표-정정--외부-캡처-연결이-아니라-내장-렌더-디버거"></span>
<span id="053-재활용하지-않는다--구조적-근거"></span>
<span id="0530-2026-09-23-cpu-수집기를-전용-신호-대기-스레드로-이전"></span>
<span id="0531-2026-09-23-봉인-페이지의-수집기-측-deserialize-및-얕은-라이브-스냅샷"></span>
<span id="0532-2026-09-23-수집기-포화종료-경합과-4모드-코어-비용-측정"></span>
<span id="0533-2026-09-23-기본-수집-큐페이지-풀-확장"></span>
<span id="0534-2026-09-23-라이브-page-ingest-명칭수집-단계-비용-계측"></span>
<span id="0535-2026-09-23-p4-완료--두-뷰인플라이트손실-진단을-제품-경로로-검증"></span>
<span id="0536-2026-09-23-14-7을-프로파일러와-독립한-단일-프레임-캡처로-정정"></span>
<span id="054-자동-계측은-이-계획의-전제가-아니다"></span>
<span id="055--phase-13enkits과의-순서-제약--새로-생긴-의존"></span>
<span id="056-개정으로-바뀐-실행-계획"></span>
<span id="057-2026-09-20-정찰-최신화--전제-다섯이-또-바뀌었다"></span>
<span id="058-2026-09-20-p1p2-착지--옛-코어를-걷었다"></span>
<span id="059-2026-09-21-p3-착지--접는-일을-그리는-층에서-떼어냈다"></span>
<span id="1-범위와-비범위"></span>
<span id="10-실행-계획"></span>
<span id="11-검증-매트릭스"></span>
<span id="11-이번-계획의-범위"></span>
<span id="110-게이트--어느-검사가-무엇을-무는가"></span>
<span id="111-cpu-정확성"></span>
<span id="112-gpu-정확성"></span>
<span id="113-파일"></span>
<span id="114-오버헤드"></span>
<span id="12-1차-범위에서-제외"></span>
<span id="12-실패-정책과-관측-가능성"></span>
<span id="13-구현-중-지켜야-할-결정"></span>
<span id="14-7--독립-render-frame-debugger-상세-renderframedebuggerplanmd-rf0rf7"></span>
<span id="14-live--녹화-독립-rendering-페이지-2026-10-01-추가"></span>
<span id="14-최종-완료-조건"></span>
<span id="15-참고-기준"></span>
<span id="2-현재-소스에서-확인한-기반"></span>
<span id="2026-10-07-cpu-성능-개선-우선-적용"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="21-cpu-계측"></span>
<span id="22-기존-frameprofiler-ui"></span>
<span id="23-dx12-gpu-계측"></span>
<span id="24-counter-기반"></span>
<span id="3-녹화-기능-전에-닫아야-할-정확성-문제"></span>
<span id="31-cpu-producer를-프레임-말미에-직접-초기화한다"></span>
<span id="32-종료한-스레드의-tls-주소-수명"></span>
<span id="33-gpu-frame-record와-query-slot이-분리돼-있다"></span>
<span id="34-엔진-프레임과-렌더-제출은-11이-아니다"></span>
<span id="35-profiler가-에디터-모듈에-있다"></span>
<span id="4-목표-계층과-의존-방향"></span>
<span id="5-공통-데이터-모델"></span>
<span id="51-clock"></span>
<span id="52-marker"></span>
<span id="53-event"></span>
<span id="54-frame"></span>
<span id="55-counter"></span>
<span id="6-수집-파이프라인"></span>
<span id="61-recorder-상태-머신"></span>
<span id="62-thread-stream"></span>
<span id="63-rolling-capture"></span>
<span id="64-immutable-reader"></span>
<span id="7-에디터-ux"></span>
<span id="71-toolbar"></span>
<span id="72-frame-overview"></span>
<span id="73-timeline"></span>
<span id="74-hierarchy"></span>
<span id="75-module-panels"></span>
<span id="8-ceprof-캡처-파일"></span>
<span id="81-요구사항"></span>
<span id="82-초기-형식"></span>
<span id="9-독립-render-frame-debugger와의-선택적-연결"></span>
<span id="build-정책--만들-것이-아니라-물-것이다"></span>
<span id="p0--현재-동작-기준선과-profiler-selftest-표면"></span>
<span id="p0-실측-결과-2026-08-11-완료"></span>
<span id="p1--enginediagnostics-코어와-공통-frame-clock"></span>
<span id="p1a--수집-코어-물리-이관-2026-08-24-완료"></span>
<span id="p1b--새-코어와-공통-frame-clock-개정"></span>
<span id="p1b-착수-전-선행--죽은-게이트-축-복구"></span>
<span id="p2--cpu-thread-stream과-rolling-capture"></span>
<span id="p3--cpu-중심-에디터-녹화-mvp"></span>
<span id="p4--gpu-frame-token과-cpugpu-clock-정렬"></span>
<span id="p5--counter-provider와-관리-marker"></span>
<span id="p6--저장불러오기"></span>
<span id="sec-0-한-줄-결론"></span>
<span id="sec-05-2026-09-15-개정--무엇이-바뀌었나"></span>
<span id="sec-1-범위와-비범위"></span>
<span id="sec-10-실행-계획"></span>
<span id="sec-11-검증-매트릭스"></span>
<span id="sec-12-실패-정책과-관측-가능성"></span>
<span id="sec-13-구현-중-지켜야-할-결정"></span>
<span id="sec-14-live--녹화-독립-rendering-페이지-2026-10-01-추가"></span>
<span id="sec-14-최종-완료-조건"></span>
<span id="sec-15-참고-기준"></span>
<span id="sec-2-현재-소스에서-확인한-기반"></span>
<span id="sec-2026-10-07-cpu-성능-개선-우선-적용"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-녹화-기능-전에-닫아야-할-정확성-문제"></span>
<span id="sec-4-목표-계층과-의존-방향"></span>
<span id="sec-5-공통-데이터-모델"></span>
<span id="sec-6-수집-파이프라인"></span>
<span id="sec-7-에디터-ux"></span>
<span id="sec-8-ceprof-캡처-파일"></span>
<span id="sec-9-독립-render-frame-debugger와의-선택적-연결"></span>
<span id="①-분석하려면-pause-를-눌러야-했다"></span>
<span id="②-막대-폭을-프레임-수로-나누고-있었다"></span>
<span id="③-2-휠이-그래프를-굴리면서-창도-같이-굴렸다"></span>
<span id="③-3-workers-자극도-profilerecord-가-필요했다"></span>
<span id="③-자가-없었다"></span>
<span id="매크로를-버리는-이유--둘-다-구조적-이득이다"></span>
<span id="표기"></span>

</details>
