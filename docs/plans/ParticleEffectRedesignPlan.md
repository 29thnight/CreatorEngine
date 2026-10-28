# 파티클·이펙트 재설계 계획 (PHASE 10)

> 2026-10-24 갱신 · 2026-10-22 사용자 결정 반영 · Niagara식 스택 + 모듈 그래프 · 10-1a/b/c 경계 수용 · 10-2a CPU IR / 10-2b-1 최소 GPU 수치 수용

[시뮬레이션 첫 계약](../design/ParticleEffectSimulationContract.md) · [모듈 IR 계약](../design/ParticleEffectModuleIRContract.md) · [프레임 전달 계약](../design/ParticleEffectTransportContract.md) · [저작 정의 계약 v1](../design/ParticleEffectAuthoringContract.md) · [전체 대시보드](../RefactoringPlanDashboard.html) · [엔진 레이어 경계](EngineLayerSeparationPlan.md) · [렌더 그래프](RenderGraphDependencySchedulingPlan.md)

## 목표와 저작 방향

구 DX11 계통 제거(10-0)는 유지하고 새 Effect 패키지를 작성한다. 저작은 **이미터의 단계별 모듈 스택을 기본 화면으로 두고, 사용자 정의 모듈 내부를 노드 그래프로 작성하는 Niagara식 구조**로 확정한다. 기존 Niagara 구현이나 자산 형식의 호환을 목표로 삼지는 않는다.

Effect 에셋은 여러 이미터와 노출 파라미터를 소유한다. 이미터는 생성·입자 초기화·입자 갱신·렌더 단계를 갖는다. 사용자는 단계에 맞는 모듈을 추가하고, 활성화·순서·입력을 편집한다. 사용자 정의 모듈을 열면 계산과 속성 접근을 노드로 편집한다. 효과 전체 화면은 이미터 구성과 타이밍을 보여준다. 이미터 간 이벤트 연결은 첫 범위에서 보류하고 이벤트 실행 계약을 정한 뒤 확장한다.

| 저작 수준 | 사용자 작업 | 저장되는 정본 |
|---|---|---|
| Effect | 이미터 추가, 재생 타이밍, 공통 파라미터 노출 | 이미터 참조·인스턴스 설정·파라미터 정의 |
| 이미터 스택 | 단계별 모듈 추가·비활성화·정렬 | 순서 있는 모듈 인스턴스와 입력 바인딩 |
| 모듈 그래프 | 노드·포트 연결, 속성 읽기·쓰기, 계산 | 타입이 있는 그래프와 모듈 입출력 정의 |
| 렌더 설정 | Billboard·Mesh·Trail, 머티리얼 지정 | 렌더 방식과 Material 에셋 참조 |

기본 모듈 예시는 Burst, Lifetime, Sphere Position, Cone Velocity, Gravity, Drag, Color/Size over Life다. 입력은 타입별로 상수·난수 범위·커브·그라데이션·노출 파라미터 바인딩을 제공한다. 모든 입력 타입에 모든 표현을 허용하지 않고 호환 표를 정의한다. Material Graph는 표면 외형을 소유하고 모듈 그래프는 입자 속성과 동작을 소유한다.

## 구현 전에 고정할 계약

