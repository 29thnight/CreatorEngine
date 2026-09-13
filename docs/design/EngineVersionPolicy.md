# CreatorEngine 버전 정책

- 결정일: 2026-09-13
- 상태: **정책 확정 · 개발 배포 경로 적용 중 · 제품 Launcher/MSI/공식 릴리스 검증은 후속 구현**
- 적용 계획: [엔진 배포 · Launcher · 프로젝트 관리](../plans/EngineDistributionAndLauncherPlan.md) — PHASE 23 DL5·DL6·DL10

## 1. 결정

Windows OS와 API의 구분을 참고해 **제품 세대, 기능 릴리스, 내부 엔진 빌드, API 계약, 배포 채널**을 독립적으로 관리한다.
새 제품 계열의 표시 이름은 **CreatorEngine 2**로 한다. 제품 세대 숫자와 내부 엔진 버전은 독립적이다.

| 구분 | 표시 예시 | 책임 |
|---|---|---|
| 제품 이름 | `CreatorEngine 2` | 사용자가 식별하는 제품 세대 |
| 기능 릴리스 | `26H2` | 기능 배포 계열과 최초 배포 반기 |
| 엔진 빌드 | `2.0.1000.12` | 내부 버전, 기능 기준 빌드, 누적 수정판 |
| API 계약 | `Script API 24` | 해당 호출 경계의 호환성 |
| 배포 채널 | `Preview` / `Stable` | 배포 상태와 버전 제안 대상 |

표준 표시 예시는 다음과 같다.

```text
CreatorEngine 2
Version 26H2 · Preview
Engine Build 2.0.1000.12
Script API 24
```

한 줄 표시는 `CreatorEngine 2 · Version 26H2 (Build 2.0.1000.12)`로 한다.
채널과 API 계약은 상세 정보에서 각각 확인할 수 있어야 한다.
**`26H2`, `1000`, `12`는 규칙 설명용 예시이며 실제 출시 일정이나 발행된 빌드를 뜻하지 않는다.**
첫 배포의 릴리스명과 빌드 번호는 릴리스 준비 시 부여한다.

## 2. 번호 부여 규칙

### 제품 세대와 기능 릴리스

- 제품 세대는 제품 방향을 기준으로 사람이 결정한다. 기능 추가나 내부 변경만으로 자동 증가하지 않는다.
- 기능 릴리스는 `YYH1` / `YYH2`로 표시한다. `H1`은 1~6월, `H2`는 7~12월이다.
- 릴리스명은 해당 기능 계열의 최초 정식 배포 시점을 기준으로 확정한다. Preview의 목표 반기가 바뀌면 정식 배포 전 표시를 갱신한다.
- 이 표기는 반기마다 출시하겠다는 일정 약속이 아니다. 정식 배포 뒤 수정판은 기존 기능 릴리스명을 유지한다.

### 내부 엔진 빌드: `Major.Minor.Build.Revision`

| 필드 | 증가 기준 |
|---|---|
| `Major.Minor` | 장기간 유지하는 런타임 기준 버전. 내부 세대나 기준 계약의 변경에 따라 명시적으로 결정하며 제품명·연도와 자동 연동하지 않는다. |
| `Build` | 새로운 기능 기준선을 발행할 때 더 높은 번호를 부여한다. 로컬 컴파일 횟수나 Git 커밋 수로 계산하지 않는다. |
| `Revision` | 동일 기준선의 배포 수정판마다 증가한다. 새 기준 빌드에서는 `0`부터 시작한다. |

- 빌드 번호는 기능 릴리스가 바뀌어도 누적 증가시킨다. 이미 발행한 번호를 재사용하지 않는다.
- 동일 기준선의 Preview 수정판에도 Revision을 부여한다. Stable 전환 자체는 숫자 필드의 의미를 바꾸지 않는다.
- 버전 비교는 네 정수의 순서로 한다. 문자열 사전순, 제품 세대, `YYH1/H2`, 채널명으로 최신 바이너리를 판정하지 않는다.
- Windows 파일 리소스에 기록하는 각 숫자는 16비트 무부호 정수 범위 안에 있어야 한다. 범위를 넘겨 잘라내거나 재사용하지 않는다.

다음은 번호 변화의 예시이며 릴리스 예약표가 아니다.

