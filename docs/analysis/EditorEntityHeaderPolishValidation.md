# Editor 엔티티 헤더·여백 후속 조정 (2026-09-12)

## 변경

- Inspector의 별도 Tag 행을 제거하고 이름 입력칸 오른쪽 끝에 Material Symbols `sell` 태그 버튼을 통합했다.
  이름 입력과 태그 버튼은 공간을 나눠 쓰므로 긴 이름이 아이콘 아래에 겹치지 않는다.
  툴팁에 현재 태그를 표시하고, 클릭하면 기존 태그 목록과 Add Tag를 연다.
  현재 태그는 표준 MenuItem의 체크 표시로 구분하며 선택 변경 후 다시 열어도 이를 표시한다.
  태그 변경은 기존 TagManager의 멤버십 제거/추가 경로를 사용한다.
  이름 변경은 기존 EditorObjectOperations::Rename 경로를 유지한다.
- Layer의 표시 명칭을 Physics Layer로 변경했다. 기존 콤보/충돌 타입 갱신 경로를 유지한다.
  관련 추가 항목·팝업·이름 라벨에도 Physics Layer를 사용한다.
- Inspector 창의 수평 여백을 10 logical px로 지정했다. 창 선언의 padding은 호스트에서
  사용자 배율과 DPI를 한 번 적용한다. 기존 명시적 padding 소비자는 Scene/Game의 0px뿐이다.
- 계층 창 + 버튼은 가로 padding 7px 때문에 작은 정사각형 안에서 아이콘이 오른쪽으로 밀렸다.
  이 버튼의 가로 padding을 0으로 바꾸고 기존 높이를 유지한다.
- 제목표시줄은 로고 뒤 Dummy가 추가하던 간격을 제거하고 첫 메뉴 시작점을 24 logical px로 둔다.
  메뉴 항목 사이의 간격과 팝업 간격은 유지한다.
- Content Browser는 8px 조절 영역 가운데 1px 선만 그린다. hover/active 색도 같은 얇은 선에 적용한다.
  마우스 드래그, 더블 클릭 초기화, 키보드 조절 경로를 유지한다.
- Material Symbols 원본 커밋과 fonttools 4.59.0은 유지한다. 정본 JSON으로 TTF/헤더/provenance를
  함께 재생성했다(64 semantic roles, 59 unique glyphs).

## 검증

증거 디렉터리: `Artifacts/phase21-entity-tag/`.

- Debug x64 `CreatorEditor.vcxproj /t:Build` 통과.
- 최종 실행 파일 `editor.selftest`: **916 checks, 0 failures**.
  `editor.theme`의 geometry/DPI/font 검사 통과, semantic icon 64개 누락 0개.
- 아이콘 래스터 정렬 검사: **289 checks, 0 failures**, 최대 중심 오차 1.500px.
- 실제 DX12 화면(사용자 150%, OS DPI 150%)에서 이름 끝 태그 아이콘, Physics Layer,
  Inspector 가장자리 여백, + 버튼 가로 중앙 정렬, 로고-File 간격과 얇은 분할선을 확인했다.
- Main Camera 태그를 Untagged→Player로 바꾼 뒤 팝업을 다시 열어 Player 체크 표시를 확인했다.
  Untagged로 복원한 후 좁은 창에서도 체크 표시를 다시 확인했다.
- Physics Layer 드롭다운 열기와 기존 Default 선택, 계층 + 생성 메뉴 열기,
  File 메뉴 열기, Add Tag 팝업 열기/취소를 확인했다. 씬 저장이나 새 태그 정의 생성은 하지 않았다.
- 창 크기 2880×1665→1280×1445→2880×1665에서 이름/태그 버튼 영역이 겹치지 않으며,
  좁을 때 Static과 Physics Layer가 다음 행으로 배치되는 것을 확인했다.
- 분할선 키보드 조절로 contentTreeWidth 204.8889→212.8889, 더블 클릭으로 220 초기화를 확인했다.
  native drag 호출 1회로는 폭이 변하지 않아 마우스 드래그 성공으로 집계하지 않는다.
  이번 변경은 그리기 영역에 한정하며 기존 8px hit 영역과 드래그 코드는 유지한다.
- `verify-editor-theme.ps1 -Backends dx12`: **3회 기동, 64 checks 통과**.
  사용자 배율 100→150→100%, 관측 OS DPI 150%. 실제 모니터 간 이동 검증은 포함하지 않는다.
- 검증용 Editor 종료 후 EngineSettings.asset와 imgui.ini를 시작 전 바이트로 복원했다.
- 변경 파일 `git diff --check`, 대시보드 JavaScript 구문 검사 통과.

화면: [전체 배치](../../Artifacts/phase21-entity-tag/editor-polish.png),
[선택 태그 체크](../../Artifacts/phase21-entity-tag/tag-selected.png),
[좁은 Inspector](../../Artifacts/phase21-entity-tag/inspector-narrow.png),
[Physics Layer](../../Artifacts/phase21-entity-tag/physics-layer.png),
[분할선](../../Artifacts/phase21-entity-tag/splitter-thin.png).

Vulkan 및 전체 W2-I/W2-B 완료 판정은 이번 범위에 포함하지 않는다.