- **단계와 실행 순서:** 생성은 방출 수를 계산하고 초기화는 새 입자에 한 번 실행한다. 갱신은 simulation tick마다 실행한다. 나이 증가와 위치 적분은 엔진이 정한 지점에서 한 번 수행하며 스택에 고정 항목으로 표시한다. Gravity와 Drag 등 모듈 순서가 결과에 미치는 의미를 문서화한다.
- **시계와 난수:** simulation tick과 view/frame을 분리한다. 다중 뷰에서 중복 적분하지 않는다. pause·step·누적 dt·시간 배율·건너뛴 프레임 정책을 고정한다. 난수는 seed, 입자 식별자, 모듈/입력 식별자 및 샘플 시점으로 재현하며 생성 시 고정과 매 tick 평가를 구별한다.
- **모듈 스키마:** 안정 ID·버전·허용 단계·입출력 타입·읽는 속성·쓰는 속성·부작용·입력 기본값을 정의한다. 스택 재정렬과 이름 변경이 ID를 바꾸지 않게 한다. 필수 속성의 초기화 전 읽기, 잘못된 단계, 중복 writer의 순서 의미를 검증한다.
- **그래프 의미:** 첫 버전은 타입이 있는 비순환 데이터 흐름, 명시적 속성 입력/출력, 정해진 연산 목록으로 제한한다. 임의 루프·재귀·외부 코드 실행은 지원 범위를 별도로 결정한다. 다중 속성 쓰기에는 명시적 실행 순서를 둔다. 단계 위반·타입 불일치·순환·누락 출력을 노드/포트 위치와 함께 진단한다.
- **공통 실행 표현:** 기본 모듈과 사용자 그래프를 동일한 타입 실행 표현(IR)으로 변환한다. 편집기에서만 동작하는 별도 시뮬레이터를 만들지 않는다. 모듈 하나를 GPU dispatch 하나에 고정하지 않고, 의미를 보존하는 범위에서 결합한다. 최적화 전후 결과를 비교한다.
- **자료와 수명:** 입자 속성 레이아웃·CPU/셰이더 배치·용량 초과·alive/dead·좌표 공간·handle generation·scene epoch를 정한다. spawn burst는 값 갱신의 최신 값 병합과 구분한다. GPU 완료 전 버퍼·descriptor를 재사용하지 않는다.
- **패키지와 합성:** 코어는 Effect 구체 타입을 모르고 패키지 없이 실행된다. 시뮬레이션과 HDR 렌더의 기여 지점을 분리한다. 색·깊이·투명 순서·motion/reactive/transparency 정책을 선언한다. 기존 UI 뒤 오버레이 확장점을 그대로 사용하지 않는다.
- **저장과 제품 경로:** 저작 스키마와 버전은 앞에서 정하고, 이후 저장/로드·cook·Player까지 같은 정의를 연결한다. 그래프 실패 시 진단과 마지막 유효 버전 유지 정책을 명시하며 조용히 오래된 결과를 최신 결과로 표시하지 않는다.

## 구현 순서와 완료 조건

대시보드의 10-1~5는 상위 추적 단위로 유지한다. 아래 세부 슬라이스는 구현 순서이며 추가 공수로 중복 합산하지 않는다. 그래프 범위 추가에 따른 공수 재산정은 실제 계약·최초 구현 결과를 바탕으로 별도 수행한다. 기존 10-4의 5일을 새 전체 저작 범위의 확정 견적으로 사용하지 않는다.

