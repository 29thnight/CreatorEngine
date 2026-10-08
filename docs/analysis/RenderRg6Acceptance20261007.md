# RG6 제품 cutover 수용 — 2026-10-07

RG6를 완료하고 계획 기성 4인일을 회수한다. 같은 현재 소스·씬·재질·셰이더에서 별도 진단 DeclarationOrder 빌드와 제품 기본 ExplicitVersioned/DependencyOrder 빌드를 Debug/Release 각각 독립 2프로세스로 비교했다. 최종 색상, 중간 첨부, 그래프 결정성, validation, 정상 종료 및 CPU/GPU 비용 기록이 정본 RG6 게이트를 통과했다. **Release CPU 기록 중앙값은 1.20→2.69 ms(+124.8%)로 증가했다. 구조·회귀 수용이며 성능 개선 완료가 아니다.** RG-V UI, MAT-9 품질/성능, Vulkan 교차 수용은 올리지 않는다.

## 비교 계약과 구현

- `CreatorRg6ReferenceBuild=true`는 EnhancedSceneRenderer.cpp 하나에 `CE_RG6_REFERENCE_DECLARATION_ORDER`를 정의하는 별도 진단 빌드다. 런타임 전환 스위치가 없으며 일반 빌드는 두 RHI live 생성자에서 ExplicitVersioned/DependencyOrder를 사용한다.
- 동일 소스 해시 목록으로 네 빌드를 검증했다. reference 실행 파일/runtime DLL은 보관된 사본의 해시로 확인하고 제품 파일은 현재 기본 배포 파일로 확인했다. 제품 Debug/Release를 기본 옵션으로 재빌드한 뒤 실행했다. [공동 BASE-0 완료](../../Build/Verification/RG6Acceptance20261007/v2/baseline-phase-complete.json).
- 동일 LX_CookFixture, 준비 완료된 lattice 재질, 카메라·world·lights·해상도 64×64, 정지 clock/history restart, IBL 1024/4096을 고정했다. 입력 identity는 모든 구성/정책/프로세스에서 `7f3cd805e8720609af9f14c8f11af20377358623d6c2dff565a5ad3e7e31e999`다. GPU validation은 켠 상태다.
- 모드에 따라 제품 Geometry Occlusion mip 6개 및 CullOcclusion이 생기고 기존 Cull이 대체된다. GizmoIcon의 명시 texture read 2개도 추가된다. reference 59 pass/51 resource, 제품 65 pass/59 resource다. 따라서 전체 비용 차이를 순수 스케줄러 overhead나 역사적 PR 전체 비용으로 귀속하지 않는다. 수용 도구는 이 정해진 graph delta만 허용하며 예상 밖 pass/resource 변경을 거부한다.
- 포인터/슬롯·풀 초기 상태·generation을 topology identity로 오인하지 않고, 생산/소비 버전·실행 순서·dependency hash·lifetime·wave·접근 선언을 별도로 감사한다.

## 실행 수용

8개의 독립 live 프로세스 모두 exit 0, 강제 종료 0, GPU validation enabled/problems 0/dropped messages 0이다. 각 빌드의 첫 프로세스에서 100회, 두 번째에서 2회 캡처하여 총 408개 accepted capture의 sealed input·compiled order·dependency hash를 검사했다. 제품 Debug/Release 각각 동일 입력 100회 결정성이 통과했다. 100회 전체 이미지 바이트 쌍 비교로 확대하지 않는다: 결정성은 모든 manifest를 감사하고, 픽셀 비교는 프로세스 내/프로세스 간/전후/구성 간의 선택된 실제 캡처에 수행했다.

네 native graph fixture 실행도 정상 종료했다. 각 실행의 `RG5_FINAL_GPU_OK policies=3 frames=129 stages=9 maxError=0`과 `RG4_GPU_OK immediate workers=1/2/4 fallback split join compiled-order pixels=0 drops=0`을 확인했다. 양·음성 graph fixture, 24 shuffle 및 critical-path fixture를 재실행했다. 전체 live 그래프의 criticalPath/wave와 pass GPU 값은 최종 JSON/manifest에 보존했다.

각 빌드의 같은 프로세스/독립 프로세스 픽셀 비교와 제품 Debug/Release 비교는 모두 통과했다. 버전 경로 14종·legacy 10종 artifact 변이를 거부했으며, cutover 비교 도구도 입력 변경·예상 밖 패스·픽셀 훼손을 거부했다.

## 전환 전후 픽셀

