# CreatorEngine 구현 문서

이 문서 트리는 **master `493536e300e274cbf59989cdd62b20310c8ae64f` (2026-10-15)**의 소스와 실제 호출 경로를 기준으로 읽는다. 문서 재작성 과정에서는 엔진 빌드, Editor/Player 실행, GPU 측정이나 이미지 비교를 수행하지 않았다. 코드에 경로가 있다는 사실과 해당 환경에서 실행 검증을 통과했다는 사실은 다르다.

## 읽는 순서

1. [기술 안내](TechnicalGuide.md): Editor/Player 진입, 공통 초기화, 씬 갱신, 렌더 제출, 자산·스크립트·패키징 경계
2. [문서 지도](RefactoringPlanDashboard.html): 분야별 문서와 현재 구현을 찾는 출발점. 과거 단계의 완료율을 제품 지원율로 환산하지 않는다
3. [웹 문서 운영](WEBSITE.md): React API 레퍼런스, 정적 문서, GitHub Pages 배포의 실제 연결
4. [기여 절차](../CONTRIBUTING.md), [참여 기준](../CODE_OF_CONDUCT.md): 저장소 전반의 기여 규칙

## 문서의 상태를 해석하는 법

- **구현됨:** 선언만이 아니라 소비자·호스트·프로젝트 편입 경로를 소스에서 확인한 기능. 실행 성공·성능·모든 backend 지원을 뜻하지 않는다
- **제한적 구현:** 특정 backend, 기기 capability, 입력 형태 또는 모드에서만 연결된 경로. fallback과 오류를 함께 읽는다
- **계획/미연결:** 설계 의도, 선언, 실험 도구가 있어도 제품 호출 경로로 연결되었다는 근거가 없는 범위
- **역사 측정:** 기록 당시 commit·설정·장면·장치에 귀속되는 수치와 이미지. 현재 HEAD의 성능이나 품질 판정으로 승격하지 않는다
- **미검증:** 이번 재작성에서 실행하지 않은 빌드·런타임·GUI·GPU·패키징 검증

`plans/`나 `archive/`라는 경로 이름, 과거 PHASE 이름, 완료 체크박스는 현재 구현의 근거가 아니다. 각 문서의 현재 계약과 남은 요구사항을 우선한다. 열려 있는 다른 브랜치의 구현도 이 master 스냅샷에 포함하지 않는다.

## 실제 진입점

| 관심사 | 코드의 출발점 | 다음 문서 |
|---|---|---|
| Editor 호스트 | [App.cpp](../Editor/EngineEntry/App.cpp), [EditorMain.cpp](../Editor/EngineEntry/EditorMain.cpp) | [Editor 작업 공간](plans/EditorWorkspaceRedesignPlan.md) |
| Player 호스트 | [PlayerApp.cpp](../Player/PlayerApp.cpp), [PlayerMain.cpp](../Player/PlayerMain.cpp) | [Player 표시](design/PlayerNativePresentation.md) |
| 공통 부팅·종료 | [EngineBootstrap.h](../Engine/RuntimeHost/EngineBootstrap.h) | [계층 경계](plans/EngineLayerSeparationPlan.md) |
| 시뮬레이션 순서 | [RuntimeFrame.cpp](../Engine/SceneRuntime/RuntimeFrame.cpp) | [씬 수명](plans/archive/SceneGraphRedesignPlan.md) |
| 렌더 조립 | [EnhancedSceneRenderer.cpp](../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp) | [렌더 흐름](analysis/RenderPipelineFlow.md) |
| 그래프 실행 | [EnhancedRenderGraph.cpp](../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp) | [RenderGraph 계약](plans/RenderGraphDependencySchedulingPlan.md) |
| C# 표면 | [ScriptCore](../ScriptCore/) | [스크립트 표면](plans/ScriptSurfacePlan.md) |
| 빌드 도구 | [Program.cs](../BuildTool/Program.cs) | [빌드 파이프라인](plans/BuildPipelinePlan.md) |

Editor는 DX12 renderer와 ImGui host presentation을 연결한다. Player는 `RuntimeSettings`의 backend 선택을 받아 native presentation을 연결한다. 두 호스트가 같은 시뮬레이션 함수를 사용한다는 이유로 편집 기능이나 backend 수용 결과까지 동일하다고 보지 않는다.

## 파일 종류와 보존 범위

| 경로 | 읽는 방법 |
|---|---|
| `design/` | 코드가 실현하는 데이터·수명·오류 계약과 아직 연결되지 않은 의도를 구분한 상세 설명 |
| `plans/` | 현행 구현에 대비한 범위·잔여 요구사항. 과거 일정이나 공수는 현재 완료의 증거가 아님 |
| `plans/archive/` | 이전 주소를 유지하는 주제별 현재 계약/승계 안내. 오래된 계획 원문은 Git 이력에서 확인 |
| `analysis/`의 Markdown/HTML | 분석 주제의 현재 구현·관측 지점·검증 한계를 새로 설명 |
| `analysis/`의 측정 JSON, PNG, GIF | 원래 bytes를 보존한 역사 자료. 최신 실행 결과로 재생성하지 않음 |
| `design/`의 SVG·PNG | 당시 UI/스타일 구상·미리보기. 현재 창의 스크린샷이나 기능 완료 증거가 아님 |
| `resource/` | 2026-09-14 Editor 캡처. 웹 소개가 사용하지만 최신 UI를 보증하지 않음 |
| `api/reference.json` | 웹 UI가 직접 읽는 선별 API 설명 데이터. 측정 JSON과 달리 코드에 맞춰 갱신하는 문서 |
| `src/`, `index.html`, `api/index.html`, `style.css`, `site-assets/` | 문서를 표시하는 웹 소스와 정적 리소스 |
| `.nojekyll` | 정적 배포용 표식 |

## API 생성기 주의

[generate_scriptbinder_docs.py](generate_scriptbinder_docs.py)는 저장소 루트의 `ScriptBinder/**/*.h`를 정규식으로 읽고 루트 `API_DOCS/`에 Markdown을 쓰는 과거 도구다. 이 기준 commit에는 그 입력 디렉터리가 없으며, 스크립트는 헤더가 없으면 메시지를 출력하고 반환한다. 현재 `ScriptCore`나 웹의 `api/reference.json`을 생성하지 않는다. 이 스크립트를 실행해 현재 C# API 문서가 갱신된다고 안내하지 않는다.

## 갱신할 때

문서를 바꿀 때는 public 선언, 구현 본문, 호출자, 프로젝트/호스트 편입을 함께 확인한다. 지원 조건·실패·취소·교체·종료 경로를 생략하지 않는다. 실행 결과가 필요하면 정확한 commit과 명령, 환경, 결과 파일을 남긴다. 기존 기록의 통과 표시를 새로운 검증으로 복사하지 않는다.

## 감사 범위와 재검사

[재작성 감사 보고](DocumentationAudit.md)는 기준 파일 전체의 처리 결과와 검증 범위를 설명한다. [파일별 근거 목록](DocumentationCoverage.json)은 문서별 확인한 행동·소스·한계와 원본/결과 blob을 기록한다. 정적 무결성 검사는 `python docs/check_documentation.py`로 실행한다. 이 검사는 링크·anchor·hash를 확인하며 문서 의미의 정확성이나 엔진 실행 성공을 자동으로 증명하지 않는다.
