# 아이콘·본문 수직 정렬 보정

2026-09-12. PHASE 21 W1 후속 수정. 사용자 표시 위치는 Scene/Game 탭과 Perspective 버튼이다.

## 원인과 수정

- 기존 라벨은 같은 문자열 또는 같은 `CalcTextSize` 높이로 배치됐지만,
  Inter와 Material Symbols의 실제 글리프 기준선·상하 여백은 달랐다.
  `GlyphOffset.y=0` 병합 때문에 아이콘 가시 영역이 글자보다 위에 놓였다.
- 폰트 병합의 공통 경로에 `merge_aligned_icons`를 적용했다.
  실제 적재한 본문의 H와 Material Symbols의 대칭 Menu 글리프를 128px로 측정하고,
  기준선에 대한 가시 영역 중심 차이를 각 폰트의 기준 크기로 환산한다.
  계산한 `GlyphOffset.y`가 ImGui의 사용자 배율·DPI 확대에 함께 적용된다.
- Scene/Game 도킹 탭, 툴바, Hierarchy/Inspector 및 작은 글씨에 병합된 아이콘이 같은 보정을 받는다.
  개별 아이콘의 고유 비율·글자의 descender를 보존하며, 모든 모양의 아래 끝을 강제로 맞추지는 않는다.
- 기본 폰트 fallback에도 기준 크기를 명시해 비영점 보정을 허용한다.
  외부의 암묵 크기 폰트 병합은 ImGui가 허용하는 영점 보정 경로를 유지한다.
- 폰트 파일·글리프 모양·가로 advance·본문 글꼴·저장된 창 ID는 수정하지 않는다.

## 검증

- `verify-editor-icon-alignment.ps1`: 289개 검사, 실패 0.
  실제 ImGui rasterizer와 제품의 병합 함수를 사용했다.
- Inter/Verdana/맑은 고딕/ImGui 기본 폰트, 기준 크기 10/12/16px,
  배율 1/1.25/1.5/2/2.25/3, Scene/Game/Menu/Lit의 가시 중심을 검사했다.
  glyph raster rounding을 포함한 최대 중심 차이는 1.5 물리 px다.
- DX12 Debug 빌드 성공. 실제 Scene/Game 탭·Perspective/Lit 버튼·Hierarchy·Inspector의 정렬을 확인했다.
  [적용 화면](../../Artifacts/phase21-icon-alignment/editor-aligned.png).
- `editor.theme`: clean/geometryMatches/themeMappingMatches=true, 63개 아이콘 역할 누락 0.
  `editor.selftest`: 640개 검사, 실패 0. 시작 assertion 없이 프레임이 진행됐고 stderr는 비어 있다.
- DX12 시작 사용자 배율 100→150→100%의 3회 재기동, 64개 테마 회귀 검사 통과.
  실제 OS DPI는 150%였고 모니터 이동 검사는 아니다. 각 실행의 640개 selftest도 통과했다.
- 프로젝트 설정과 imgui.ini를 작업 전 원본 바이트로 복원했다.

작업 증거는 `Artifacts/phase21-icon-alignment/`에 둔다.
이 수정으로 W2/W4/W5의 별도 완료 범위를 확장하지 않는다.