16개 attachment/stage에 기존 판정식 `absError ≤ 0.002 + 0.005 × max(abs(before), abs(after))`를 그대로 적용했다. 4개 전후 비교 모두 허용 오차 초과 픽셀 0이다. 최종 display, preTone HDR와 색상 단계는 동일하며 깊이는 아래처럼 별도 판정한다.

| 비교 | final max | depth max | depth RMSE | depth changed pixels | 전체 exceeded pixels |
|---|---:|---:|---:|---:|---:|
| Debug-process-0 | 0 | 0.00256198645 | 0.000491265455 | 256 | 0 |
| Debug-process-1 | 0 | 0.00256198645 | 0.000491265455 | 256 | 0 |
| Release-process-0 | 0 | 0.00256198645 | 0.000491265455 | 256 | 0 |
| Release-process-1 | 0 | 0.00256198645 | 0.000491265455 | 256 | 0 |

legacy는 Grid의 깊이 갱신 후 GBuffer 깊이를 readback한다. 버전 경로의 캡처는 지정된 GBuffer.Depth v1을 읽고 Grid는 v2를 쓰므로 WAR edge에 따라 캡처가 Grid 앞에 실행된다. 이 읽기 의미와 순서 차이는 그래프에 기록돼 있다. 깊이까지 비트 동일이라고 보고하지 않는다. 모든 원본/차영상 PNG와 선형 오차는 [최종 수용 JSON](../../Build/Verification/RG6Acceptance20261007/v2/final-result.json)에 연결된 구성별 비교 폴더에 있다.

## 비용 측정

각 빌드별 독립 2프로세스 × unique GPU frame 100개, 총 800개 일반 프레임 표본을 기록했다. CPU last record와 GPU 완료 프레임은 비동기 window이므로 같은 frame의 한 쌍으로 취급하지 않는다. 아래 값은 ms이며 GPU validation on의 64×64 정적 fixture 범위다. pass별 GPU 분포·프로세스별 분포·전체 범위·device budget snapshot은 JSON에 보존했다.

| 구성/경로 | live CPU median / p95 | live GPU median / p95 | capture compile median | capture record median | capture GPU median |
|---|---:|---:|---:|---:|---:|
| Debug/Reference | 1.7855 / 2.5060 | 1.9847 / 3.8260 | 0.5185 | 2.1048 | 2.3374 |
| Debug/Product | 5.4675 / 9.6940 | 2.4475 / 5.9757 | 2.3193 | 5.5459 | 2.6941 |
| Release/Reference | 1.1975 / 1.4660 | 1.5213 / 1.8801 | 0.0408 | 1.3152 | 1.8643 |
| Release/Product | 2.6925 / 5.0310 | 1.7320 / 2.2752 | 0.1290 | 2.5631 | 2.1886 |

Release live CPU median은 1.1975→2.6925 ms(+124.8%)다. capture record/GPU에는 readback과 진단 stage가 포함된다. GPU queue span은 측정 구간이며 화면 FPS/전 프레임 latency가 아니다. 메모리는 DXGI 사용량 snapshot이며 transient peak/alias 이득 증거가 아니다. 정본 RG6는 비용 산출물을 요구하고 고정 성능 향상률을 요구하지 않는다. 이번 수용을 성능 향상 또는 MAT-9의 이동 카메라/4K/일반 배포 성능 완료로 확대하지 않는다.

## 실패 보존과 재검증

첫 Reference-Debug 실행은 로컬 명령 HTTP의 socket address 충돌로 중단되어 강제 종료됐고 수용에서 제외했다. `verify-render-base0.ps1`의 반복 요청이 한 WebRequestSession/connection pool을 재사용하고 마지막에 dispose하도록 수정했다. 변경된 하네스로 reference와 제품 Debug/Release를 새 v2 폴더에서 전부 다시 실행했다. v1 실패/예비 비교는 보존하되 최종 증거에 혼합하지 않는다.

진단 배포 사본의 Saved 폴더 정리는 자동 승인 검토의 `blocked by policy`로 실행되지 않았다. 원본 프로파일 자료와 복사본 모두 보존돼 있다. 해당 파일은 수용 입력/판정에 사용하지 않는다.

## 문서·기성

RG6 done/earnedDays 4, PHASE 4.3 100/기성 50/잔여 50, PHASE 4 계열 355/기성 102/잔여 253(+미산정)으로 맞춘다. RG-V는 progress/기성 0, RG7~RG9/Q0는 미착수다. 코드/문서는 로컬 변경이며 commit/push는 하지 않았다.

근거: [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md), [로드맵](../plans/RenderPhaseRoadmap.md), [공수 원장](../plans/RenderPhaseEffortEstimate.md), [최종 증거](../../Build/Verification/RG6Acceptance20261007/v2/final-result.json).