| 배포 | 기능 릴리스 | 엔진 빌드 |
|---|---|---|
| 기능 계열 최초 정식 배포 | `26H2` | `2.0.1000.0` |
| 첫 누적 수정판 | `26H2` | `2.0.1000.1` |
| 다음 누적 수정판 | `26H2` | `2.0.1000.2` |
| 다음 기능 기준선 | `27H1` | `2.0.1200.0` |

### 배포 채널과 발행 단위

- 사용자 표시 채널은 `Preview`와 `Stable`로 한다. 배포 metadata의 값은 `preview` / `stable`로 기록한다.
- 채널은 네 자리 숫자와 별도 필드로 유지한다. Alpha/Beta/RC를 제품 버전 접미사로 사용하는 이전 제안은 채택하지 않는다.
- 릴리스 번호 부여는 사람의 명시적 릴리스 작업이다. 커밋마다 버전 파일을 수정·커밋하는 자동화는 복원하지 않는다.
- 발행된 버전의 payload는 불변이다. 같은 번호로 수정·재빌드한 파일을 덮어쓰지 않는다.
- 동일 payload를 Preview에서 Stable로 승격하는 것은 채널 metadata의 변경이다. 파일 변경이 필요하면 새 버전을 발행한다.
- Editor·Player·엔진 도구는 같은 엔진 배포 버전을 공유한다. Launcher는 독립적으로 버전을 관리한다.

## 3. API와 데이터 호환성

제품명이나 내부 엔진 버전만으로 API 호환성을 보장하지 않는다. 각 호출 경계의 계약을 명시한다.

- 기존 호출을 유지하는 확장과 호환되지 않는 변경을 구분한다. 호환되지 않는 인터페이스는 새 계약으로 제공하고 지원 계약을 기록한다.
- API 번호가 더 크다는 사실만으로 구버전 계약 지원을 추론하지 않는다. 지원 목록이나 실제 계약 검증이 근거다.
- 현재 `Script API 24`는 네이티브와 C# 사이의 API 테이블 계약이다. 모든 엔진 API를 대표하는 번호로 확장하지 않는다.
- 기존 API 테이블 버전 검사와 불일치 거부를 유지한다. 이전 계약 지원은 adapter/협상과 회귀 검증이 구현된 경우에만 선언한다.
- native Core/plugin ABI, Script API, 씬·프리팹·프로젝트 schema, cooked/pak 포맷은 각각의 경계에서 관리한다.
- API 지원 범위가 같아도 데이터 포맷과 패키지 호환성은 별도로 확인한다. 이관이 필요한 변경은 릴리스 기록에 명시한다.

## 4. 프로젝트 고정과 빌드 식별

- PHASE 23의 프로젝트 descriptor에서 `engine.version`은 **네 자리 전체 엔진 빌드 문자열**이다.
- `engine.version + engine.buildId`로 정확한 배포를 선택한다. 제품 세대·기능 릴리스·채널은 이를 대체하지 않는다.
- Launcher는 업데이트를 제안할 수 있지만 패치 수정판도 사용자의 명시적 전환으로 적용한다.
- `buildId`는 배포 식별자다. Git SHA, 미커밋 변경 여부, 빌드 구성, 파일 digest는 별도 추적 정보로 기록한다.
- Git SHA만으로 바이너리 동일성을 보장하지 않는다. 로컬 개발 빌드는 공식 발행본과 구별해서 표시한다.
- 과거 `v1.55(Portable)` 등을 포함한 Git 태그는 그대로 보존한다. 새 발행 태그는 `v<Major.Minor.Build.Revision>` 형식을 사용한다.
- 플랫폼·아키텍처·패키지 형태는 배포 파일명이나 metadata로 기록한다. 네 자리 버전에는 섞지 않는다.

## 5. Windows 파일 속성과 MSI

Windows 파일 속성의 숫자 버전에는 네 자리 엔진 빌드를 사용하고, 제품 표시 문자열에는 제품명·기능 릴리스를 담는다.
MSI `ProductVersion`은 앞의 세 자리만 비교하므로 엔진 버전을 그대로 잘라서 사용하는 것으로 Revision 수정판을 구별할 수 없다.

DL6에서 installer용 버전과 package identity의 매핑을 정하고, 서로 다른 Revision을 설치·선택·복구·제거할 수 있는지 검증한다.
MSI용 숫자 범위와 버전 비교 규칙은 별도로 준수하며, installer의 제약 때문에 엔진 버전이나 프로젝트의 정확한 배포 지정을 축약하지 않는다.

