# master 동기화와 기존 적용 사항 재점검

2026-10-23, 로컬 master를 `0847ceeb2ec99d1b4edbdfd9f7f8a94bd773f915`에서 `a985ee7e177af1a2ff13739c9c0ab09c5dcad887`로 fast-forward했다. 상류 19커밋과 기존 미커밋 작업을 통합했다. 실행 증거와 소스·바이너리 지문은 [동명 JSON](MasterSyncRecheck20261023.json)에 기록한다. [이전 애니메이션 수용](AnimationCookAcceptance20261023.md)의 수치와 JSON은 당시 실행 기록으로 보존한다.

## 보존과 충돌 해결

동기화 전에 변경 경로512개, 총333,096,368바이트를 기록했다. 기존 캐시50경로는 제자리에 두고 SHA-256을 기록했으며 나머지 파일의 원본 바이트와 패치를 별도로 보관했다. 복구용 stash `7777f4d3a8b3171459f9e18cc72d6265da2d94a0`도 유지한다. 원래 비어 있던 index를 복원하여 작업은 미커밋·미스테이징 상태로 남긴다. 커밋·푸시는 하지 않았다.

14파일의 충돌을 해결했다. 상류의 `own::shared_owner`와 최신 렌더 프로그램·SDK 진단 필드를 받아들이고 기존 F# API45, 고정 틱, 명령·스냅샷 교환, Archive·Parser 경계, 정적 메시 캐시의 세션 종료 배선을 유지했다. 상류와 겹치지 않는 파일103개는 원본 바이트로 복원했다.

서버 인계의 2026-10-21 설계는 역사적 제안으로 보존하고, 현재 계약은 [NetworkFrameworkPlan](../plans/NetworkFrameworkPlan.md)과 [최종 인계 점검](NetworkWorkerHandoff20261023.md)을 우선하도록 연결했다. F# 일반 멤버의 정적 등록과 `OnTickSimulate(dt)`를 유지한다. `OnSimulate`·`Scope.Delay`는 복원하지 않는다.

## 통합 후 발견한 문제와 수정

자동 병합된 `EnhancedSceneRenderer`의 새 post-tuning 진단 코드에 직접 `ryml` 접근이 남아 N1 경계 게이트가 실패했다. 공통 `Diagnostics::Document`·`Node` 경계로 이주했다. `EnhancedPbrCapture`와 `TemporalMotionFixture`도 최신 상류 필드를 유지하면서 같은 경계를 사용한다. 이후 실제 Editor 캡처와 경계 회귀를 다시 실행했다.

상류의 소유권 API 이주 뒤 메시·래스터 회귀 소비자가 이전 `std::shared_ptr` 형식을 요구하여 컴파일에 실패했다. 실제로 포함되는 래스터 fixture18개와 Scene packet 지원 코드를 함께 이주했다. 소유 객체 비교에는 기존 `SamePinnedObject`를 사용하고 만료·수명 검사를 유지했다. 활성 bucket이 span으로 바뀐 호출부는 `AppendBucket()`으로 이주하여 잘못된 root 순서의 거부 검사도 유지했다.

실제 AssetCooker에서도 사운드 그래프 컴파일 결과는 `own::shared_owner`인데 결과 보관 map은 이전 형식이었다. 같은 소유권 형식으로 맞췄다. 현재 쿠커로 클립1개·그래프1개를 실제 생성했고, 없는 클립 참조는 exit3으로 거부되며 기존 수용 산출물의 모든 SHA-256이 보존됨을 확인했다.

이전 수정의 SPIR-V 소스 기능 계약·v14 캐시, streaming generation 밀봉, AnimationPlayer의 모델·Graph·stable layer 이주, GPU 종료 후 메시 분할 캐시와 파일 lease 회수는 유지한다. 회귀 수치·assertion·금지 타입 상한은 올리지 않았다.

## 현재 통합 작업본에서 실행한 검사

| 검사 | 실행 결과 |
|---|---|
| 네이티브 Release 빌드 | Player·CreatorEditor·AssetCooker 통과 |
| BuildTool | 188검사 통과 |
| 셰이더 캐시 | 44검사 통과. DXIL/SPIR-V 쌍, 기능 계약 실패 보존·캐시 무효화·동시 요청 포함 |
| 서버 인계 회귀 | ND-a·W4/ND-b·N1·N3·PhysicsABI·PhysicsIdentity·LifecycleEnums, 7게이트 통과 |
| F# 기존 저작 비교 | 컴포넌트53종·필드34개 일치 |
| 실제 Editor의 F# 고정 틱·수명 | 7종 F# fixture, 실패·비활성·정리 경로 및 정상 종료 통과 |
| 실제 Editor의 N1 진단 캡처 | 캡처·첨부7개 판독, GPU validation0·정상 종료 통과 |
| CLI 소비자 계약 | PowerShell 5·7 모두 상한0·위반0, 결과 판독 회귀 각82검사 통과 |
| 메시 GPU·수명 | 1,264,244검사·594,828 GPU 성분, 정점 형식8종·포즈3종·제출32회 통과. 캐시 수명40검사 유지 |
| 재질 cook·실제 GPU 소비 | 두 등급 반복 산출물 바이트 일치, 실패6종 거부·기존 출력 보존, 24프레임·114,778검사·12,501 GPU 성분·validation0. 소스854개 drift0 |
| 사운드 그래프 실제 cook | 클립1·그래프1 성공, 누락 참조 거부·기존 출력 보존 |
| 새 SDK 패키징 | SDK `3a4b2497-c864-4c75-b8f3-b036c8f2cd9f`, 모델9·재질 프로그램14·산출물97 생성, 검증·smoke 통과 |
| 새 패키지의 애니메이션 소비 | PowerShell 5·7 모두69엔티티, Walk·54본·스킨 메시4개, 포즈 진행·정상 종료0·런타임 디렉터리 회수 통과 |