| 순서 | 소유 항목 | 산출물 | 통과 조건 |
|---|---|---|---|
| 1 | 10-1a | 빈 Effect 패키지, 공용 인터페이스, 등록·해제·무설치 경로 | 포함/제외 각각 Editor·Player 빌드와 시작·종료, 정적 라이브러리 등록 코드의 실제 링크 포함 확인 |
| 2 | 10-1b | Effect/Emitter/Module 스키마, 단계·시계·속성·난수·입력 계약 | 폭발 예제 정의로 순서·타입·잘못된 단계 거절 확인. 저장 버전과 모듈 ID 정책 확정 |
| 3 | 10-1c | snapshot 전달과 시뮬레이션/HDR 기여 경계 | 다중 뷰 중복 tick 방지, stale handle 거절, 씬 전환·재등록·resize·종료, 패키지 제외 회귀 |
| 4 | 10-2a | 공통 모듈 IR, 작은 그래프 변환기, CPU 기준 평가기 | 기본 Gravity와 같은 동작의 그래프가 같은 IR 의미/값을 내는지 비교. 타입·순환·출력 오류 진단. 그래프 UI 없이 수행 |
| 5 | 10-2b | GPU spawn·초기화·수명·등속 이동, 기본 갱신 모듈 | 고정 seed/dt의 실제 GPU readback과 CPU 기준 비교. 0/1/최대/초과 입자, 모듈 on/off, 배리어·완료 후 재사용 |
| 6 | 10-3a | Billboard와 HDR/깊이/머티리얼·temporal 연결 | 실제 씬에서 alpha/additive·차폐·다중 뷰·resize 및 temporal 정책 확인 |
| 7 | 10-4a | 최소 스택 편집기와 미리보기, 저장/로드 | 모듈 추가·활성·정렬·입력·커브·그라데이션·노출 파라미터 편집. pause/step/seed 재시작과 저장 재개방으로 같은 결과 |
| 8 | 10-4b | 모듈 그래프 편집기, 재사용 모듈 에셋, 컴파일 진단 | 노드 연결→공통 IR→GPU 실행. Gravity/Drag 기준 비교와 사용자 Vortex 작성. 노드 위치 진단, undo/redo, 저장·reload·참조 모듈 변경 무효화 |
| 9 | 10-3b | Mesh·Trail 렌더 | 실제 제품 화면과 자원 수명 확인. Trail CPU/GPU 선택은 작업량·측정으로 결정 |
| 10 | 10-4c | 컴포넌트·스크립트 제어·cook/Player 제품 경로 | 생성/정지/재시작·파라미터 전달·씬/프리팹 왕복·cook 후 Player 재생. C++/C# ABI 동시 변경 및 왕복 검증 |
| 11 | 10-5 | 최종 패키지 제거 수용과 성능 기준선 | 프로젝트/링크 의존을 실제 제외한 빌드·실행. 고정 입자 수·해상도·모듈 조합에서 CPU/GPU 시간과 메모리 기록 |

첫 구현은 10-1a다. **GPU 체인 전에 저작/모듈 계약과 작은 그래프 변환 경로를 세우고, 그래프 편집기 UI는 실제 GPU·스택 경로가 동작한 뒤 작성한다.** 10-4a와 10-4b는 Billboard만으로 먼저 검증하며 Mesh·Trail 완료를 기다리지 않는다. 패키지 제거 수용은 각 단계에서 계속 유지한다.

## 2026-10-22 정찰에서 확인한 연결 후보

정찰 당시 소스 HEAD: `0847ceeb2ec99d1b4edbdfd9f7f8a94bd773f915`. 아래는 기존 기반의 존재를 뜻하며 새 이펙트 실행 성공을 뜻하지 않는다.

- [EnhancedRenderPass.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h): PrepareFrame/Declare/Shutdown과 RHI 서비스 경계.
- [EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h): 버퍼·읽기/쓰기·버전 의존성과 그래프 실행.
- [RenderFeatureContributor.h](../../Engine/RenderEngine/Render/Core/RenderFeatureContributor.h): 기존 확장은 UI 뒤 오버레이와 LDR/gizmo 문맥에 한정돼 단계 확장이 필요하다.
- [ProxyCommand.h](../../Engine/RenderEngine/ProxyCommand.h): 내장 타입의 명령 집합이므로 패키지용 범용 계약이 이미 있다고 간주하지 않는다.
- [IRenderDeviceServices.h](../../Engine/RenderEngine/RHI/IRenderDeviceServices.h): 버퍼 생성과 CreateBufferReadback 후보. 실제 복사·완료 대기·map은 실행 검증한다.
- [verify-render-base0.ps1](../../Tools/regression/verify-render-base0.ps1): DX12 Editor 렌더 회귀 진입점. 별도의 파티클 수치 게이트가 필요하다.

MaterialGraphSceneRuntimeEffects는 기존 재질 효과 자원이며 새 파티클의 소유 대상이 아니다. EnginePackagingPlan의 옛 §4.1은 인계 후 본문이 없으므로 Phase 10의 상세 실행 계획은 이 문서가 소유한다.

## 10-1a 구현 및 수용 (2026-10-22)

**10-1a 완료 범위: 빈 패키지의 Debug x64 제품 포함·제외 수용.** 아래 기록은 2026-10-22 당시의 수용이며, 후속 10-1b 결과는 다음 절에서 구분한다. [검증 기록](../analysis/Phase10PackageAcceptance20261022.json)에 소스 해시·바이너리 해시·실행 결과를 남긴다.

