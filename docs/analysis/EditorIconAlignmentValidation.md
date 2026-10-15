# 아이콘 baseline 정렬과 fallback 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 정렬 알고리즘

`editor::fonts::merge_aligned_icons`는 이미 atlas에 있는 destination text font에 icon font를 merge한다. CalcTextSize의 line box만으로는 visible glyph bearing 차이를 알 수 없으므로 sample size 128에서 본문 H와 semantic Menu glyph의 Y0/Y1 중심 및 ascent를 비교한다. destination text size와 icon size를 적용해 GlyphOffset.y를 구하고 opticalOffset을 더한다.

destination이 implicit reference size이면 ImGui가 nonzero offset merge를 허용하지 않으므로 zero-offset 경로를 유지한다. destination이 없거나 source font를 읽지 못하면 null을 반환한다. 임시 metrics bake는 merge 때 폐기되고 실제 offset은 reference pixels로 남아 user scale/DPI에 따라 확대된다.

## 소비와 회귀

EditorFontResources는 본문 fallback을 고른 뒤 이 merge helper를 사용한다. 따라서 특정 개발자 PC의 Inter 한 폰트에서 보기 좋은 상수를 모든 fallback에 하드코딩하지 않는다. private-use exclusion과 icon merge가 함께 지켜져야 올바른 glyph가 선택된다.

`editor_icon_alignment_probe.cpp`와 `verify-editor-icon-alignment.ps1`이 실제 glyph metric을 비교하는 재현 경로다. glyph 중심 계산의 단정과 최종 행의 optical balance는 다르다. toolbar/tab/header/menu의 작은 크기, 여러 user scale/DPI, system font 또는 default fallback을 실제 화면에서도 확인해야 한다.

과거 `Artifacts/phase21-icon-alignment/editor-aligned.png`는 당시 캡처다. 본 개정은 원본 이미지나 TTF를 변경하지 않았고 최신 실행 화면을 만들지 않았다. semantic role 또는 글꼴 source를 바꾸면 probe와 시각 수용을 다시 수행한다.

## 구현 근거

- [Editor/ImGuiHelper/EditorIconAlignment.h](../../Editor/ImGuiHelper/EditorIconAlignment.h)
- [Editor/EngineGUIWindow/EditorFontResources.cpp](../../Editor/EngineGUIWindow/EditorFontResources.cpp)
- [Tools/regression/editor_icon_alignment_probe.cpp](../../Tools/regression/editor_icon_alignment_probe.cpp)
- [Tools/regression/verify-editor-icon-alignment.ps1](../../Tools/regression/verify-editor-icon-alignment.ps1)
