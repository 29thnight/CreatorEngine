# Inspector S&Box 스타일 적용 및 검증

- 날짜: 2026-09-12
- 범위: PHASE 21 W2/W2-I의 Inspector 시각 스타일 후속 슬라이스
- 기준: 사용자가 제공한 CreatorEngine / S&Box Inspector 스크린샷
- 백엔드: DX12. Vulkan은 앞선 사용자 결정에 따라 보류한다.

## 적용

| 요소 | 적용 내용 |
|---|---|
| 패널 / 입력칸 | Inspector 배경 `#323534`, 입력칸 `#171818`. 전역 Scene/워크스페이스 색은 유지 |
| 글자 / 간격 | 값은 밝게, 속성 라벨은 `#BEC5C2`. 행 간격 3 logical px, 기존 24 logical px 컨트롤 높이 유지 |
| XYZ | 공통 어두운 배지 위에 색이 다른 X/Y/Z 문자. 값과 연결된 모서리, 3 logical px 축 간격 |
| 숫자 | 왼쪽 정렬. `0.000 → 0`, `1.000 → 1`, `0.100 → 0.1`. 값·드래그 정밀도·직접 입력 포맷은 유지 |
| 컴포넌트 | 작은 접기 화살표, 중립색 Transform 아이콘, 가벼운 더보기 버튼, 기존 얇은 구분선 유지 |
| 내부 그룹 | Camera 등의 채움 헤더를 가벼운 Tree 행으로 전환. 기존 자식 필드 ID 유지 |
| 오브젝트 헤더 | 오브젝트 아이콘과 녹색 활성 상태. 이름·Static·Tag·Layer의 기존 기능 유지 |

색은 첨부 화면에 맞춘 근사값이며 S&Box의 공식 테마 상수라고 주장하지 않는다.
Inter / Material Symbols 및 비동기 썸네일에 대한 기존 결정은 바꾸지 않았다.
Inspector 헤더의 탐색 이력·잠금 기능, 실제 에셋 썸네일은 이번 슬라이스에 포함하지 않는다.

## 구현 경계

- `EditorStandardWindows.h`의 선언된 배경은 호스트가 `Begin` 전에 적용한다.
- `InspectorStyleScope`가 본문 색/간격을 적용하고 조기 반환에서도 복원한다.
- 공통 속성 라벨·숫자·내부 그룹의 새 표시는 Inspector 범위에서 활성화한다.
- 숫자의 드래그/직접 입력은 ImGui `DragBehavior` / `TempInputScalar`를 사용한다.
  활성화 연결은 현재 의존성인 ImGui 1.92.8 `DragScalar`에서 가져왔고 MIT 고지를 포함했다.
  ImGui 업그레이드 시 이 연결과 직접 입력·Tab·드래그 동작을 함께 확인해야 한다.
- `EditorChromeProbe`의 전역 FrameBg 기대값을 실제 `ApplyEditorTheme`와 동일한 Chrome으로 수정했다.
  Inspector를 그린 뒤 전역 색과 geometry가 복원됐는지 기존 `editor.theme` 감사로 확인한다.
- 명령의 selftest는 게임 스레드에서 실행된다. 순수 포맷/대비 검사를 수행하고,
  실제 ImGui 상태는 Presentation 스레드에서 게시하는 snapshot으로만 검사한다.

## 편집 중 발견한 기존 Undo 결함

Position/Scale은 첫 변경 **후**의 값을 이전 값으로 저장하고 있었고,
Rotation은 이전 값을 매 프레임 덮어쓰고 있었다. 직접 입력 0→1 뒤 Undo 이력이 늘지 않는 것을 재현했다.
첫 변경 직전 값을 저장하고 편집 종료까지 유지하도록 고쳤다.

수정 후 실제 Inspector에서 입력/드래그하고 제품 `undo` 명령을 호출해 다음을 확인했다.

| 실제 조작 | 변경값 | Undo 후 |
|---|---|---|
| Position X 직접 입력 | 0 → 2 | (0, 0, 0) |
| Rotation X 드래그 | 약 4.6° | quaternion (0, 0, 0, 1) |
| Scale X 드래그 | 1 → 4.3 | (1, 1, 1) |