## 6. 현재 상태와 적용 완료 기준

2026-09-13 소스 확인 기준:

- 버전 정본은 루트 [EngineVersion.json](../../EngineVersion.json)이다. 현재 값은 제품명 `CreatorEngine 2`, 미지정 기능 릴리스, `0.0.0.0`, `preview`, `localDevelopment=true`인 **미발행 로컬 개발 상태**다.
- 명시적 버전 편집 시 [update-engine-version.ps1](../../Tools/distribution/update-engine-version.ps1)이 native 상수와 Windows 버전 리소스를 동기화한다. 일반 빌드는 일치 여부만 검사하며 커밋/컴파일마다 번호를 올리지 않는다.
- [MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)의 About와 [DumpHandler.h](../../Engine/Utility_Framework/DumpHandler.h)의 크래시 보고는 같은 native 상수와 배포 metadata를 사용한다. EXE/DLL 숫자 파일 버전과 `--engine-info`도 같은 네 자리 엔진 빌드를 기록한다.
- [ScriptApiVersion.h](../../Engine/Utility_Framework/ScriptApiVersion.h)의 `CreatorScriptApiVersion`은 `24`다.

개발 배포 스크립트는 별도 UUID `buildId`와 payload digest를 기록하고 정확한 배포 pin을 검사한다.
설정별 개발 pin은 정식 `.creatorproject` 구현을 대체하지 않는다. 사용법과 적용 범위는
[사전 빌드 개발 환경](../../Tools/distribution/README.md)에 기록한다.
공식 번호 발행, Launcher 구현, MSI 설치 검증과 Stable 지정은 아직 완료되지 않았다.

| 기존 작업 | 이 정책의 적용 및 판정 기준 |
|---|---|
| PHASE 23 DL5 | 제품·릴리스·네 자리 빌드·채널·API/ABI/schema·build ID를 일관되게 기록한다. 같은 배포의 About·진단·파일 속성·manifest 값이 일치하고 descriptor가 정확한 배포를 선택한다. |
| PHASE 23 DL6 | installer 매핑을 확정한다. Revision만 다른 두 배포의 설치·선택·복구·개별 제거가 충돌 없이 동작하고 프로젝트 원본이 보존된다. |
| PHASE 23 DL10 | 번호 발행·채널 승격·불변 payload 운영을 릴리스 절차에 포함한다. Stable은 지원 환경의 빌드, 프로젝트 열기·저장, 스크립트 실행, 게임 패키징·실행 및 관련 회귀 결과를 근거로 지정한다. |

위 항목은 기존 DL 작업의 구체화다. 정책 문서 작성으로 구현 작업을 done 처리하거나 기존 공수·진행률을 올리지 않는다.
단일 버전 정본은 `EngineVersion.json`으로 확정했으며 installer 매핑 방식은 DL6 구현 시 결정한다.

## 7. 검토한 대안

- **SemVer 단일 제품 버전:** API 호환성 표현에는 유용하지만, 이번 결정에서는 제품 세대·기능 배포·수정 빌드·API 계약을 독립적으로 표현하기로 했다.
- **컴파일마다 Build 증가:** 로컬 반복 빌드와 제품 기능 기준선이 섞이므로 채택하지 않는다. 개발 빌드 식별은 별도 추적 정보가 담당한다.
- **제품 세대와 API 번호 동기화:** 제품 이름 변경과 호출 계약 변경의 주기가 다르므로 채택하지 않는다.

## 8. 참고

- [Microsoft OS 버전](https://learn.microsoft.com/en-us/windows/win32/sysinfo/operating-system-version) — 제품 이름과 내부 OS 버전의 구분
- [Windows 릴리스 정보](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information) — 기능 릴리스와 OS 빌드 표시
- [API 계약 지원 조회](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.metadata.apiinformation.isapicontractpresent) — 계약별 지원 확인
- [Windows VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource) — 파일·제품의 숫자 버전 리소스
- [MSI ProductVersion](https://learn.microsoft.com/en-us/windows/win32/msi/productversion) — 세 자리 비교와 installer 버전 제약

Windows의 개념을 참고한 CreatorEngine 자체 정책이며 Microsoft 제품 전체에 적용되는 단일 번호 부여 규칙을 주장하지 않는다.
