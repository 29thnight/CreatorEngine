# PHASE 21 W1 — Material Symbols 제품 적용

2026-09-11. 작업 시작 HEAD `3efbb23f`와 작업 트리 기준.
[정본 계획](../plans/EditorWorkspaceRedesignPlan.md) ·
[선정 근거와 W7 썸네일 계약](EditorIconSelectionStudy.md).

## 변경

- 공통 패널·도구·검색·재생·상태·유형 아이콘을 Google Material Symbols Outlined로 교체했다.
  의미가 맞지 않았던 Select/Scale/Orthographic/Search/Controller 상세도 새 역할 매핑을 사용한다.
- `EditorIcons.h`는 `MaterialSymbols.json`의 **59개 의미 역할 / 54개 고유 글리프**를 제공한다.
  문자열을 조합하는 라벨은 컴파일 시 생성하고 정적 수명을 갖는다. 새 아이콘 매크로는 도입하지 않았다.
- 공식 commit `40a7a292a79d9394157e1ea24f83d52d5e17c556`의 폰트에서
  `opsz=20, wght=400, FILL=0, GRAD=0`을 고정한 **7,920바이트 static TTF**를 배포한다.
  ASCII·ligature lookup은 포함하지 않으며 본문 Inter와 한글·작은 글씨 폰트에 같은 아이콘을 병합한다.
  원본/결과 SHA-256, 도구 버전, 변경 사항과 Apache 2.0 라이선스를 함께 보관한다.
  일반 빌드는 네트워크나 폰트 생성 도구를 요구하지 않는다.
- FA6 헤더와 내장 압축 블롭을 제거했다. 서로 겹치는 코드포인트를 두 폰트에 동시에 병합하지 않는다.
  기존 창 8개의 저장 ID 바이트는 유지하고 표시 라벨만 변경했다.
- **Inter 충돌 수정:** Inter 4.1에도 선택한 문자 번호 9개가 있다. 문자 존재 검사만으로는
  Play/Pause/Stop·더하기/빼기·Profiler·폴더 등이 Inter의 다른 모양으로 나오는 것을 잡지 못했다.
  본문·한글·작은 글씨·시스템/기본 fallback의 `GlyphExcludeRanges`에서 UI 사설 문자 영역
  U+E000–U+F8FF를 한 범위로 제외해
  Material Symbols가 해당 번호를 담당하게 했다. `iconSourcePolicyValid`도 실제 등록 설정을 검사한다.
  처음에는 역할마다 범위를 넣어 119개 값이 됐고 ImGui의 최대 64개 제한으로 Debug 시작 시
  assertion이 발생했다. 이를 3개 값의 영속 배열로 수정하고 컴파일 시 제한 검사도 추가했다.
  이 실패 실행은 `theme-debug-final/`에 보존하며 수정 후 결과와 구분한다.
- 실행 기능이 없던 Live Code 버튼과 빈 정렬 공간을 제거했다. Collider debug 버튼은 유지한다.
  CoreCLR 자동 변경 감지 기능 자체를 이번 작업에서 구현한 것은 아니다.
- Content Browser는 종류별 공통 PNG 로딩을 제거하고 Material Symbols 유형 아이콘을 그린다.
  타일의 기존 ID·클릭/팝업/drag-drop 경로를 유지하며 keyboard navigation을 활성화했다.
  브라우저 타일과 재질 선택 타일의 크기는 사용자 배율·DPI에 따라 계산한다.
  Camera/Light 월드 표식은 기존 전용 이미지·렌더 자원 수명 경로를 유지한다.
- `editor.theme`가 창 라벨뿐 아니라 적재된 각 본문 폰트의 의미 역할 전체를 검사하고
  `iconRoles` / `missingIconRoles` / `iconSourcePolicyValid`를 보고한다. 테마 회귀 게이트도 실제 Material 폰트 경로와
  역할 전체의 누락 0을 요구한다.

## 검증

아래 산출물은 `Artifacts/phase21-material-symbols/`에 있다.

