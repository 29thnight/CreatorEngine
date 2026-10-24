# 기존 Scene/Entity 무렌더 실행 수용 — 2026-10-24

**별도 제한 프로필을 폐기하고 기존 SceneManager·Scene·Entity·Component·ClrHost를 재사용하는 렌더 없는 실행으로 전환했다.** Debug·Release·새 checkout의 실제 host 57건과 기존 소비자 회귀를 통과했다. 실제 네트워크 서버 제품 수용은 별도다.

[기계 판독 기록](HeadlessSceneRuntimeAcceptance20261024.json) · [실행 계약](../design/HeadlessSceneRuntimeContract.md) · [네트워크 계획](../plans/NetworkFrameworkPlan.md) · [대시보드](../RefactoringPlanDashboard.html#phase-20).

## 구현과 기존 호출자

`SceneRuntimeHeadless`는 기존 production SceneManager·Scene·Entity·Component·ScriptComponent·물리 구현을 표현 서비스 없이 컴파일한다. `Runtime::Headless::Session`은 입장·프레임·종료 순서만 조정한다. 자체 엔티티 저장소·Transform 계산·물리 바인딩·수명 큐와 HeadlessBindings 전용 ABI를 제거했다. 기존 Editor/Player도 같은 핵심 클래스와 ClrHost API45를 사용한다.

`CookedSceneLoader`는 기존 AssetCooker의 CEDO v1을 실제 factory에 복원한다. 엔티티/컴포넌트 identity·prefab/override·태그·설치된 프로젝트 layer ID·계층·sparse file slot·forward parent·UINT32_MAX 무효 참조·레거시 `m_transform`을 보존한다. 추가 CPU 컴포넌트는 reader를 등록한다. 미설치 컴포넌트·asset bundle·외부 geometry 서비스는 입장 오류이며, 조용히 버리지 않는다. 현재 host는 기본 project layer 서비스를 설치하며 알 수 없는 layer ID도 거부한다. 모든 기존 게임 씬을 그대로 지원한다고 확대하지 않는다.

기존 수명 큐·GCCE owner domain·ScriptObjectRegistry의 세대/핸들과 지연 파괴를 유지한다. 비활성 스크립트의 초기화도 기존 규칙대로 지연한다. DDOL은 실제 SceneManager의 이송을 사용한다. Editor Play snapshot과 prefab snapshot을 제거하지 않았으며 `Authoring::Document`의 중립 소유권과 backend 접근을 분리했다. Adopt/Clone은 독립된 writer를 소유한다. Unity object의 중립/저작 코드 합성도 차단해 parser 링크를 제거했다.

개별 F# 콜백 예외는 기존 일반 저작 계약대로 해당 스크립트만 비활성화한다. 배치가 정상 완료하면 물리·snapshot을 커밋한다. 배치 전체 실패 때에는 커밋하지 않는다. 검증 host는 관리 오류 기록이 있으면 정상 수명 정리를 마친 뒤 프로세스 실패로 보고한다. 전역 F# 예외 계약을 바꾸지 않았다.

## 실제 실행 결과

합성 프레임 입력 2초를 각 구성에서 실행했다. FPS 성능 측정 결과가 아니다.

| 구성 | 입력 cadence | 프레임 | F# 배치 / 커밋 / 물리 | 0틱 프레임 | 다중틱 프레임 |
|---|---:|---:|---:|---:|---:|
| Debug | 30 / 60 / 144 | 60 / 120 / 288 | 각각 120 / 120 / 120 | 0 / 0 / 168 | 60 / 0 / 0 |
| Release | 30 / 60 / 144 | 60 / 120 / 288 | 각각 120 / 120 / 120 | 0 / 0 / 168 | 60 / 0 / 0 |
| 새 checkout Release | 30 / 60 / 144 | 60 / 120 / 288 | 각각 120 / 120 / 120 | 0 / 0 / 168 | 60 / 0 / 0 |

공통 TimeSystem의 FrameCount도 실제 프레임 수와 일치한다. 모든 cadence에서 이동체 x=6.0000048m, 낙하체 y=0.4999990m, 실제 CCT x=-5.0500498m/y=1.04999995m다. F# 소비자가 1/60 dt·명령 선적용·물리 뒤 snapshot·오래된 wrapper·필드 복원·수명/정리를 검사했다. DDOL은 동일 Entity·ScriptComponent·바디 컴포넌트·전역 script handle을 보존하고, 이전 씬 handle을 stale 처리하며 SDK scene/body handle·명령 epoch를 갱신했다. pose·속도를 보존했고 초기화/시작/종료/해제는 각각 1회, 씬 추가/제거는 각각 2회였다. 120 관리 배치 중 이송 후 새 epoch의 커밋/물리 서수는 90이다.

각 구성의 실제 host 19건은 cadence 3·실제 시계·한글/상대 경로·텍스트/절단/버전 오류·sparse/forward 계층·레거시 Transform·미설치 컴포넌트·계층 순환·잘못된 body 필드·외부 geometry·F# 필드 타입 오류·틱/종료 예외·실제 DDOL·스크립트 누락을 검사했다. 주 수용은 57건이고 Product suite도 별도로 Release 19건을 다시 실행했다. 틱 콜백 예외는 10번째 호출에서 격리되며 정상 완료한 커밋/물리 10 뒤 host가 실패를 반환한다. 정상·오류 경로에서 GC 객체/루트/스크립트 핸들은 0, 물리 resource는 balanced, 생성/파괴 통지는 일치했다.

패키지 원장·native imports·최종 map·실제 로드 모듈에서 RenderEngine/Editor·DXGI/D3D/Vulkan·shader compiler·GPU PhysX·ryml/c4 의존을 차단했다. adapter/project 금지 변이 5건·DLL 규칙 7건과 금지 패키지 canary도 거부했다. 공통 헤더의 선택 서비스 타입 선언을 최종 링크 코드와 혼동하지 않는다. CLR이 사용하는 USER32/GDI 등 OS DLL의 부재를 주장하지 않는다.

## 새 checkout 재현

기준 commit은 `71504c4fd610a205c8c857538364e1d392e9e150`, checkout은 `C:/Users/idene/.codex/worktrees/shared-scene-repro/CreatorEngine`다. 명시한 source overlay 72개 파일은 원본/새 checkout의 SHA256이 일치한다. F# 도구는 별도 checkout의 `3351cbc80d7216eb10110c4c8b30dce98aef1d1f`로 고정했다.

원본 Bin/Build 산출물을 복사하지 않고 실제 native·managed fixture를 다시 빌드하고 씬을 다시 구웠다. vcpkg 설치 cache는 junction, NuGet은 전역 cache를 재사용했다. 재현 solution에서는 이번 경계와 무관한 기존 Effect 미추적 프로젝트 항목을 제외했고 direct target을 빌드했다. 전체 전이 include 지문의 raw byte 일치를 주장하지 않는다. source overlay와 actual package/import/map/프로세스 결과를 함께 기록했다.

의존 cache가 없는 clean VM 설치·Linux/컨테이너·self-contained .NET 배포는 검증하지 않았다. 설치된 .NET 10 x64와 MSVC runtime이 필요하다.

## 기존 소비자 회귀

| 회귀 | 이번 최종 소스 결과 |
|---|---|
| HeadlessHost·CreatorEditor·Player·AssetCooker native | Release 재빌드 PASS; Debug host 별도 제품 PASS |
| NetworkHandoff `All -Product` | 등록 기본 10그룹 + 제품 7그룹, 단일 실행 17그룹 PASS |
| 기존 Editor 고정틱·활성·일반 async·실패/수명 | PASS |
| 실제 명령/snapshot·Scene GC 2회·진단 JSON/PBR | Product suite PASS |
| 실제 Editor 30/60/144fps·CCT/강체 궤적·moving 144 GPU | Product suite PASS; 합성 host cadence와 구분 |
| 프리팹 override writer·snapshot deep clone | 로컬 수정 기록·갱신·값/Transform 보존 7판정 PASS |
| F# 생성·필드 보존·자동 reload·실패 compile 보존·일반 멤버 tick | 실제 Editor PASS |
| 손상된 관리 DLL의 reload 실패/복구 | 임시 F# Bobber fixture로 편집/Play identity·활성 개수 보존 PASS |
| 새 로컬 SDK·새 cooked AnimationPlayer | 실제 cooker/package·애니메이션/명령/정상 종료 PASS |
| 새 SDK F# CCT Player | 실제 고정틱/native read/write·완료 marker·정상 종료·패키지 불변 PASS |
| cooked Player parser 비호출·경계 변이 | Product 및 등록 기본 suite PASS |

추가 리로드 검사의 첫 시도는 이전 fixture DLL에 Bobber가 없어 기준 부착 단계에서 중단됐다. 임시 F# fixture를 명시적으로 빌드한 뒤 재실행했고 원래 관리 파일을 복원했다. 이전 제한 프로필의 결과 JSON과 실패 로그는 Build 증거에 보존했으며 새 실행으로 바꿔 쓰지 않았다. 기본 suite는 pwsh로, 주 host 수용 57건은 Windows PowerShell 5로 실행했다. 원격 CI 실행이나 모든 엔진/장치/soak 회귀 통과를 주장하지 않는다.

## 문서와 인계

계약·계획·인계·대시보드는 기존 Scene/Entity 재사용 구조로 갱신하고 Markdown에서 HTML을 다시 생성했다. 대시보드 기존 455개 작업의 ID/상태/공수/순서를 보존한다. 날짜가 고정된 master 감사와 과거 측정은 별도 기록으로 유지한다. 이번 소스·문서 지문과 검증 결과는 JSON 및 `Build/Verification/SharedScene20261024`에 있다.

서버 담당자는 N0 → N2/N4 → N5 → N6 → N7 → N10 순서로 착수한다. 로컬 handle·schema 1을 wire identity/serializer로 사용하지 않는다. N9 실제 서버 bootstrap·2-client 동기화·connection drain·운영 배포, 추가 서비스/provider와 soak는 별도 수용이다. 기존 Effect·모델 메타·workspace 변경은 보존했으며 이번 작업을 커밋·푸시하지 않았다.