애니메이션 검증기는 작업본의 Player를 복사하므로 별도로 원래 패키지의 Player.exe·Player.runtime.dll과 두 셸 검증의 바이너리 SHA-256을 대조한다. 이전 SDK나 실행 파일을 이번 결과로 인용하지 않는다.

## 고정 틱과 렌더 보간의 제품 재검증

상류의 프레임 정책 변경을 반영한 동일 Editor 바이너리로 실제 DX12 제품 검사를 실행했다. 목표30/60/144fps에서 각각30.0042/59.9864/144.0422fps를 기록했다. 동일 tick의 CCT·강체 상대 궤적 차이는 모두0이며, CCT·강체·카메라 보간 행렬과 GPU 실루엣 검사를 통과했다. Scene/Game 대상 왕복4회와 독립 oracle의 canary17개도 통과했다.

별도 moving-quality 실행에서는 실제 `sourceEngineFrame`과 연결된 이동 중 0틱 GPU readback을 수용했다. 비활성화·재활성화·워프 전환의 실루엣도 통과했다. 이 캡처 실행의 측정은141.8559fps이며, 위 정상 pacing 실행과 구분한다.

실행 근거: [30/60/144fps](../../Build/Verification/N3Pacing/Editor-5d96511b93194bfc86a43e0783f9b706/result.json), [moving GPU](../../Build/Verification/N3Pacing/Editor-49724de945a24f9bbcc3f49d4a81543b/result.json), [PS7 애니메이션](../../Build/Verification/MasterSyncAnimation/Player-ps7/verification.json), [PS5 애니메이션](../../Build/Verification/MasterSyncAnimation/Player-ps5/verification.json).

이 수치는 해당 장치·fixture·짧은 측정 구간의 pacing 수용 결과다. 전체 게임의 성능 예산이나 이전 바이너리 대비 성능 향상을 뜻하지 않는다.

## 계획과 생성 문서 점검

Network·ScriptSurface·Animation·Material·BuildPipeline·Editor CLI 계획과 대시보드에 이번 기록을 연결했다. 생성된 계획의 첫 요약에 과거 `OnUpdate/API43`·C# 결정이 남아 있어, Markdown의 명시적 `plan-summary`가 최신 요약을 제공하도록 생성기를 수정했다. 과거 audit JSON의 소스 근거는 그대로 보존한다. 서버 인계의 최신 계약 안내도 실제 HTML에 표시한다.

계획66개와 전체 문서250개의 생성 소스 일치 검사를 통과했다. 변경한 계획·인계 문서는 HTML을 다시 생성하고 시각 검사를 수행했다. 상류 RHI 명세의 오래된 생성 소스도 원문에서 재생성했다. 최종 문서688페이지의 실행 스크립트369개 구문 검사에서 실패는0이다. 이 결과는 문서 검증이며 엔진·GPU·SDK 수용은 위의 별도 실행 근거로 판단한다.

변경 파일의 공백 검사도 통과했다. 전체 diff에는 동기화 전부터 있던 `active.workspace`의 EOF 빈 줄1건이 남아 있으며, 이 파일은 원래 SHA-256과 동일하게 보존했다.

## 수용 범위

이번 기록은 위 통합 작업본의 Windows Release·DX12 제품 경로를 대상으로 한다. 다른 GPU·Vulkan 제품 동등성·Shipping·일반 GUI 조작·Inspector 종료/장시간 soak·렌더 없는 서버 링크와 실제 Dedicated Server 구동은 별도다. SPIR-V cook 성공을 Vulkan 제품 실행 수용으로 확대하지 않는다. 이전 전체20모델·73프로그램 cook은 이전 JSON의 역사적 증거이며, 이번 새 패키지는 실제 AnimationPlayer 의존성인9모델·14프로그램을 검증했다.

문서·대시보드의 상태는 검증한 범위에만 맞춘다. 기존 애니메이션 재생을 확인한 결과로 AG0~AG5 신규 기능이나 LC9의 일반 GUI·Shipping 잔여를 완료 처리하지 않는다.
