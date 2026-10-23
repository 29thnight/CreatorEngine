# N1 Archive·Parser 경계 계약

2026-10-22 로컬 구현 정본. [수용 기록](../analysis/ArchiveParserAcceptance20261022.md).

## 책임과 호출 경계

공통 reflgen schema는 포맷을 소유하지 않는다. Authoring archive는 텍스트 저작, Cooked archive는 CEDO v1 bounded binary를 소비한다. 동일 schema의 양쪽 왕복을 실제 production codec으로 검사한다. 이 fixture schema는 Network ID/schema 정본이나 wire serializer가 아니다.

| 소비자 | 허용 경계 | 금지 |
|---|---|---|
| SceneRuntime·Player·렌더·물리 | 검토된 로컬 Authoring/Cooked facade | 직접 ryml/YAML/nlohmann/c4 타입·include |
| 중립 schema·공개 opaque 헤더 | 표준 C++ 및 중립 타입 | 전이 include를 통한 backend 누출 |
| Network | 동결 wire 계약·중립 schema | Authoring/Diagnostics facade 및 backend 의존 |
| Utility archive 구현 | parser/backend 내부 소유 | 공개 헤더로 backend 전달 |

기존 ComponentFactory의 Authoring::NodeView, scene/prefab/material의 로컬 읽기·쓰기 facade는 유지한다. 소스 게이트의 runtimeFacadeCallers에 ParseText/File/Cooked 호출 위치를 기록한다. 개발/저작 입력의 텍스트 허용과 cooked Player의 parser 호출 0을 구분한다. 전체 runtime에서 parser 라이브러리를 물리적으로 unlink하는 작업과 Dedicated Server 제품은 N9/E6 별도 수용이다.

## 진단 출력

TemporalMotionFixture와 EnhancedPbrCapture의 직접 ryml tree 소유를 Utility의 Diagnostics::Document로 이동했다. EnhancedSceneRenderer의 기존 manifest 호출자도 Root()로 이주했다. JSON writer만 제공하며 parser API를 추가하지 않는다. backend는 DiagnosticDocument.cpp에만 존재한다.

Document는 단독 소유·이동 가능하다. Node는 빌린 핸들이므로 원래 document state가 파괴되기 전에 사용해야 한다. document 이동 후 state 주소는 유지되며 이동 대입으로 폐기된 state의 node는 사용할 수 없다. 키/문자열은 arena에 복사하고 문자열 scalar를 명시적으로 인용한다. bool·숫자와 문자열 true/123의 자료형을 보존한다. 빈 node와 범위 밖 sequence index는 실패한다.

## 실패 게이트와 실행

`verify-archive-parser-boundary.ps1 -Configuration All`은 production codec·진단 writer native Debug/Release 각각 226검사와 독립 JSON 자료형 검사, runtime 860소스의 역할별 include 그래프, 격리 변이 35건을 실행한다. 소스 scanner는 일반 C++ preprocessor가 아니며 검토 facade에서 로컬 runtime 탐색을 멈춘다. 중립/Network root는 전이 의존을 추적한다. 실제 엔진 빌드가 이를 보완한다.

`verify-net-regression-wiring.ps1`은 N1 필수 등록 삭제와 실패 종료코드 전파를 검증한다. CI는 N1 dependency를 명시적으로 설치하고 누락 시 실패한다. 원격 CI 실행은 별도다.

```powershell
./Tools/regression/run-all.ps1 -Suite NetworkHandoff -Configuration All -Product -EditorExe <새 Release Editor> -CookedStage <새 cooked 패키지 폴더>
```

기본 등록 9그룹에 N3·W4/ND-b 실제 Editor, N1 cooked Player, N1 실제 PBR 진단 소비자와 N3 cadence/moving GPU 6그룹을 추가한다. CookedStage가 없거나 현재 빌드 runtime과 다르면 Product gate는 실패한다. Player parser 호출 0·직접 backend 참조 0·패키지 불변을 확인한다. 실제 PBR gate는 manifest 타입·실제 첨부·크기·유한값·GPU validation을 검사한다. Motion fixture와 전반적 픽셀 품질은 이 gate의 통과 범위에 포함하지 않는다.
