# 엔진 배포 · Launcher · 프로젝트 관리 (PHASE 23)

- 수립일: 2026-08-24
- 재검토일: 2026-08-27 — efsw 유지 결정 반영
- 상태: **계획 수립 · 통합 구현 미착수**
- 배치: 전체 리팩터링 대시보드의 마지막 릴리스 페이즈
- 초기 추정: **43 인일**. DL0 실측과 설치 토폴로지 판정 뒤 갱신
- 제품 방향: **서명된 MSI 설치 · 버전별 불변 엔진 · Launcher가 프로젝트와 엔진 버전을 연결**

관련 정본:

- [RefactoringPlanDashboard.html](../RefactoringPlanDashboard.html) — PHASE 23 진행 상태
- [BuildPipelinePlan.md](BuildPipelinePlan.md) — 빌드·Cook·Stage·Pak·Verify 파이프라인
- [EngineLayerSeparationPlan.md](EngineLayerSeparationPlan.md) — Engine / Editor / Player 경계
- [EnginePackagingPlan.md](EnginePackagingPlan.md) — 엔진 내부 프로젝트 의존 방향. 설치 제품 계획과는 별개
- [SerializationPlan.md](SerializationPlan.md) — 저작 archive와 cooked manifest 경계
- [EditorWorkspaceRedesignPlan.md](EditorWorkspaceRedesignPlan.md) — PHASE 21 Editor shell 최종 계약
- [AudioBackendModernizationPlan.md](AudioBackendModernizationPlan.md) — PHASE 22 FMOD 은퇴·miniaudio source 통합·오디오 package gate

외부 기준:

