# 엔티티 이미지 프리셋·선택 이력·편집 잠금 검증

2026-09-13. PHASE 21 Inspector 후속 슬라이스. DX12 기준이며 전체 W2-I 완료 판정은 아니다.

이 문서의 @icons 이미지 출처·화면은 최초 검증 당시의 기록이다. 이후 사용자 요청으로
Microsoft Fluent Emoji 3D로 교체했다. 현재 리소스·배포 도구·화면은
[EditorFluentEmojiValidation.md](EditorFluentEmojiValidation.md)를 따른다.
아래 선택 이력·잠금·저장/복제·Undo 검증 결과는 해당 구현의 기록으로 유지한다.

## 적용 결과

- Inspector 헤더 왼쪽에 40 logical px 이미지 버튼을 배치했다. 팝업에서 9개 프리셋을
  고르고 현재 선택을 체크 표시한다. 계층 창도 같은 프리셋 이미지를 사용한다.
- 사용자가 선택한 **@icons** 스타일을 적용했다. 원본 SVG의 형태·색상을 유지하고
  96×96 투명 PNG로 변환했다. 기능 버튼의 Material Symbols 폰트는 유지한다.
- 프리셋 ID와 편집 잠금은 Entity의 숨겨진 리플렉션 필드로 저장한다. 이전 씬에서
  필드가 없으면 기본 엔티티 아이콘·잠금 해제로 시작한다. 알 수 없는 프리셋 ID는
  표시 시 기본 아이콘으로 대체한다.
- 상단 뒤로/앞으로 버튼은 엔티티 선택 이력을 이동한다. 이력은 scene ID와 generation을
  포함한 핸들을 최대 128개 보관하며, 삭제된 항목을 건너뛴다. 뒤로 이동한 뒤 새 선택을
  하면 앞으로 이력을 버린다. 씬 교체 시 초기화하며 이력 이동 자체는 Undo를 추가하지 않는다.
- 오른쪽 잠금 버튼은 선택 고정이 아닌 **엔티티 편집 잠금**이다. Inspector 입력,
  씬 transform/rect gizmo, 계층 수정·삭제·재배치를 막는다. 잠긴 엔티티 선택과 카메라
  탐색은 가능하다. 상위 변형·삭제를 통한 우회도 차단하며 자식은 상위 잠금을 따른다.
- 아이콘과 잠금 변경은 Undo/Redo를 지원한다. 복제본은 두 필드를 유지한다.
  이는 저작 상태이며 게임 시뮬레이션을 멈추는 잠금은 아니다.

## 이미지 출처

- 제작자: Valentin Fossati (Voxybuns) and contributors.
- 원본: <https://github.com/Voxybuns/at-icons>
- 고정 revision: `63827aa4f04c88d018c57cfad98aac7f33f69a4c`.
- MIT 라이선스: `Resources/Editor/Icons/LICENSE-at-icons-MIT.txt`.
  엔진 배포 리소스에도 원문을 포함한다.
- `EntityIcons.provenance.json`에 각 원본 URL, 팔레트, SVG/PNG SHA-256과 변환기 버전을 기록한다.
  `Fetch-EntityIcons.ps1`과 `Rasterize-EntityIcons.cjs`로 고정 원본을 재현한다.
  런타임에는 웹 접속이나 SVG 렌더러가 필요하지 않다.

| 프리셋 | 원본 / 팔레트 |
|---|---|
| Entity | cube / mesh |
| Game Manager | cog / control |
| Camera | video_camera / node2d |
| Light | lightbulb / mesh |
| Audio | speaker / control |
| Player | person_body / node3d |
| Trigger | lightning_bolt / animation |
| Script | file_code / control |
| Prefab | box / node2d |

## 검증

- VS18/v145 `Editor/CreatorEditor.vcxproj` Debug 빌드 통과. 최종 코드·리소스 배포 기록:
  `Artifacts/phase21-inspector-navigation/build-approved-art.log`.
- `verify-editor-entity-authoring.ps1`: **163 checks, 69 commands 통과**.
  `/health`의 idle과 frame 증가를 확인한 뒤 별도 씬에서 실행한다.
  결과는 `Artifacts/phase21-inspector-navigation/authoring-gate-final/summary.json`.
- 이력 뒤로/앞으로, Undo 스택 불변, 삭제 항목 건너뛰기, 분기·씬 교체 초기화를 확인했다.
- 아이콘·잠금 Undo/Redo, 잠금 중 이름·아이콘·Transform·컴포넌트 추가/삭제·프로퍼티·
  부모 변경/삭제 거부와 잠긴 자식을 가진 상위 엔티티 보호를 확인했다.
  거부 시 엔티티 상태와 Undo 스택이 바뀌지 않았다.
- 복제와 `.creator` 저장/재로드 후 아이콘·잠금이 유지됐다.
  잠금 해제 후 변형·이름 변경·컴포넌트 추가/삭제·엔티티 삭제가 다시 가능했다.
- `editor.selftest`: **937 checks, 0 failures**, 창·메뉴 선언 검사 통과.
  `commands.selftest`: **109 commands, 0 problems**. 검사 중 발견한 기존
  `editor.sceneview` descriptor 누락을 보완했다.
- 아이콘 정렬 검사 **289 checks, 0 failures**, 최대 중심 오차 1.500px.
- 원본 및 배포 PNG 9개의 체크섬과 MIT 고지문 배포를 확인했다.
- DX12 실제 UI: 기본 큐브와 9개 팝업 표시, Camera 선택 후 Inspector/Hierarchy 동시 반영,
  팝업 재열기 시 Camera 체크, Main Camera ↔ Directional Light 화살표 탐색을 확인했다.
  잠금 시 입력 비활성·변형 기즈모 제거·계층 Delete 차단, 해제 시 기즈모 복원을 확인했다.
- 모든 검증용 Editor를 종료하고 EngineSettings.asset와 imgui.ini를 시작 전 바이트로 복원했다.
  사용자 SampleScene의 UI 검증 변경은 저장하지 않았다.

실제 화면: [아이콘 선택 팝업](../../Artifacts/phase21-inspector-navigation/icon-presets-approved.png),
[편집 잠금](../../Artifacts/phase21-inspector-navigation/entity-locked-approved.png).

Vulkan, 전체 폭·DPI 매트릭스, 모든 전용 컴포넌트 드로어의 수동 조작 전수 검증은 포함하지 않는다.
ImGui disabled 상태만으로 드롭이 차단되지 않는 실제 구현을 확인하여 Inspector 드롭 타깃에도
disabled 가드를 넣었다. Terrain의 자동 컴포넌트 변경·브러시와 Sound 곡선의 읽기 중 변경도 막았다.
