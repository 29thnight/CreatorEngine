# F# 공통 관리 런타임·일반 멤버 생명주기 수용 — 2026-10-23

> 선택적 생명주기·빈 훅 없는 생성 템플릿·VS Code 도우미는 [후속 수용 기록](FSharpOptionalLifecycleAcceptance20261023.md)을 따른다. 본문의 공통 실행기 이주 당시 로그·소스 신원은 유지한다.

## 현재 범위

Script API45로 공통 관리 실행기를 F#으로 이주하고, 컴포넌트 생명주기 11종을 일반 멤버의 정적 등록으로 바꿨다. [현재 계약](../design/FSharpScriptingContract.md)을 따른다. 제품 회귀의 최종 실행 결과와 소스 신원은 이 문서와 동명 JSON에 기록한다.

| 역할 | 현재 구현과 경계 |
|---|---|
| 공통 관리 실행기 | ScriptRuntime의 F#. 팩토리·인스턴스·지연 멤버십·생명주기·구독 정리·재개 큐·접촉 라우팅·collectible 로더·애니메이션/BT 소유 목록·진단 호출·GC 정책 |
| 네이티브 바인딩 | C# ScriptCore. 함수 포인터·ABI 값 형식·공개 native wrapper·native payload 파싱·전달 진입점. 별도의 스크립트 실행 목록은 없음 |
| 사용자 저작 | F# 일반 인스턴스 멤버. 생성기가 RegisterAllLifecycle의 typed callback을 만들고 실행기가 호출 |

네이티브는 고정틱마다 한 번 경계를 넘는다. 생성 콜백은 저작 타입으로 cast하여 일반 멤버를 직접 호출하며, 없는 훅은 null이다. Component 기반 타입에 virtual 생명주기를 두지 않는다. private/static/generic/curried/overloaded·잘못된 인자/반환형은 CEF1020 오류다. 필드/메시지 접근자의 내부 override는 인스펙터/직렬화 ABI용이다. AniBehavior/BT 노드의 별도 형식 계약은 컴포넌트 생명주기 11종과 구분한다.

## 보존한 동작

- 60Hz·최대 catch-up4, 명령 적용→관리 tick→물리→접촉→성공 snapshot 순서와 프레임 Pre/PostPhysics를 유지한다.
- 초기화 시도와 성공, 씬 진입 성공, Begin 성공을 구분한다. 성공한 여는 훅에만 닫는 훅을 짝지으며 예외가 난 스크립트만 비활성화한다.
- 파괴/리로드는 상태와 목록을 먼저 분리하고 구독을 정리한다. 재진입 등록·제거·Clear가 현재 순회를 오염시키지 않게 멤버십 큐와 generation을 유지한다.
- Entity 세대와 PhysicsBody/CCT NativeInstance 검사는 기존 바인딩을 사용한다. DDOL의 씬 이탈은 접촉 관찰만 초기화하고 컴포넌트 수명을 끝내지 않는다.
- 새 DLL은 버리는 collectible 컨텍스트에서 먼저 검증한다. 실패하면 기존 코드·인스턴스·factory·callback 표를 보존한다. 성공한 교체에서는 이전 세대의 callback·instance·구독·재개 참조를 해제한다.

## 실제 제품에서 수정한 누락

F# 공통 실행기의 FSharp.Core가 Managed 루트에 복사되지 않아 native 초기화가 실패했다. ScriptRuntime의 CopyLocalLockFileAssemblies와 SDK CoreFiles를 연결했다. 게임의 private FSharp.Core는 Scripts 아래에 별도로 둔다.

hostfxr가 ScriptCore를 별도 component 컨텍스트에 올리는 환경에서 Default로 실행기를 적재하면 계약 타입이 갈라졌다. 실제 ScriptCore의 컨텍스트에 엔진용 FSharp.Core와 공통 실행기를 적재하도록 수정하고, Default가 아닌 엔진 컨텍스트 초기화를 독립 회귀에 추가했다. 초기화 예외도 native 로그에 남긴다.

Editor 생성 템플릿은 member 구문으로 이주했다. 저작 회귀는 시작 전에 독립된 빈 카탈로그를 만들고 종료 시 기존 GameScripts 출력을 복원하여, 이전 성공/실패 실행의 DLL에 의존하지 않게 했다.

기반 virtual의 타입 추론이 사라지면서, 예외만 던지던 실패 fixture 두 곳이 generic 반환형으로 추론됐다. 검사를 완화하지 않고 이주한 제품 스크립트에 single 인자와 unit 반환형을 명시했다. 초기화/Begin 실패·정리 예외·자기 비활성화·일반 await/worker 거부 회귀를 유지한다.

Debug에서는 일반 out-튜플·커리드 헬퍼·값 형식 비교에 의존하던 접촉 핫 경로가 추가 할당 검사를 통과하지 못했다. 명시적 out 조회·튜플 인자의 CLR 메서드·형식이 확정된 Equals·인덱스 순회로 바꾸고, Debug/Release 양쪽에서 기존 0바이트 기준을 그대로 통과했다. 스크립트/애니메이션/BT의 반복 조회도 명시적 out 경로로 정리했다.

## 검증 기록