- `Engine/Effect/Effect.vcxproj`는 정적 라이브러리다. `RenderEngine/Effects/IEffectSystem.h`의 공용 factory 등록·해제와 세션 소유권만 구현했다. 생성·갱신·기록 등 실행 API는 10-1b/c 계약을 정한 뒤 추가한다.
- `Tools/build/EffectPackage.targets`가 Editor/Player의 선택적 ProjectReference와 `/INCLUDE:ce_effect_package_anchor`를 함께 설정한다. `EngineEffectPackage=false`는 프로젝트 의존과 링크 root를 모두 제거하며 코어 C++를 조건부 컴파일하지 않는다. Effect 프로젝트는 솔루션의 Engine 폴더에 등록한다. host 참조는 선택적이며, 전체 솔루션의 제외 빌드는 Effect를 소스·archive 없는 Utility 대상으로 처리한다. 프로젝트를 실제 제거할 때에는 솔루션 항목도 함께 제거하며 앱 C++ 수정은 필요 없다.
- 패키지 존재 시 기본 포함한다. 정적 등록자는 host DLL 로드 때 factory를 등록하고 해제 때 unregister한다. 이는 한 host image의 정적 구성 경계이며 hot-load API가 아니다. 세션은 App 생성 전 초기화하고 App 종료·소멸 뒤, 공용 런타임 teardown 전에 종료한다. 초기화 실패·예외에서도 소유자가 부분 초기화를 정리한다.

| 검증 | 결과 | 관측 |
|---|---|---|
| Debug Editor 포함/제외 | 통과 | 솔루션 경유 실제 빌드, 사설 프로젝트의 scene.new/wait/quit 성공, 종료 코드 0 |
| Debug Player 포함/제외 | 통과 | 솔루션 경유 빌드, 쿠킹된 camera/light 씬, DX12 offscreen 렌더 smoke, ready=true, 종료 코드 0, 런타임 텍스트 파싱 0, atexit 도달 |
| 프로젝트·링크 경계 | 통과 | 제외 시 Effect ProjectReference 0 및 map의 anchor/EffectPackage.obj 없음. 포함 시 참조 1 및 두 심볼 존재 |
| 솔루션의 제외된 Effect 대상 | 통과 | Utility 평가·컴파일 입력 0, 직접 빌드 후 기존 archive의 수정 시각 불변 |
| 실제 세션 수명 | 통과 | 포함 시 initialized/shutdown 각각 1회, 제외 시 unavailable 1회 |
| 오류·소유 계약 | 통과 | 중복/잘못된 해제 거절, 실패·예외 정리, null factory, 재생성, unregister |
| 최적화 archive 링크 | 통과 | 실제 EffectPackage.cpp를 /O2 /GL archive로 만들고 /LTCG /OPT:REF와 anchor로 등록 유지. archive 제외도 통과. Release 제품 수용과 구분 |

재현 순서(저장소 루트 PowerShell 7):

```powershell
pwsh -NoProfile -File Tools/regression/new-effect-package-fixture.ps1 `
  -OutputDirectory artifacts/phase10-fixture-new -Configuration Debug
pwsh -NoProfile -File Tools/regression/verify-effect-package.ps1 `
  -FixtureProject artifacts/phase10-fixture-new/Project `
  -CookedPak artifacts/phase10-fixture-new/GameAssets.pak `
  -OutputDirectory artifacts/phase10-matrix-new -Configuration Debug
```

출력 폴더는 새 경로를 사용한다. fixture 작성에는 현재 AssetCooker/AssetPacker가 필요하다. 검증기는 host 빌드·의존 목록·link map·stdout/stderr·결과 JSON을 보존하며, 마지막에는 패키지 포함 빌드로 복구한다. 기존 사용자 프로젝트를 검증 입력으로 사용하지 않는다. 원본 구현 수용 산출물은 `artifacts/phase10-1a-20261022/solution-debug-matrix/result.json`이다.

## 10-1b 구현 및 수용 (2026-10-24)

