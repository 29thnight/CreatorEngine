# Content Browser 배치와 Scene 시작 탭

2026-09-12. 사용자 제공 S&Box Asset Browser 및 CreatorEngine 구형 브라우저 스크린샷을 기준으로 수정한다.

## 변경

- 배율을 반영하는 디렉토리 선호 폭, 드래그/키보드 조정·초기화와 좁은 창 폴더 팝업.
- 오른쪽 New, 뒤로/앞으로/상위, breadcrumb·조상 메뉴·경로 입력/복사, 통합 검색/지우기.
- 하위 폴더+자산 표시, 이름순 정렬, 유형 필터, 타일/목록 전환과 타일 크기.
- 공통 폴더 메뉴와 EditorAssetDatabase 새 폴더 쓰기. 기존 자산 선택·파일 열기·drag payload 유지.
- 모든 창의 첫 Begin 이후 Scene을 한 번 선택. 기존 ini 도크 배치를 보존한다.

## 검증

검증 산출물: `Artifacts/phase21-content-browser/`.

| 검증 | 결과 |
|---|---|
| VS18 / v145 / x64 Debug Editor 프로젝트 | 통과. 최종 로그 `build-final-verified.log` |
| 기존 도킹 ini의 시작 선택 | Scene 표시. Game 수동 선택 유지 후 종료·재시작하면 Scene으로 시작 |
| 트리·경로·검색 | Animation 선택과 경로 바 동기화, 검색 `c`로 두 모델 중 한 모델 표시 |
| 탐색·생성 | New에서 테스트 폴더 생성·경로 이동, Back/Forward 왕복 및 빈 폴더 메시지 확인. 테스트 폴더 제거 |
| 자산 표시 | 폴더 아이콘, 타일/목록 전환, 모델 목록 선택→Inspector Import Settings 연결 확인 |
| 트리 선호 폭 | 키보드 조절 및 설정 저장·재시작 복원 확인 |
| 좁은 창 | 760×600 physical px 창(가용 본문 약 320 logical px), 두 줄 도구 모음·폴더 팝업·팝업 내 이동 확인 |
| 자체 검사 | `editor.selftest` 640 checks / 0 failures |
| DX12 배율 | 시작 사용자 배율 100→150→100%, OS DPI 150% 관측, 3회 시작·64 checks PASS |
| 도킹 설정 회귀 | `verify-editor-workspace.ps1`: 현재·손상·legacy ini 6 fixtures / 108 checks PASS. 손상 fixture의 기존 undocked 기준을 0으로 이주했다는 뜻은 아님 |
| 정적 검사 | `git diff --check`, dashboard JavaScript 구문, ImGui obsolete 검사 953 sources / 28 call sites / 0 violations |

분할선 입력은 제품의 `DirectorySplitter` 처리 블록을 그대로 추출한 `splitter_probe.cpp`에서 실제
ImGui 프레임·마우스·키 이벤트로 추가 검사했다. 선호 폭 220→drag 300→release 300→방향키 두 번 316,
PASS (`splitter-probe.txt`). 설정 저장소만 테스트용으로 대체했다. 운영체제 창 입력을 대체하는 자동화 도구가 아니다.

> **2026-09-17 후속:** 실제 마우스 끌기는 `Tools/regression/verify-content-browser-splitter.ps1` 이 운영체제 메시지 경로로 네 배율에서 검증한다. 아래 실패는 보내는 쪽 스레드가 DPI 를 몰라 좌표가 모니터 배율(1.5)만큼 늘어난 탓이었을 가능성이 크다 — 같은 날 (476,1224) 로 보낸 좌표가 ImGui 에 (714,1836) 으로 닿는 것을 관측했다. 상세는 계획서 §W2-B.

마우스 UI 자동화의 drag 요청에서는 실제 폭 변화를 관측하지 못했다. 따라서 **실제 창에서의 마우스 드래그
검증은 미확인**이며, 위 ImGui 입력 검사와 실제 키보드 조절/저장 복원 결과를 구분한다. 에셋의 기존 drag
payload 경로는 유지했지만 이번 실행에서 drop 소비자까지 재검증하지 않았다. 물리 모니터 간 DPI 이동도 하지 않았다.

확인 화면: `browser-final.png`, `list-selection.png`, `narrow-layout.png`, `narrow-folders.png`.
검증 전 설정과 도킹 ini는 원본 바이트로 복원했고, 테스트로 실행한 Editor는 종료했다.

## 2026-09-12 후속: 디렉토리 트리와 목록 여백

- 분할선과 오른쪽 도구 모음 사이 10 logical px, 목록 안쪽 6px 여백을 적용했다.
  트리를 표시할 수 있는 폭 계산에서도 추가 여백을 차감한다.
  이후 창 전체 여백 축소 요청에 따라 목록·트리 안쪽은 2px로 줄였다.
  현재 검증은 [Scene crop·창 여백](EditorSceneCropValidation.md)에 기록한다.
- 트리 안쪽 여백 6px, 단계별 들여쓰기 20→14px, 행 높이 18px와 행 사이 1px를 적용했다.
  실제 프로젝트 이름의 머리행과 Assets 하위 트리를 표시한다.
- 트리 폴더는 16px 슬롯에 투명 배경의 닫힘/열림 PNG를 표시한다. 화살표·이미지·이름을
  행 중앙에 맞추고, 긴 이름은 생략 및 전체 경로 tooltip을 유지한다.
- 원본 그림은 `Tools/icons/Generate-DirectoryIcons.ps1`의 벡터 좌표이며 PNG는 64×64다.
  `EditorAssetPresentation`이 고정 이미지 두 장을 한 번 읽고 종료 시 해제하며,
  `EditorImGuiTexture::From`의 백엔드 변환/캐시를 사용한다. 이미지가 없으면 폰트 아이콘으로 대체한다.

이번 후속 검증 산출물은 `Artifacts/phase21-browser-spacing/`이다.

| 검증 | 결과 |
|---|---|
| VS18/v145 x64 Debug CreatorEditor | exit 0, `build-debug.log`. 기존 Vulkan DELAYLOAD 관련 LNK4229 경고 1개 |
| 실제 DX12 화면 | `browser-final.png`: 분할선 뒤 여백, 폴더 PNG, 프로젝트/Assets/하위 폴더 정렬 확인 |
| 트리 입력 | Animation 선택→경로/목록 변경, Assets 접기/펼치기 및 이미지 전환, 폴더 우클릭 메뉴 확인 |
| 실행 상태 | `/health` idle, 프레임 361→1578 증가. OS DPI 150%, 사용자 배율 150%, `editor.theme` clean |
| 정리 | 검증용 Editor 정상 종료, stderr 0 bytes, 검증 직전 설정·도킹 ini 원본 바이트 복원 |

이번 후속에서는 기존 640/64/108개 전체 게이트와 실제 마우스 분할선 드래그, 좁은 창 및
프리팹 drop 소비자를 재실행하지 않았다. 위 이전 검증과 이번 화면/입력 검증을 구분한다.

## 남은 범위

W2-B 전체 완료 판정은 하지 않는다. 최근/전체 검색, 방문별 검색·선택 복원, W3 workspace 상태 통합,
W7 목록 snapshot/cache/clipping 및 비동기 썸네일 교체는 남아 있다. 현 단계는 파일 유형 아이콘을 표시한다.
Vulkan은 사용자 요청에 따라 이번 검증에서 제외한다.
