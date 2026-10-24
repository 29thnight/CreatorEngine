# F# 단독 저작·OnTickSimulate 수용 — 2026-10-23

> API44 단계의 실행 기록이다. 이후 공통 관리 런타임 F# 이주·일반 멤버 정적 생명주기 등록(API45)으로 전환했다. 현재 신원과 수용은 [API45 기록](FSharpManagedRuntimeAcceptance20261023.md)을 따른다.
## 판정

**F# 단독 저작·고정 tick, 실제 30/60/144fps와 moving 0틱 GPU gate를 수용했다. 기존 C# 전체 fixture의 F# 이주는 진행 중이다.** ScriptCore는 엔진 내부 C# 바인딩이며 사용자 저작·Editor 생성/감시·배포 컴파일러는 F# 경로다. 이전 OnUpdate 이름을 제거하고 OnTickSimulate(dt), Script API44로 바꿨다. 구형 호스트와 혼합할 수 없다.

[계약](../design/FSharpScriptingContract.md) · [기계 판정·소스 hash](FSharpScriptingAcceptance20261023.json) · [도구 저장소 작업본](https://github.com/29thnight/FSharp_Tool_Pakage_for_CreatorEngine/blob/3351cbc80d7216eb10110c4c8b30dce98aef1d1f/README.md)

## 구현

- C++ bind→Bootstrap→관리 배치→Component와 모든 기존 OnUpdate 호출자/프로브를 OnTickSimulate로 이주했다. 60Hz/최대 catch-up4, 물리 앞 호출·세대·Enabled·예외 격리·성공 snapshot 의미는 유지한다. 새 lifecycle enum 슬롯은 추가하지 않는다.
- FCS lifecycle 제외 표·정적 registry·필드 접근자·샘플을 이주하고 SimulationScope/Scope.Delay 기반 F# simulation CE와 전용 실행/벤치마크 구현을 제거했다. 과거 측정 raw 파일은 역사적 자료로 남긴다.
- 샘플은 C# GameScripts를 참조하지 않는 F# primary GameScripts.dll이다. 기존 혼용 loader fixture는 호환 회귀일 뿐 제품 저작 경로가 아니다. FSharp.Core/private dependency를 같은 collectible ALC에 두고 failed reload의 이전 코드 보존을 유지한다.
- Editor의 생성 template·파일 분류/drag·감시·빌드와 배포 fsc/생성기를 연결했다. compile.order를 명시 순서로 사용한다. 순서 누락·중복·경로 이탈·소스 집합 불일치를 검사한다.
- SDK compiler가 쓰는 FSharp.Core(compiler private)와 게임 참조/배포용 10.0.112를 분리했다. FSharp.Core와 얇은 F# Runtime을 ManagedFiles에 명시하여 실제 Player 패키지에서 유실되지 않게 했다. 배포 fsc 응답 파일 문법과 경로 구분자 비교도 실제 실행으로 수정했다.
- 네이티브 타입은 기존 generation/instance 기반 wrapper를 직접 소비한다. F# helper는 조회 결과를 voption으로 감싸며 별도 mirror 상태·수명·tick reflection을 만들지 않는다.

## 실제 실행

| 검사 | 결과 |
|---|---|
| FCS / generator | 47 / 36 통과. AniBehavior·BT 정적 factory 포함 |
| F# 생성 타입 → unmanaged Bootstrap → 실제 관리 배치 | Release 11 / Debug 11 통과 |
| 관리 loader | 50 통과. F# sole primary·reload·private FSharp.Core·ALC 수거 포함 |
| 적용/스테이징 workflow | 9 통과. 기존 로컬 변경 보존·정확한 재적용·Scope 파일 삭제 포함 |
| 로컬 NuGet 3종 | 0.2.0-alpha.1 pack/내용 검사 통과. 게시 안 함 |
| 깨끗한 pinned checkout | 패치 check/apply 및 전체 after hash 일치 |
| BuildTool | 174 통과. F# 순서·경로·패키지 의존성 누락 회귀 포함 |
| NetworkHandoff 전체 등록 | Debug/Release 관리·네이티브 9 + 제품 6 = 15그룹 통과. 엔진의 모든 제품 회귀를 뜻하지 않음 |
| 실제 Editor 저작 | F# 생성·명시 order 추가·자동 reload·필드37 유지·컴파일 실패 시 이전 코드/instance 보존·수정 후 재생 tick38·정상 종료 통과 |
| 신규 Release cooked Player | F# 단독 registry, 30 fixed ticks, dt0.016666668, 실제 CCT 이동0.65000004m·정상 종료·패키지 파일/hash 불변 통과 |
| 실제 30/60/144fps DX12 | 29.9936 / 60.0095 / 144.4394fps. 동일 tick CCT/강체 상대 궤적 차이 0, 보간·GPU silhouette·Scene/Game 전환 4회와 mutation canary 17종 통과 |
| moving 0틱 GPU | 실제 zero-tick sourceEngineFrame과 GPU readback 연결 통과. 진단 캡처 구간의 목표 cadence 수용과는 구분 |
| F# fixture 비교 | 컴포넌트 53종·직렬화 필드 34개의 이름·형식·초기값, Ani 1종·BT 3종 카탈로그 일치 |
| 자동 저작 회귀 | F# 생성·수정·이름 변경·삭제·실패 복구 87건 통과 |
| 컴포넌트 브라우저 | 418건 통과. 실패한 명시 부착 요청을 파일 감시가 덮어쓰던 재시도 경합 수정 포함 |

제품 기록: Editor · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Editor-7f22602157d14902b6888773f5ad809f/result.json`), Player · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Player-fc249bf2aff6449e9674043b657cf7d7/result.json`), 30/60/144fps · 로컬 기록, 사이트 미포함 (`Build/Verification/N3Pacing/Editor-3224fde089564552b106a4cc990f35e7/result.json`), moving GPU · 로컬 기록, 사이트 미포함 (`Build/Verification/N3Pacing/Editor-35205d8791914dbc842fde39538aa4eb/result.json`), NetworkHandoff 15그룹 · 로컬 기록, 사이트 미포함 (`Build/Verification/NDA/Suite-4e86efd52e394c0599e8174c63b8d063/result.json`), 카탈로그 비교 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Compatibility-03cc2e799f93490f99c05ef86eae6887/result.json`). 각 결과는 해당 실행 당시 바이너리 신원을 기록한다.

## 성능 판정

100,000회 관리 호스트 반복에서 Transform 조회는 direct/helper 모두12,800,000 bytes(128B/조회), cache LocalPosition 읽기는 추가 할당0 bytes였다. 캐시 읽기당 native callback1회, 추가 Exists 조회0회였다. helper가 기존 wrapper 할당을 제거하거나 타이밍을 개선한다고 판정하지 않는다. 초기/필요한 재바인딩 때만 조회하고 hot tick에서 cache를 재사용한다. PhysicsBody/CCT의 instance ID 검사는 제거하지 않는다.

144fps 목표의 동등 작업량 비교는 F#123.55fps, C# control124.19fps였으며 같은 native runtime hash였다. 둘 다 ±5% 수용에 실패했다. 단일 표본으로 성능 동등성을 증명하지도, F#만의 회귀라고 단정하지도 않는다. 앞선 빌드/Player 동시 실행 및 추가 native-read 계측 표본도 실패 기록으로 보존한다. 허용 오차를 완화하지 않았다.

이후 실제 profiler에서 Game 모드에도 숨은 Scene 뷰를 함께 렌더링하는 것을 확인했다. 보이는 뷰만 제출하도록 수정하고 Scene→Game→Scene→Game GPU 캡처로 재개 동작을 검증했다. 수정 후 30/60/144fps gate가 허용 오차 변경 없이 통과했다.

moving capture의 원인은 실제 0틱 프레임에서만 시도하던 render-control try-lock이었다. busy 응답 이후의 짧은 양보 창과 다음 0틱 프레임이 어긋났다. 적격 0틱 프레임 안에서 진단 readback 제어권을 확보하도록 바꾸고 GPU sourceEngineFrame으로 실제 0틱 프레임을 재검증했다. 진단 캡처는 stall을 허용하며, 이 구간의 실측 약120fps를 정상 144fps cadence 통과로 사용하지 않는다.

## 가독성과 이주 범위

스크립트와 내부 C# 바인딩은 공백 외에도 구조를 정리했다. N3 검증은 명령 접수·commit·snapshot 유지 단계로 나눴고, 오디오 반복은 이름 있는 상태와 재생/중지 메서드로 분리했다. ABI 필드는 기존 순서를 유지하며 한 줄씩 선언하고 포인터·출력 인자·실패 경로를 명시했다. 생성 접근자 이름은 원본 스크립트를 드러내며, 생성기에서 factory·접근자 구획을 유지한다. 상세 기준은 [계약](../design/FSharpScriptingContract.md)을 따른다.

실제 오디오 자산의 스크립트를 F#으로 이주하고 GUID를 유지했다. 기본 프로젝트에는 해당 오디오 fixture의 미디어가 없으므로 실제 100회 오디오 재생 완료로 판정하지 않는다. 현재 비교 gate의 F# 컴포넌트는 53종이며, 아직 F# 대응 파일이 없는 C# 소스는 19개다. B2 partial probe·contact stream·일부 UI/curve 및 이들을 복사·수정하는 기존 제품 호출자의 이주와 실행은 남아 있다.

## 남는 범위

전체 fixture 이주 완료로 판정하지 않는다. C# production compiler 선택지는 제공하지 않는다. 실제 IDE breakpoint/VSIX·NativeAOT·Shipping/다른 장치·장시간 reload·remote CI·일반 성능 예산은 별도다. 두 저장소는 로컬 미커밋 상태이며 푸시하지 않았다.

재실행: Tools/regression/verify-fsharp-editor-authoring.ps1, verify-fsharp-player.ps1 -EngineDistribution <새SDK>, verify-n3-pacing-product.ps1 -Rates 30,60,144 및 -MovingGpuCapture -Rates 144. CSharpControl switch는 비교용 회귀 옵션이며 사용자 저작 compiler가 아니다.