- [Microsoft Windows Installer](https://learn.microsoft.com/en-us/windows/win32/msi/windows-installer-portal) — 설치·구성·복구·롤백을 OS 서비스에 맡기는 기준
- [WiX Toolset MSBuild](https://docs.firegiant.com/wix/tools/msbuild/) — MSI 산출물을 기존 MSBuild 계열 빌드에 넣는 후보
- [WiX 패키지 서명](https://docs.firegiant.com/wix/tools/signing/) — MSI 및 외부 cabinet 서명 절차
- [efsw 공식 저장소](https://github.com/SpartanJ/efsw) — Editor authoring root 감시의 유지 종속. 현재 vcpkg 해석 버전은 `1.6.3`

---

## 0. 결정 요약

1. **PHASE 23을 최종 제품화 페이즈로 둔다.** 개발 저장소 안에서만 실행되는 Editor를
   `MSI → Launcher → Project Descriptor → Editor` 제품 흐름으로 바꾼다.
2. 설치 제품을 둘로 나눈다.
   - `CreatorEngineLauncher.msi`: 안정적인 Launcher와 등록 정보
   - `CreatorEngine-<version>-x64.msi`: 특정 엔진 버전의 불변 payload
   Launcher는 추가 엔진 버전을 내려받아 검증한 뒤 표준 Windows Installer 승격 흐름으로 설치한다.
3. 프로젝트 정본은 폴더 이름이나 Launcher 최근 목록이 아니라 **`*.creatorproject` descriptor**다.
   내부 `projectId`가 정체성이며, 경로와 표시 이름은 바뀔 수 있다.
4. Editor는 `CreatorEditor.exe --project <절대 descriptor 경로>`로만 제품 모드에서 시작한다.
   현재 `..\..\Dynamic_CPP` 추론과 현재 작업 디렉터리 의존은 제거한다.
5. 엔진은 `%ProgramFiles%` 아래 버전별 읽기 전용 디렉터리에 두고, 프로젝트와 생성물은 사용자
   쓰기 가능 위치에 둔다. **복구·업데이트·제거는 프로젝트 원본을 수정하거나 삭제하지 않는다.**
6. 한 Editor 프로세스는 한 프로젝트만 연다. 여러 프로젝트는 여러 Editor 프로세스로 연다.
   동일 프로젝트의 두 번째 쓰기 세션은 project lock으로 막고, 읽기 전용 동시 열기는 후속 기능으로 둔다.
7. **efsw는 유지한다.** EditorAssetDatabase가 한 `efsw::FileWatcher`와 여러 `WatchID`를 소유하고,
   ProjectSession이 descriptor의 authoring root 등록·해제 수명을 공급한다. 내부 IOCP watcher를 새 제품
   정본으로 만들거나 새 process-global registry를 추가하지 않는다.
8. Launcher는 프로젝트의 `Assets`를 감시하지 않는다. v1은 명시적 create/import/remove 동작, 창 재활성화,
   Editor 종료 통지에서 descriptor와 설치 inventory를 다시 읽는다. Editor만 descriptor가 선언한 저작
   root들을 efsw에 동적으로 등록한다.
9. Player는 프로젝트 감시, Launcher 상태, efsw를 링크하지 않는다. Cooked content만 읽는다.
10. Launcher는 빌드 로직을 재구현하지 않는다. PHASE 12의 동일 orchestrator를 별도 프로세스로
    실행하고 진행률·취소·로그만 중계한다.

이 페이즈의 핵심은 설치 UI가 아니다. 다음 네 정본이 한 줄로 이어지는 것이 완료 상태다.

```text
signed MSI product
  -> installed engine version manifest
  -> *.creatorproject (projectId + required engine version)
  -> Launcher-selected CreatorEditor.exe --project <absolute descriptor>
  -> host-supplied EnginePaths + Editor-owned dynamic efsw watches
```

---

## 1. 현재 소스 기준선 (2026-08-27 재확인)

### 1.1 이미 있는 이음새

`Engine/Utility_Framework/EnginePaths.h`는 프로세스 Host가 완전히 해석한 경로를 주입하도록
되어 있다.

- `executableRoot`, `projectRoot`, `runtimeContentRoot`, `runtimeDataRoot`, `assetsRoot`
- `enableAssetAuthoring` capability
- Runtime은 이 값의 하위 경로만 파생하고 Editor/Player 여부를 스스로 추론하지 않는다는 주석 계약

따라서 런타임 전체에 Launcher를 알리는 새 전역 서비스를 만들 필요가 없다. Launcher가 descriptor를
고르고 Editor Host가 검증된 `EnginePaths`를 조립하는 경계를 확장하면 된다.

`Tools/build.ps1`도 이미 `-Project`, `-InputMode Project|Workspace|Tracked`를 받고 canonical path,
reparse point, stage 범위를 검사한다. PHASE 23은 이 검증을 버리지 않고 descriptor 입력을 받는 얇은
adapter를 추가한다.

### 1.2 설치형 제품을 막는 현재 가정

`Editor/EngineEntry/App.cpp::MakeEditorLaunchConfig`는 현재 프로젝트 root를 다음처럼 계산한다.

```cpp
(executableRoot / L".." / L".." / L"Dynamic_CPP").lexically_normal()
```

제품 설치 뒤 `%ProgramFiles%\CreatorEngine\...\CreatorEditor.exe`와 사용자 프로젝트는 나란히 있지
않으므로 이 계약은 성립하지 않는다. 또한 `Tools/build.ps1`의 Workspace/Tracked 모드는 현재 저장소의
`Dynamic_CPP`만 허용하고, `ProjectSetting` 단수 디렉터리와 프로젝트 폴더 leaf name에 의존한다.
이들은 DL1/DL8에서 명시적으로 이관할 대상이며 현재 완료된 기능으로 세지 않는다.

### 1.3 파일 감시 기준선

`Editor/EngineEntry/EditorAssetDatabase.cpp`는 현재 다음 efsw surface를 사용한다.

- 재귀 root 한 개 등록
- Add / Delete / Moved 이벤트
- watcher 소멸로 callback thread 정지
- callback에서 Asset DB의 생성·이동·삭제 처리 호출

현재 vcpkg 해석본은 `efsw 1.6.3`이며 `FileWatcher::addWatch/removeWatch`, `WatchID`, `directories()`,
`FileWatchListener::handleMissedFileActions()`를 제공한다. PHASE 23은 이 API를 교체하지 않고 여러 root의
수명·값 event queue·rescan 수렴을 Editor 경계에서 닫는다. 미배선 `EditorDirectoryWatcher` IOCP 초안은
제품 경로에 배선하지 않으며, 그 존재를 DL2 진행 또는 완료로 표시하지 않는다.

### 1.4 아직 없는 것

- Launcher executable/project
- `*.creatorproject` schema와 parser
- 제품 모드 `--project` 인자
- 설치된 엔진 inventory와 exact version resolver
- MSI authoring, product/component GUID 정책, code signing, clean-VM gate
- project session lock, migration/rollback 계약
- efsw 다중 root 등록·해제와 missed-action rescan을 묶는 Editor session 수명

즉 이 문서는 구현 현황 보고가 아니라 마지막 제품화 단계의 정본이다.

---

## 2. 제품 경계와 비목표

### 2.1 목표

- 저장소 checkout, Visual Studio, vcpkg가 없는 clean Windows VM에서 MSI 설치 후 Launcher가 뜬다.
- Launcher에서 프로젝트 생성·가져오기·열기·목록 제거·엔진 버전 변경 준비를 수행한다.
- 서로 다른 엔진 버전 두 개를 side-by-side로 설치하고 각 프로젝트가 exact version을 고른다.
- 프로젝트를 다른 디렉터리나 드라이브로 옮겨도 descriptor의 `projectId`가 유지된다.
- Editor의 asset/meta DB가 여러 저작 root를 감시하고 overflow 뒤 scan으로 수렴한다.
- Launcher의 제거·MSI uninstall·repair·engine update가 사용자 프로젝트 원본을 건드리지 않는다.
- Launcher에서 동일 빌드 orchestrator를 호출해 Game package를 만들 수 있다.

### 2.2 비목표

- 게임 배포 패키지를 엔진 MSI와 합치지 않는다. 게임 산출물은 PHASE 12의 별도 결과다.
- Launcher 안에서 여러 프로젝트 Scene/Asset DB를 동시에 열지 않는다.
- Launcher가 모든 등록 프로젝트의 `Assets`를 상시 재귀 감시하지 않는다.
- `%ProgramFiles%`의 엔진 설치물을 Editor가 즉석에서 수정하거나 self-patch하지 않는다.
- 상주 elevated Windows Service나 UAC 우회 경로를 만들지 않는다.
- 프로젝트 목록에서 제거하는 동작을 디스크 삭제와 같은 명령으로 만들지 않는다.
- project path, 폴더 leaf name, 현재 작업 디렉터리를 프로젝트 identity로 쓰지 않는다.
- 초기 릴리스에서 macOS/Linux 설치기를 함께 설계하지 않는다. descriptor와 Host 계약만 플랫폼 중립으로 둔다.

---

## 3. 프로세스와 소유권

```text
Windows Installer
├─ CreatorEngineLauncher.msi
└─ CreatorEngine-<version>-x64.msi (side-by-side)

CreatorLauncher.exe
├─ InstalledEngineCatalog        읽기: 설치 manifest/registry
├─ ProjectIndex                  최근 목록 cache, 프로젝트 정본 아님
├─ ProjectDescriptorService      명시적 동작/창 재활성화 때 *.creatorproject 재검증
└─ ProcessLauncher               정확한 버전의 Editor/빌드 도구 실행

CreatorEditor.exe --project X.creatorproject
├─ ProjectSession                descriptor + lock + migration state 소유
├─ EnginePaths                   Host가 절대 경로로 조립
├─ EditorAssetWatchSession       efsw FileWatcher + root별 WatchID + event queue
└─ EditorAssetDatabase           main-thread에서 value event 소비

Player/Game package
└─ Cooked content only           Launcher/descriptor watcher/efsw 링크 금지
```

소유권 규칙:

- Launcher는 v1에서 directory watcher를 소유하지 않는다.
- EditorAssetDatabase는 Editor 프로세스당 한 `efsw::FileWatcher`를 소유하고 root마다 `WatchID`를 보관한다.
- ProjectSession이 authoring root 집합을 공급하며 close/실패/재열기 때 해당 `WatchID`를 명시적으로 제거한다.
- efsw callback은 `DirectoryChangeEvent` 값만 queue에 넣고 Asset DB나 AudioRuntime을 직접 호출하지 않는다.
- `handleMissedFileActions()`는 해당 root의 `RescanRequired`로 승격한다.
- EditorAssetDatabase의 mutation은 watcher thread가 아니라 Editor main-thread dispatch에서 수행한다.

---

## 4. 설치·데이터 디렉터리 계약

초기 기준 layout은 다음과 같다. 실제 회사명/ProductCode/설치 scope는 DL0에서 고정하지만, 쓰기 경계는
변경하지 않는다.

```text
%ProgramFiles%\CreatorEngine\
├─ Launcher\<launcher-version>\       MSI 소유, 읽기 전용
└─ Engines\<engine-version>\          버전별 MSI 소유, 읽기 전용
   ├─ Bin\CreatorEditor.exe
   ├─ Tools\...
   ├─ Templates\...
   └─ engine.manifest.json

%LocalAppData%\CreatorEngine\
├─ Launcher\projects.json             최근 프로젝트 index/cache
├─ Launcher\Downloads\                검증 전 download staging
└─ Logs\...

<사용자가 고른 ProjectRoot>\
├─ <name>.creatorproject               프로젝트 정본
├─ Assets\                             저작 원본
├─ Packages\                           프로젝트 package 선언/소스
├─ ProjectSetting\                     현재 이름을 우선 보존; rename은 별도 migration
├─ Intermediate\                       재생성 가능
├─ Cache\                              재생성 가능
└─ Saved\                              로그·layout·backup·session lock
```

규칙:

1. MSI component는 `%ProgramFiles%`와 명시적 등록 정보만 소유한다.
2. Launcher 사용자 상태는 MSI component로 등록하지 않는다.
3. 프로젝트는 설치 디렉터리 아래 만들 수 없다.
4. uninstall custom action으로 project/cache를 재귀 삭제하지 않는다.
5. engine version 디렉터리는 설치 완료 뒤 불변이다. update는 새 version install과 project opt-in 전환이다.
6. engine inventory는 디렉터리 이름만 믿지 않고 서명/설치 등록과 `engine.manifest.json`을 교차 검증한다.
7. 부분 download/install은 최종 version 경로로 publish하지 않는다.

---

## 5. 프로젝트 descriptor 정본

기본 파일명은 `<display-name>.creatorproject`, 내용은 UTF-8 JSON으로 한다. 파일명과 폴더명은 표시 편의일
뿐 identity가 아니다. schema v1의 최소 필드는 다음과 같다.

```json
{
  "schemaVersion": 1,
  "projectId": "3f7f4ab5-5a8a-4fd8-a16f-a3a9f44f2189",
  "displayName": "My Game",
  "engine": {
    "version": "1.0.0",
    "buildId": "ce-1.0.0+20260824.1",
    "channel": "stable"
  },
  "roots": {
    "assets": ["Assets"],
    "packages": "Packages",
    "settings": "ProjectSetting",
    "intermediate": "Intermediate",
    "cache": "Cache",
    "saved": "Saved"
  }
}
```

- `projectId`는 UUID이며 복사본 생성 명령 외에는 바꾸지 않는다.
- `engine.version + engine.buildId`가 exact distribution을 고른다. `engine.version`은 자동으로
  “latest”를 따라가지 않는다. Launcher가 호환 버전을 제안할 수는 있지만 descriptor 변경은 사용자의
  명시적 migration transaction이다. 같은 version 문자열로 다른 payload를 재발행하지 않는다.
- 상대 root는 descriptor parent 기준으로 canonicalize한다.
- project 바깥 root, UNC, symlink/reparse 경계는 기본 거부한다. 외부 source root 지원이 필요하면 별도
  trust/capability와 cache scope를 설계한 뒤 schema를 올린다.
- 모르는 필드는 보존하되 모르는 `schemaVersion`은 fail closed한다.
- `ProjectSetting` → `ProjectSettings` 같은 정리는 descriptor migration 없이 암묵적으로 수행하지 않는다.
- Launcher의 `projects.json`은 descriptor path, last-opened, cached display metadata만 가진다. project가
  이동하면 다시 가져와 index를 고치며 `projectId` 충돌을 보고한다.
- `EnginePaths`는 process/project의 기본 root 주입을 유지하고, 여러 authoring root와 mount identity는
  Editor의 `ProjectSession`이 소유한다. 이를 새 process-global path registry로 만들지 않는다.

Editor 제품 실행 계약:

```text
CreatorEditor.exe --project "D:\Projects\MyGame\MyGame.creatorproject"
```

- 상대 경로와 directory-only 입력은 제품 모드에서 거부한다.
- Launcher는 `workingDirectory`에 의미를 싣지 않는다.
- Editor Host가 descriptor/schema/version/root를 검증한 뒤 `EnginePaths`를 조립한다.
- 명령행 또는 descriptor 오류는 GUI 초기화 전에 진단 가능한 exit code와 로그를 남긴다.
- 개발자 호환 모드는 별도 명시 flag로만 남기며 release build의 묵시적 `Dynamic_CPP` fallback은 없다.

---

## 6. efsw 다중 root authoring 감시 계약

### 6.1 API 방향

```cpp
struct DirectoryWatchRequest
{
    std::filesystem::path root;
    bool recursive;
    DirectoryChangeFilter filter;
    WatchConsumerId consumer;
};

class EditorAssetWatchSession
{
public:
    WatchAddResult AddRoot(DirectoryWatchRequest request); // 내부 identity는 efsw::WatchID
    void RemoveWatch(WatchToken token) noexcept;
    std::size_t DrainEvents(std::span<DirectoryChangeEvent> output);
    void Shutdown() noexcept;
};
```

구체 API는 DL2에서 컴파일 fixture로 확정한다. 다음 불변식은 바꾸지 않는다.

- 여러 root를 런타임에 add/remove할 수 있다.
- Editor 프로세스당 한 `efsw::FileWatcher`를 공유하되 root/consumer별 event stream을 구분한다.
- event는 root-relative UTF-16 path, action, optional old path, watch token, mount/consumer identity,
  monotonic sequence를 가진 값이다. 여러 root의 같은 상대 경로를 하나로 합치지 않는다.
- efsw `Add/Delete/Modified/Moved`와 `oldFilename`을 값 event로 정규화한다.
- `handleMissedFileActions()`, root invalidation, 해석 불가능한 rename은 조용히 버리지 않고 해당 root의
  `RescanRequired`를 발생시킨다.
- Add/Remove/Shutdown과 callback race에서 use-after-free, double callback, stale `WatchID` 소비가 없어야 한다.
- 이벤트 handler를 내부 lock을 잡은 채 호출하지 않는다.

### 6.2 소비자별 정책

| 소비자 | 감시 범위 | 재귀 | missed action/overflow 복구 |
|---|---|---:|---|
| Editor Asset DB | descriptor의 `roots.assets`와 필요한 package source | 예 | root scan + DB reconcile |
| Editor settings | descriptor와 settings root | 기본 아니오 | settings reload/검증 |
| Player | 없음 | - | - |

Launcher가 20개 프로젝트를 등록해도 20개의 Asset DB나 20개의 scan thread를 만들지 않는다.

### 6.3 efsw 검증 범위

대규모 범용 benchmark나 내부 IOCP 구현과의 A/B는 하지 않는다. `efsw 1.6.3`을 제품 감시 구현으로
검증하고, 다음 절대 correctness/수명/성능 예산만 닫는다.

- 기능: create/delete/rename/move-in/move-out, directory rename, Unicode, long path, 빠른 stop
- 부하: idle 10분, 1/8/32 root, 10k 파일 burst, add/remove 100회
- 측정: event 유실/중복, rescan 수렴, p50/p95 전달 지연, CPU, commit, thread/handle 수, shutdown 시간
- 통과: 명시적 `RescanRequired` 없이 event 유실 0, rescan 뒤 DB diff 0, handle/thread 증가 0
- 성능: idle CPU, 10k burst p95, commit, thread/handle 수가 DL0에서 고정한 제품 예산 안에 든다.

종속 정책은 **Editor만 efsw 허용**이다. clean build와 설치 manifest/SBOM에는 exact 버전·license를 남기고,
Player와 Launcher의 link/import/stage에는 `efsw`가 없어야 한다.

---

## 7. Launcher 기능 계약

### 7.1 프로젝트 목록

- Create: template을 임시 디렉터리에 펼치고 descriptor/schema/root를 검증한 뒤 원자적으로 publish
- Import: 사용자가 고른 `*.creatorproject`를 검증하고 index에 등록
- Open: descriptor가 요구한 exact engine version을 resolve해 Editor 실행
- Remove from list: index만 수정. 디스크 삭제 명령이 아님을 UI와 API 이름 모두에서 명확히 표현
- Reveal/Repair reference: 누락된 descriptor 또는 이동된 project를 다시 연결
- Duplicate: source project를 복사한 뒤 새 `projectId`를 발급하는 명시적 transaction

### 7.2 엔진 버전

- installed/available/repair-needed 상태를 구분한다.
- exact version 부재 시 Editor를 임의 버전으로 열지 않는다.
- 설치 또는 migration 선택을 제시하고 사용자 확인 뒤에만 descriptor를 변경한다.
- side-by-side 엔진 설치는 기존 프로젝트를 자동 전환하지 않는다.
- 제거 전 해당 버전을 pin한 프로젝트를 index 기준으로 경고하되, index가 전 세계 프로젝트 정본이라고
  가정하지 않는다.

### 7.3 실행과 session lock

- Launcher는 `CreateProcessW`에 executable과 인자 목록을 분리해 조립하고 경로 quoting fixture를 둔다.
- Editor는 `projectId` 기반 named mutex와 `Saved/Session` lock record를 함께 사용한다.
- lock record는 PID, process start identity, engine version, descriptor path, timestamp를 기록한다.
- PID 숫자만으로 stale 여부를 판정하지 않는다. 비정상 종료 뒤 복구 절차와 강제 열기 경고를 둔다.
- Launcher 종료는 이미 열린 Editor를 종료시키지 않는다.

### 7.4 빌드

- Launcher는 PHASE 12 orchestrator를 child process로 호출한다.
- project descriptor path, target, config, backend, output을 명시적으로 전달한다.
- stdout 텍스트 scraping만으로 상태를 추론하지 않도록 versioned machine-readable progress/event를 추가한다.
- cancel은 process tree와 임시 stage를 안전하게 정리하고 마지막 정상 release pointer를 보존해야 한다.
- 빌드 결과와 로그에는 projectId, descriptor digest, engine version/build ID, source revision을 기록한다.

---

## 8. MSI·업데이트·신뢰 경계

### 8.1 MSI 제품 모델

- Launcher와 엔진 version MSI는 서로 독립적으로 repair/uninstall할 수 있다.
- 동일하지 않은 MSI 산출물은 같은 PackageCode를 재사용하지 않는다.
- component GUID와 key path는 DL0에서 규칙과 검증 script를 고정한다.
- per-machine 설치를 기본 후보로 하며, 승격은 Windows Installer가 필요한 시점에만 요청한다.
- Visual C++ runtime 등 prerequisite가 실제 clean-VM에서 필요할 때만 signed bootstrapper를 추가한다.
  `.msi` 자체는 항상 독립 산출물로 보존한다.
- custom action은 최소화하고, 파일 복사·등록·복구·제거는 MSI component model로 표현한다.

### 8.2 다운로드와 업데이트

```text
channel metadata (signed)
  -> download staging
  -> size/hash/signature/product identity verify
  -> Windows Installer invocation
  -> installed engine manifest verify
  -> catalog publish
```

- transport TLS만 믿지 않고 metadata signature, SHA-256, Authenticode/MSI signature를 검증한다.
- stable/beta 같은 channel은 available version 선택 정책이지 project의 exact pin을 대체하지 않는다.
- install 실패 또는 취소 시 기존 엔진과 project descriptor는 그대로 남는다.
- engine rollback은 구버전 side-by-side 재선택이고, project migration rollback은 별도 backup transaction이다.

### 8.3 프로젝트 신뢰

상용 Launcher가 임의 project를 여는 것은 코드 실행 경계다. C#/native plugin/build hook이 있는 project는
열기 또는 build 전에 origin과 변경된 executable content를 식별해야 한다.

- 다운로드/네트워크 위치 project는 trust prompt와 zone/origin 정보를 표시한다.
- descriptor가 project 밖 executable/hook을 가리키는 schema는 v1에서 허용하지 않는다.
- native plugin ABI/version/signature 정책이 정해지기 전에는 자동 load하지 않는다.
- migration 전 descriptor와 변경 대상 파일을 backup하고 dry-run diff를 제공한다.

---

## 9. 실행 계획

모든 상태는 최초 `todo`다. 문서 작성과 미배선 watcher 초안은 구현 진행으로 세지 않는다. 합계 **43 인일**은
작업량이며 병렬화 전 달력 기간이 아니다.

### DL0 — 제품·설치·프로젝트 기준선과 실패 게이트 (P0, 2일)

- 현재 실행 경로, repo-relative 가정, build input, efsw 1.6.3 event/성능, clean-VM prerequisites를 고정한다.
- Launcher/engine MSI의 ProductCode·UpgradeCode·component GUID·install scope·signing 후보를 기록한다.
- 깨진 descriptor, 잘못된 signature, dirty uninstall을 검출하는 canary를 먼저 만든다.

**판정:** baseline artifact와 최소 하나의 의도적 실패 fixture 없이 다음 슬라이스를 시작하지 않는다.

### DL1 — 프로젝트 descriptor·identity·`--project` Host 계약 (P0, 4일)

- `*.creatorproject` schema/parser/validation과 `ProjectIdentity`를 만든다.
- Editor command line을 추가하고 검증된 root로 `EnginePaths`를 조립한다.
- 제품 모드의 `Dynamic_CPP`, CWD, 폴더 leaf-name identity 의존을 제거한다.
- 현재 `ProjectSetting`과 build adapter의 migration 경계를 문서와 fixture로 고정한다.

**판정:** 저장소 밖 Unicode/space 경로의 두 프로젝트를 같은 Editor binary가 각각 열고, 잘못된 schema와
엔진 version은 GUI 초기화 전에 실패한다.

### DL2 — efsw 다중 root·main-thread reconcile 수명 (P0, 3일)

- EditorAssetDatabase의 한 `efsw::FileWatcher`에 root를 동적으로 add/remove하고 `WatchID` mapping을 소유한다.
- callback을 값 event queue로 제한하고 `handleMissedFileActions()`를 root별 rescan/main-thread reconcile로 연결한다.
- 미배선 IOCP watcher는 제품에 배선하지 않고 build 대상에도 추가하지 않는다.

**판정:** 1/8/32 root, 10k burst, Unicode/long path, rename/move, overflow, add/remove 100회, shutdown
회귀를 통과한다. Editor package에는 pinned efsw와 notice가 있고 Player/Launcher import에는 efsw가 0이다.

### DL3 — Editor `ProjectSession`·lock·root lifecycle (P0, 4일)

- descriptor load부터 Asset DB watch 등록까지 session 소유권을 한 객체에 모은다.
- open 실패, scene 전환, 정상 종료, crash 복구의 watch/DB/lock 해제 순서를 고정한다.
- 동일 project 두 번째 writer를 막고 Launcher 종료와 Editor 생존을 분리한다.

**판정:** open/close 100회 뒤 watch/handle/thread/lock 잔류가 0이고 stale lock 복구 fixture가 통과한다.

### DL4 — Launcher 프로젝트 관리 vertical slice (P0, 5일)

- project index와 Create/Import/Open/Remove-from-list/Reveal을 구현한다.
- template 생성은 staging→validate→publish transaction으로 만든다.
- exact engine resolver와 명시적 오류/설치 제안을 연결한다.

**판정:** 두 위치의 두 프로젝트와 두 엔진 version 조합을 재시작 뒤 보존하고, 목록 제거가 project
파일 diff 0임을 자동 검증한다.

### DL5 — 버전별 불변 엔진 distribution layout·provenance (P0, 4일)

- Editor, 도구, template, runtime dependency를 version stage에 닫는다.
- `engine.manifest.json`, file digest, build ID, ABI/schema 범위를 생성한다.
- PHASE 12 산출물을 Launcher/installer가 소비하는 하나의 distribution contract로 만든다.
- PHASE 22의 pinned miniaudio source hash/license와 FMOD-free dependency audit 결과를 provenance에 포함한다.

**판정:** repo checkout과 vcpkg 없이 staged engine으로 외부 project를 열고, 누락/변조 파일은 catalog
등록 전에 실패한다.

### DL6 — WiX MSI 설치·복구·제거·side-by-side (P0, 5일)

- Launcher MSI와 versioned engine MSI를 빌드 파이프라인에 추가한다.
- install/repair/uninstall, upgrade policy, file association, Start Menu entry를 구현한다.
- project와 사용자 index/cache를 MSI component 밖에 유지한다.

**판정:** clean VM에서 silent/passive/UI 설치, repair, 두 engine side-by-side, 개별 uninstall을 통과하고
프로젝트 hash가 전 과정에서 같다.

### DL7 — 서명 update·migration·rollback·trust (P0, 4일)

- signed channel metadata와 download/hash/signature 검증을 구현한다.
- project engine 전환을 backup→dry-run→migrate→commit transaction으로 만든다.
- 실패·취소·crash에서 구버전 open 또는 backup restore가 가능해야 한다.

**판정:** 변조 package/metadata는 설치 전에 거부되고, migration 실패 뒤 descriptor/content가 이전
digest로 복원된다.

### DL8 — Launcher build/cook 연동 (P1, 4일)

- PHASE 12 orchestrator에 descriptor adapter와 machine-readable progress/cancel을 추가한다.
- Launcher build UI는 child process 상태와 artifact/log link만 제공한다.
- Workspace/Tracked의 저장소 전용 가정과 일반 Project 제품 경계를 분리한다.

**판정:** 설치된 엔진과 저장소 밖 project만으로 Game package를 만들고 Player smoke/manifest verify를
통과한다. Launcher와 CLI 결과 digest가 같다.

### DL9 — clean-VM E2E·다중 프로젝트/버전·watch soak (P0, 5일)

- install→create/import→open→asset change→build→repair→update→rollback→uninstall 전체를 자동화한다.
- Unicode, space, long path, secondary drive와 지원 대상으로 판정된 OneDrive/UNC 조건을 검증한다.
- 두 project 동시 Editor, 32 watch root, burst/overflow, 비정상 종료 soak를 실행한다.

**판정:** release matrix 전부 통과, project 원본 diff 0, 잔류 engine process/handle 0, DB full scan diff 0이다.

### DL10 — 릴리스 운영·진단·최종 외부 종속 감사 (P1, 3일)

- projectId/engine build ID가 포함된 crash/log/support bundle과 개인정보 제외 규칙을 만든다.
- license/EULA, third-party notices, SBOM, symbol/provenance 보존, signing key 운영 절차를 닫는다.
- Player/Editor/Launcher/MSI dependency와 설치 결과를 최종 감사한다.

**판정:** release checklist와 rollback runbook이 다른 clean VM에서 재현되고, Editor의 pinned efsw
버전/license가 SBOM과 일치한다. Player/Launcher의 efsw, 전체 제품의 FMOD dependency와
`miniaudio.dll`은 0이다.

---

## 10. 의존 관계와 배정

PHASE 23은 대시보드상 마지막이다. 단, 선행 연구와 기반 구현을 마지막까지 미룬다는 뜻은 아니다.

```text
PHASE 12 B2 ───────────────┐
PHASE 17 D5 ────────────┐  │
PHASE 21 W8 ─────────┐  │  │
PHASE 22 AU8/AU9 ─┐  │  │  │
                 v  v  v  v
DL0 -> DL1 -> DL3 -> DL4 -> DL5 -> DL6 -> DL7
          \-> DL2 --/        \-----------> DL9 -> DL10
                    DL4 + PHASE 12 B2/B3/B4/B5 -> DL8 --/
```

- **DL0~DL4**는 PHASE 23의 선행 slice로 먼저 진행할 수 있다. 프로젝트 정본과 watcher 경계는 다른
  작업에도 필요하다.
- **DL5** distribution closure는 PHASE 12 B2/B3/B4의 안정된 산출물 계약과 PHASE 22 AU8의
  FMOD-free runtime stage를 요구한다.
- **DL8/DL9 release gate**는 PHASE 12 B5 game CI, PHASE 17 D5 cooked manifest, PHASE 21 W8 Editor,
  PHASE 22 AU9 audio device/performance/soak 회귀가 닫힌 뒤 판정한다.
- PHASE 21의 UI shell을 Launcher에 복제하지 않는다. Launcher는 별도 작고 안정적인 제품 UI다.
- EnginePackagingPlan P1~P5는 내부 링크/프로젝트 경계이고, 이 문서의 MSI/distribution ownership을
  대체하지 않는다.

---

## 11. 최종 완료 기준

다음을 모두 증명해야 PHASE 23을 완료로 표시한다.

| 영역 | 완료 수치/증거 |
|---|---|
| 설치 | clean VM에서 서명 MSI install/repair/uninstall 성공, 저장소·VS·vcpkg 불필요 |
| 버전 | 엔진 두 버전 side-by-side, 각 project exact pin 재시작 후 동일 |
| 프로젝트 | 서로 다른 위치의 2개 이상 project create/import/open/build 성공 |
| 경로 | 제품 코드의 묵시적 `..\..\Dynamic_CPP`와 CWD 기반 project 선택 0 |
| 보존 | repair/update/rollback/uninstall 전후 project source digest diff 0 |
| 감시 | Editor efsw 32 root, add/remove 100회, 10k burst 후 event 또는 명시적 rescan으로 DB diff 0 |
| 수명 | Editor open/close/crash 반복 뒤 watcher thread/handle/session lock 증가 0 |
| Launcher | 등록 project의 asset-wide recursive watch 0 |
| Player | Launcher/Editor watcher import 0, `efsw.dll` import 0 |
| Editor 종속 | pinned efsw version/hash/license가 설치 manifest·third-party notice·SBOM과 일치 |
| Audio | FMOD source/link/stage/PE import 0, `miniaudio.dll` 0, WAV/MP3/FLAC package smoke 통과 |
| 빌드 | Launcher와 CLI package manifest/content digest 동일 |
| 신뢰 | MSI/PE/channel metadata 서명 검증, 변조 fixture 전부 거부 |
| 운영 | SBOM, third-party notices, crash/log bundle, rollback runbook 재현 |

---

## 12. 위험과 기각한 대안

| 후보 | 판정 | 이유 |
|---|---|---|
| 한 Editor 프로세스에서 여러 project 동시 관리 | 기각 | 전역/싱글턴 상태와 Asset DB 정본이 교차 오염된다. 프로세스 격리가 더 명확하다 |
| Launcher가 모든 project Assets 상시 감시 | 기각 | UI 목록 갱신에 불필요한 IO/handle/권한 비용이며 Editor 책임을 복제한다 |
| 내부 IOCP watcher로 efsw 교체 | 기각 | 현재 요구는 efsw의 다중 watch와 missed-action callback으로 충족 가능하며 별도 Windows transport의 수명·회귀 비용이 더 크다 |
| root마다 `efsw::FileWatcher` 생성 | 기각 | root 증가가 watcher/thread 증가로 직결된다. 한 Editor 소유 watcher에 여러 `WatchID`를 둔다 |
| 현재 single-root 사용부를 Launcher에 복사 | 기각 | Launcher는 asset 감시가 필요 없고 Editor authoring 책임을 중복한다 |
| polling을 fallback 정본으로 사용 | 기각 | idle IO와 latency가 root/file 수에 비례한다. overflow는 명시적 rescan으로 복구한다 |
| `%ProgramFiles%` 설치물을 Launcher가 직접 patch | 기각 | 부분 update, 권한, repair 정본이 갈라진다. 새 불변 version을 설치한다 |
| project를 MSI component로 등록 | 기각 | uninstall/repair가 사용자 원본에 소유권을 갖게 된다 |
| “latest” engine 자동 전환 | 기각 | migration과 재현 가능한 build를 깨뜨린다. exact pin과 명시적 transaction을 쓴다 |
| Launcher 안에 별도 build pipeline | 기각 | CLI/CI/GUI 결과가 갈라진다. PHASE 12 orchestrator 하나만 둔다 |
| 상주 elevated update service | 초기 릴리스 기각 | 공격면과 운영 복잡도가 크다. 표준 MSI 승격을 필요 시점에 사용한다 |

---

## 13. 갱신 규칙

- 대시보드의 DL0~DL10과 이 문서의 상태를 함께 바꾼다.
- “코드 작성”, “MSI 생성”, “Launcher 화면 표시”를 단독 완료로 세지 않는다. 각 slice의 **판정**을 통과해야 한다.
- 설치/업데이트 실패 판단은 project source digest와 Windows Installer 결과/로그를 함께 남긴다.
- watcher 판단은 callback 개수만 보지 않고 최종 Asset DB scan diff와 handle/thread 수명까지 본다.
- efsw 버전 변경이나 교체는 별도 결정으로만 열고, vcpkg lock/프로젝트 링크/Editor 배포 DLL/license/SBOM과
  Player/Launcher PE import 0을 함께 감사한다.
- 오디오 종속 감사는 PHASE 22 AU8/AU9의 source/project/stage/PE import 결과와 pinned miniaudio provenance를 재사용한다.
- 경로·설치 scope·schema·migration 결정을 바꾸면 이유와 기존 project 호환 결과를 이 문서에 남긴다.