| 검사 | 결과 |
|---|---|
| FCS 일반 멤버 분석 | 63건 통과. 11종 탐지·잘못된 시그니처/접근성/중복 거절·상속 포함 |
| 생성 코드 | 39건 통과. factory와 callback 표 대응·일반 멤버 직접 실행·없는 tick의 null callback 포함 |
| unmanaged Bootstrap → F# 배치 | Release/Debug 각12건 통과 |
| collectible loader | 50건 통과. private FSharp.Core·실패 보존·성공 교체·이전 ALC 수거 |
| 실제 F# 수명·고정틱 | 26건 통과. 별도 엔진 컨텍스트·예외·비활성·재진입·정리 포함 |
| 실제 F# 접촉·수명 | Debug/Release 각각76건 통과. 반복 route/read/clear 추가 할당0 bytes |
| BuildTool | 175건 통과. 공통 F# 런타임 의존성 목록 포함 |
| Release 네이티브 호스트 | Editor·Player·AssetCooker·AssetPacker API45 빌드 통과 |
| Editor 저작 | 생성·명시 순서·자동 reload·필드37 유지·실패 시 이전 코드/인스턴스 보존·수정 후 고정틱37·정상 종료 통과 |
| 자동 저작·컴포넌트 브라우저 | 실제 Editor 자동 저작87건, 브라우저/재시도422건 통과. 종료 후 설정 복원 |
| 실패·정리·일반 await 제품 회귀 | F# 7종을 실제 Editor와 cooked Player에서 실행. Player 2,000프레임·패키지 불변·정상 종료 통과 |
| CCT·DDOL Player | 캐릭터 이동12건·DDOL 이송8건 통과. 패키지244파일 불변·정상 종료 |
| 기존 저작 계약 비교 | F# 컴포넌트53종·직렬화 필드34개의 이름/종류/기본값, Ani1/BT3 카탈로그 일치 |
| 통합 패치 | 고정 baseline의 깨끗한 별도 checkout에 34경로 적용·파일 hash 일치·공통 실행기 Release 빌드 통과 |
| 신규 cooked Player | API45, 실제 CCT 이동0.65000004m, 고정틱30, 패키지 파일/hash 불변과 정상 종료 통과 |
| 등록 NetworkHandoff 전체 | 관리/네이티브9 + 제품6 = 15그룹 통과. Debug/Release 매트릭스 포함 |
| 실제 30/60/144fps DX12 | 29.9999 / 59.9940 / 144.0416fps. 동일 tick CCT·강체 상대 궤적 차이0. Scene/Game 캡처4회·canary17 통과 |
| moving 0틱 GPU | 실제 sourceEngineFrame과 연결된 readback 통과. 진단 캡처의 cadence는 정상 pacing 판정과 구분 |

근거: 도구 관리 검사 · 로컬 기록, 사이트 미포함 (`../FSharp_Tool_Pakage_for_CreatorEngine/artifacts/managed-runtime-validation-20261023/verification.json`), Editor · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Editor-a0b6c30fbaa0487bbae1eef52fd2cb56/result.json`), Player · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Player-c0103530102f437c880908112e4917ac/result.json`).

추가 제품 근거: NetworkHandoff 15그룹 · 로컬 기록, 사이트 미포함 (`Build/Verification/NDA/Suite-89a4cc6a934b41d5b4976a05fd931173/result.json`), 30/60/144fps · 로컬 기록, 사이트 미포함 (`Build/Verification/N3Pacing/Editor-0c0ebbc98b47411fb19427a470655ce2/result.json`), moving GPU · 로컬 기록, 사이트 미포함 (`Build/Verification/N3Pacing/Editor-1201263533c441dbb66fbfa6ff345d23/result.json`).

저작·수명 근거: 자동 저작 · 로컬 기록, 사이트 미포함 (`Build/AutomaticScripts-Release-8f365a67/result.json`), 컴포넌트 브라우저 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharpRuntimeMigration/ComponentBrowser/summary.json`), 실패·정리 Editor · 로컬 기록, 사이트 미포함 (`Build/Verification/CSharpFixedUpdate/Editor-a2b84cec39a146f39eb819aa19134546/result.json`), 실패·정리 Player · 로컬 기록, 사이트 미포함 (`Build/Verification/CSharpFixedUpdate/Player-4194662c3afd47939eeb13f0b564ca6d/result.json`), CCT·DDOL · 로컬 기록, 사이트 미포함 (`Build/Obj/Phase19Player/run-355e4ea0ec2946fd905778bcb1d6274f/result.json`), 계약 비교 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Compatibility-613a214b40d14ec9ad23f659ababc8e9/result.json`). 기존 호출자의 로그·결과 경로에 남은 CSharpFixedUpdate 이름은 호환성을 위해 유지한 이름이며, 이 실행의 저작 코드는 F#이다.

## 수용의 한계

NativeAOT·Shipping·다른 장치/플랫폼·IDE breakpoint·장시간 reload와 일반적인 성능 예산을 수용한 기록은 아니다. 공통 런타임의 구현 언어 변경만으로 프레임 성능 개선을 주장하지 않는다. 관리 호스트의 100,000회 조회는 direct/helper 각각9.6MB, cache 읽기0 bytes·native callback1회였으며 실제 프레임 측정과 구분한다.

이전 단계에서 남은 전체 C# fixture의 F# 이주와 미디어를 포함한 오디오 제품 수용은 별도다. 렌더 없는 서버 링크·N9 Dedicated Server 제품 수용도 별도다. 두 저장소의 변경은 로컬 작업본이며 커밋·푸시·공개 패키지 게시를 수행하지 않았다.