Effect·Emitter·ModuleStack·ModuleInstance·입력 표현의 C++ 논리 스키마와 검증기를 구현했다. [저작 정의 계약 v1](../design/ParticleEffectAuthoringContract.md)이 안정 ID·버전·단계·고정 시계·속성 초기화/다중 writer 순서·난수 표본 신원을 소유한다. 기본 카탈로그는 11개 모듈의 **기술자**이며 모듈 계산이 구현됐다는 뜻은 아니다.

- Effect 라이브러리에 실제 정의·검증 코드를 추가했다. 폭발 예제는 테스트 fixture로 분리하고, 이를 실제 라이브러리에 링크한 네이티브 소비자로 검증한다.
- Debug·Release 각각 51개 단정이 통과했다. 정상 폭발·다중 이미터, ID를 보존한 이름 변경·재정렬, 단계/타입/버전/범위/커브/파라미터 오류 거절, 초기화 전 읽기와 비활성 writer, 결정적 난수 key의 고정 표본을 확인했다.
- 패키지 경계 회귀도 현재 바이너리로 다시 수행했다. Debug Editor/Player 포함·제외 4조합의 솔루션 빌드·실행·정상 종료와 map/참조 경계를 통과했다. 이전의 사설 camera/light 쿠킹 fixture를 재사용했으며 새 파티클 콘텐츠를 실행한 검증은 아니다.
- 파일 직렬화·마이그레이션 실행·clock 스케줄러·입자 계산·IR·그래프 편집기·GPU는 구현하지 않았다. 저장 버전/안정 ID/입력 의미를 먼저 고정한 것이며 파일 왕복과 제품 저작 수용은 10-4에서 닫는다.

```powershell
pwsh -NoProfile -File Tools/regression/verify-effect-definition.ps1 `
  -OutputDirectory artifacts/phase10-definition-new