직접 입력 후 Undo 이력이 1→2로 늘었고, 원래 Transform으로 복원됐다.
자동화의 Ctrl+Z 입력은 제품 Undo를 실행하지 않았으므로 키보드 단축키까지 통과했다고 세지 않는다.
위 검증은 GUI 편집과 공유 제품 Undo 명령 경로의 검증이다.

## 화면 및 검증 범위

- VS18/v145 Debug Editor 빌드와 실제 DX12 실행 확인.
- Main Camera의 기본 폭/좁은 폭/기본 폭 복귀 확인. 좁은 폭에서 XYZ 세로 전환, 복귀 후 가로 정렬 확인.
- 사용자 배율 150%, OS DPI 150% 환경에서 촬영했다. 두 배율은 기존처럼 각각 한 번 적용된다.
- 최종 DX12 배율 gate: 사용자 배율 100→150→100%의 3회 기동, 64개 검사 통과.
  실제 OS DPI는 150%로 유지됐으며 폰트 크기는 24→36→24px였다. 실제 모니터 이동 검증은 아니다.
- 매 기동의 `editor.selftest`: 311개 검사, 실패 0. 포맷 9개 사례 및 축 문자 대비 4.5:1 이상 포함.
- 최종 바이너리에서 Main Camera를 선택한 별도 기동도 `editor.theme` 2회와 selftest를 통과했다.
  Inspector 본문을 그린 뒤 전역 테마가 복원되는 경로까지 확인했다.
- 샘플 씬 편집은 저장하지 않았으며 설정/ini는 검증 전 원본과 바이트 단위로 동일하게 복원했다.

화면: [기본 폭](../../Artifacts/phase21-inspector-style/inspector-normal.png),
[좁은 폭](../../Artifacts/phase21-inspector-style/inspector-narrow.png).
입력/Undo JSON 및 빌드 로그: `Artifacts/phase21-inspector-style/`.

W2/W2-I 전체 완료 판정은 하지 않는다. 전용 드로어 전수, 모든 속성 타입·상호작용 상태,
계획된 폭/DPI 전체 조합, 성능 gate, Vulkan은 이번 검증 실적에 포함하지 않는다.

## 2026-09-13 Float3 가로 배치 복귀 기준

값 열에 여유가 있어도 축이 세로로 남는 원인은 최소 폭의 대표 문자열 `-0.000`과,
가로 복귀 여유 폭을 축마다 더해 행 전체에서는 3배가 되는 판정이었다.
공통 `EditorPropertyRow`의 축 최소 폭은 compact 표시용 `-0.00`로 재고,
복귀 여유 폭은 세 축 전체에 한 번만 적용하도록 변경했다.
표시 폭 판정만 바꾸며 float 저장값과 `%.3f` 편집 포맷은 유지한다.
Transform과 SerializeField Float3이 같은 기준을 소비한다.
경계의 진동 방지와 편집 중 배치 보류를 유지하며,
기존 selftest에 좁은 폭→경계→가로 복귀 회귀 검사를 추가했다.

사용자 배율 150%의 현재 창에서 Position/Rotation/Scale 각각의 XYZ 가로 배치를 실제 화면으로
확인했고, 사용자도 수정 결과를 확인했다.
[확인 화면](../../Artifacts/phase21-float3-fit/transform-inline-confirmed.png).
Float3 수정의 Editor 라이브러리 빌드 및 수정이 포함된 runtime DLL/launcher 링크는 완료됐다.
전체 통합 빌드 성공과는 구분한다. 동시 작업의 LogSink fmt 호환 오류 두 곳을 `size() != 0`으로
수정했고 런처 후처리는 출력 파이프로 종료를 기다리도록 수정했다. 이후 통합 빌드는 새
`EditorEngineDistribution.h`의 `nlohmann/json.hpp` 누락으로 중단됐다.
추가 selftest는 작성·컴파일했지만 이번 회차에서 실행 통과로 계산하지 않는다.
격리 DX12 배율 gate도 다시 열린 사용자 Editor를 감지해 시작 전에 중단됐으며,
사용자 화면과 설정은 변경하지 않았다. 근거 로그는 `Artifacts/phase21-float3-fit/`에 둔다.
