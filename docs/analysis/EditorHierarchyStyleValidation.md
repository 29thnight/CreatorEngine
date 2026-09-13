# Hierarchy S&Box 스타일 적용 및 검증

- 날짜: 2026-09-12
- 범위: PHASE 21 W2의 Hierarchy 시각 스타일 후속 슬라이스
- 기준: 사용자가 제공한 S&Box Hierarchy 스크린샷
- 백엔드: DX12. Vulkan은 앞선 사용자 결정에 따라 보류한다.

## 적용

| 요소 | 적용 내용 |
|---|---|
| 패널 / 목록 | 외곽 `#323534`, 목록 `#171818`, 교차 행 `#1E201F` |
| 선택 | 가로 전체 회색 `#494D4C`, 선택된 행의 hover `#535856` |
| 아이콘 / 이름 | 일반 오브젝트 `#7095D0`, 선택된 오브젝트 `#A8C9FA`. 비활성 텍스트는 기존 회색 유지 |
| 상단 | 사각 `+` 버튼과 검색 아이콘을 포함한 단일 Search 입력칸. 간격 3 logical px |
| 생성 메뉴 | `+`와 기존 우클릭 메뉴가 같은 생성 함수 사용. Empty / Light / Camera / 기존 UI 생성 경로 연결 |
| 스크롤 | 트리만 별도 child 영역에서 스크롤하며 상단 도구는 고정 |

색상은 첨부 화면을 바탕으로 정한 근사값이며 S&Box의 공식 상수는 아니다.
Material Symbols의 GameObject / Prefab 의미 아이콘과 기존 씬 그룹 표시를 유지한다.
기존 탭 표시와 저장된 Hierarchy 창 ID를 사용하며 이번 작업에서 탭 닫기 기능은 추가하지 않는다.

## 구현 경계

- `EditorStandardWindows.h`의 배경 선언과 `HierarchyStyleScope`로 계층창 범위에만 색/간격을 적용한다.
- 기존 `TreeNodeEx`, 재귀 탐색, 검색 필터, 선택 및 drag-drop 경로를 사용한다.
  트리는 새 child의 ID 범위에 들어가므로 이전 프레임/실행의 개별 펼침 상태 이주를 보장하지 않는다.
- 오브젝트 단축키는 트리에 포커스가 있고 텍스트 입력 중이 아닐 때만 처리한다.
  Delete는 최초 누름만 소비해 검색어 편집이 오브젝트 삭제로 전달되지 않게 했다.
- 원래 동작이 없던 UI Button 생성 항목은 비활성 표시한다.

## 실제 검증

| 검사 | 결과 |
|---|---|
| VS18/v145 Debug Editor 빌드 | 성공. 기존 `LNK4229 /DELAYLOAD:vulkan-1.dll` 경고 1건 |
| 선택 / Inspector 연동 | Main Camera와 Preview Node 14 선택 시 회색 행 및 Inspector 갱신 확인 |
| `+` → Create Empty | Entity 생성, 씬 객체 수 3→4 확인 |
| 검색 | `l` 입력 시 Directional Light만 매칭. 기존 Main Camera 선택 유지 |
| 검색어 Delete | 입력칸의 `l`만 제거. 객체 수 4와 Main Camera 신원/Transform 유지 |
| GUI drag-drop | Entity의 부모가 씬 루트 `@1:0:1`에서 Directional Light `@1:2:1`로 변경 |
| 제품 `undo` 명령 | Entity의 부모가 `@1:0:1`로 복원 |
| 스크롤 / 패널 높이 변경 | 임시 Empty 16개를 추가해 아래쪽 행 표시, 상단 `+`/Search 고정 및 높이 확장 확인 |
| 전역 테마 복원 | 선택 상태의 `editor.theme`: clean / themeMappingMatches / geometryMatches 모두 true |
| DX12 배율 gate | 사용자 배율 100→150→100% 3회 기동, 64개 검사 통과 |
| 각 기동 selftest | 311개 검사, 실패 0 |

촬영은 사용자 배율 150%, OS DPI 150% 환경이다. 배율 gate의 OS DPI는 150%로 유지됐고
폰트 크기는 24→36→24px였다. 실제 모니터 이동 검증이나 전체 폭/DPI 조합 검증은 아니다.
샘플 씬의 생성/이동은 저장하지 않았으며 검증용 Editor는 종료했다.
프로젝트 설정과 imgui.ini는 검증 전 원본과 바이트 단위로 동일하게 복원했다.

화면: [적용 결과](../../Artifacts/phase21-hierarchy-style/hierarchy-applied.png),
[검색](../../Artifacts/phase21-hierarchy-style/hierarchy-search.png),
[스크롤](../../Artifacts/phase21-hierarchy-style/hierarchy-scrolled.png).
명령 결과 JSON 및 빌드/배율 로그: `Artifacts/phase21-hierarchy-style/`.

W2 전체는 `progress`다. 모든 생성 종류·키보드 탐색·다중 선택·disabled 상태의 전수 조작,
대규모 계층의 성능 gate, Vulkan은 이번 검증 실적에 포함하지 않는다.