```

소스·바이너리 신원과 검증 기록은 [10-1b 수용 기록](../analysis/Phase10DefinitionAcceptance20261024.json)에 남긴다. 아래 10-1c에서 snapshot 전달과 simulation/HDR 입력 경계를 연결했다.

## 10-1c 구현 및 수용 (2026-10-24)

성공한 공용 고정 tick을 Effect에 한 번 전달하고, GT 소유 EffectState의 불변 값 snapshot을 기존 렌더 packet과 뷰 context로 발행한다. [프레임 전달 계약](../design/ParticleEffectTransportContract.md)과 [수용 기록](../analysis/Phase10TransportAcceptance20261024.json)이 신원·수명·재현 경로를 소유한다.

- `(context epoch, slot, generation)` 핸들, 재등록·scene 전환·shutdown, 독립 epoch token, 변하지 않는 과거 프레임을 구현했다. 렌더 스레드는 컴포넌트나 EffectState 주소를 조회하지 않는다.
- 버스트는 개수만 합치지 않고 요청 당시 행렬·tick·sequence를 보존한다. journal 한도에서 명시적으로 거절하고, 실제 simulation submission 인수 확인 전에는 보존한다. 현재 HDR 노드는 이 확인을 가짜로 발행하지 않는다.
- 기존 staged contributor를 재사용했다. Host overlay 교체와 분리한 owner를 조립 시 복사하고, BeforePostProcess에서 HDR/깊이 입력과 snapshot의 기록 수명만 검증한다. 이 노드는 입자를 계산·그리지 않으며 최종 반투명/temporal 합성 위치를 확정하지 않는다.
- Debug/Release 전송·수명 56개씩과 스키마 51개씩 통과했다. Debug Editor/Player 포함·제외 4조합과 실제 view 캡처·Player scene reload·resize 후 캡처도 통과했다. Release/Shipping 제품 실행은 이번 수용에 포함하지 않는다.
- 10-1a의 최적화 standalone archive 기록은 당시 범위로 보존한다. RHI 기여를 추가한 현재 패키지는 제품 map/실행과 실제 라이브러리 소비자 검사로 검증한다.

## 10-2a 구현 및 수용 (2026-10-24)

기본 모듈과 사용자 그래프가 같은 타입 IR 컴파일러와 CPU 평가기를 통과한다. [모듈 IR 계약 v1](../design/ParticleEffectModuleIRContract.md)과 [수용 기록](../analysis/Phase10ModuleIRAcceptance20261024.json)이 실행 범위와 재현 경로를 소유한다.

- 7개 연산의 위상 정렬·타입 검사·출력 검증을 구현했다. 기본 Gravity와 별도 작성한 동등 그래프를 여러 dt/초기 속도에서 해석식과 비교한다. Drag, 속성 대입, 파라미터 override, 스택 순서·동시 출력·전체 스택 rollback도 검증한다.
- 당시 11개 기술자 중 Gravity/Drag 및 속성 대입 5개의 상수·파라미터 입력 lowering을 구현했다. 당시 난수·커브·그라데이션, burst·형상 표본·draw는 명시적으로 거절했다. 아래 10-2b-1에서 CPU 표본 입력을 확장했다. 현재 폭발 예제 전체 실행이나 GPU 일치를 주장하지 않는다.
- 실제 라이브러리를 소비하는 네이티브 IR 검사 Debug/Release 각 142개, 기존 스키마 각 51개·전송 각 56개가 통과했다. Debug Editor/Player 포함·제외 4조합과 scene reload·resize 캡처도 통과했다. Release/Shipping 제품 실행은 범위 밖이다.

## 10-2b-1 구현 및 수용 (2026-10-24)

[시뮬레이션 첫 계약](../design/ParticleEffectSimulationContract.md)과 [수용 기록](../analysis/Phase10SimulationAcceptance20261024.json)을 추가했다.

- 공통 CPU IR에서 난수·커브·그라데이션을 평가한다. 고정 난수 key에 분포 v1을 적용하며 표본 시점·입자 신원, UInt 전체 범위, 키 보간·수명 읽기를 검증한다. 별도 CPU step이 Age 증가→수명 종료→Update→위치 적분을 수행한다.
- Effect 패키지에 backend 중립 그래프 기여 함수와 최소 Slang 고정 커널을 추가했다. 초기화 시 앞의 min(requested,capacity)개를 활성화하고 Gravity→Drag·수명·이동을 실행한다. 첫 최대 용량은 4,096개다.
- CPU Debug/Release 각 274개 검사와 실제 DX12 Debug GPU 60조합·1,082,120개 성분 비교를 통과했다. 0/1/최대/초과 요청, 모듈 on/off, 정지·등속 이동·수명 경계, 완료 후 버퍼 재초기화/재사용을 확인했다. GPU validation 메시지 0개이며 잘못된 수치 기준을 실제로 거절했다.
- 기존 스키마 각 51개·전송 각 56개 Debug/Release 검사와 Debug Editor/Player 포함·제외 4조합, scene reload·resize 캡처도 통과했다. 이는 제품 경계 회귀이며 제품 파티클 재생 수용은 아니다.
- 이 GPU 경로는 일반 IR 변환이 아니라 상수 입력의 고정 커널이다. GPU 표본 입력·형상 생성·임의 모듈 순서·제품 snapshot/burst 연결·동시 프레임 자원 수명·overflow 오류 처리는 남아 있다. 기존 HDR 관찰 경로는 계속 simulation 인수 확인을 하지 않는다.

다음은 **10-2b-2: 공통 IR의 GPU 변환과 표본 입력/기본 초기화 모듈**이다. 그 뒤 제품 committed tick·snapshot·burst 인수 확인과 완료 기반 자원 수명을 연결해 씬 전환·다중 뷰에서 검증한다. 10-2 전체는 진행 중이다.

## 검증 범위와 잔여 결정

10-1a/b/c의 패키지·저작·전달 경계, 10-2a의 공통 CPU IR과 10-2b-1의 표본 입력/step·최소 GPU 수치 체인을 구현했다. 일반 IR GPU 변환·제품 파티클 실행·저작 UI·픽셀 정확성·성능과 Release/Shipping GPU/제품 수용은 미수행이다. 10-1은 경계 범위에서 완료, 10-2는 진행, 10-3~5는 대기다. Unreal과 동일한 기능 범위나 성능은 주장하지 않는다.