| 검사 | 결과 | 증거 |
|---|---|---|
| 원본 codepoint ↔ C++ 매핑 ↔ 실제 TTF cmap·outline | 59역할 / 54글리프, 누락·빈 outline 0 | `font-integrity.json` |
| 기존 창 저장 ID 비교 | 8개 모두 `3efbb23f`와 동일 | `legacy-id-check.json` |
| 초기 Debug 테마·폰트·배율 | 6기동 / 115검사 통과. Inter 겹침은 존재 검사로 잡히지 않아 이후 별도 수정 | `theme-debug/` |
| 수정 후 Debug x64 빌드 | 오류 0 / 경고 0 | `debug-final-build.log` |
| 수정 후 Release x64 빌드 | 오류 0 / 기존 LNK4229 경고 1 | `release-final-build.log` |
| 수정 후 Debug DX12·Vulkan | 6기동 / 121검사 통과, 모든 실행 정상 종료 | `theme-debug-verified/` |
| 수정 후 Release DX12·Vulkan | 6기동 / 121검사 통과, 모든 실행 정상 종료 | `theme-release-verified/` |
| 창 배치 회귀 | ini fixture 6종 / 108검사 통과 | `workspace-debug/` |
| 수정 후 Debug 사용자 배율 100·150% 추가 실행 | 역할 59 / 누락 0 / source policy valid, 종료 코드 0 | `user100.jsonl`, `user150.jsonl` |
| 배포된 Debug·Release TTF | 두 구성 모두 소스 SHA-256과 일치 | `MaterialSymbols.provenance.json`의 결과 해시 |
| ImGui obsolete surface | 943 source / 27 call site / 위반 0 | `verify-imgui-obsolete-surface.ps1` |
| 최종 정적 검사 | `git diff --check`, 프로젝트 XML, 회귀 스크립트·대시보드 문법 통과, 제품 FA 소비자 0 | 현재 작업 트리 |

폰트 전체 역할을 배포 TTF에서 직접 렌더한 `material-symbols-shipped.png`도 확인했다.
이는 폰트 모양과 역할 매핑의 확인용이며 실제 에디터 화면 캡처는 아니다.
실제 창 캡처 시도 `user100.png`·`user150.png`는 검은 화면으로 나와 시각 통과 증거에서 제외한다.
이후 실제 화면 검증은 별도 캡처 도구로 수행했다. 아이콘은 표시되지만 DPI 전환 후 렌더링 문제를 발견해
당시 완료 판정은 보류했다. 이후 DX12 수정·검증과 Vulkan 보류 결정에 따른 현재 판정은 아래에 기록한다.

테마 게이트는 각 backend에서 사용자 배율 100→150→100%로 재기동하며,
관측 OS DPI 150%에서 본문 크기 24→36→24px를 확인한다.
설정과 ini는 실행 전 바이트로 복원했다. 창 배치 게이트는 창 ID 변경 직후 실행한 결과이며,
이후의 문자 제외 범위 수정은 저장 ID나 도킹 경로를 바꾸지 않는다.

## 후속 판정과 남은 범위

- 실제 OS DPI·모니터 경계 검증에서 발견한 DX12 Scene 표시 지연은 수정 후 Debug/Release 실기 검증이 통과했다.
  [DX12 수정 검증](EditorW1Dx12ResizeValidation.md)에 증거를 기록했다.
  2026-09-12 사용자 결정으로 Vulkan 대응은 별도 보류하며 **W1은 DX12 기준 `done`**이다.
  Vulkan 경계 resize 후 device loss는 미수정·미재검증이다.
- **W7 비동기 썸네일:** 현재 타일은 유형 아이콘까지 구현했다. 메시·텍스처의 실제 썸네일
  요청/생성, 캐시·무효화·완료 게시 및 Ready 결과로의 교체는 아직 구현하지 않았다.
- 이 작업 중 별도 변경된 `App.cpp`·`ImGuiHost.cpp`와 기존 `reflect_golden.yaml`은 보존했다.
  빌드·실행 결과는 해당 작업 트리를 포함한 통합 결과다.
