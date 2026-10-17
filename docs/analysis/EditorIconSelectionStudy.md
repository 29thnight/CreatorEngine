# 기능 아이콘의 정본: Material Symbols와 semantic role

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 선택과 생성 경로

이 문서는 후보 비교를 반복하는 연구 노트 대신 현재 선택의 근거를 설명한다. toolbar·menu·component control의 기능 아이콘은 Material Symbols를 semantic role로 참조한다. [MaterialSymbols.json](../../Resources/Editor/Fonts/MaterialSymbols.json)이 원본 commit, axes(opsz=20, wght=400, FILL=0, GRAD=0), fonttools 4.59.0과 role→glyph 이름을 기록한다. `build-editor-symbol-font.py`가 이 입력을 통해 subset font와 EditorIcons 헤더 및 provenance를 함께 생성하는 경로다.

호출자는 codepoint를 직접 복사하기보다 EditorIcon::Scene, Tag, Search 같은 의미 이름을 사용한다. 여러 role이 같은 glyph를 쓸 수 있으므로 role 수와 고유 glyph 수는 같은 측정이 아니다. 이름 변경으로 window stable ID까지 바뀌지 않도록 표시 label과 `###Editor.*` 신원을 분리한다.

## 글꼴과 이미지의 경계

EditorFontResources는 배포 Inter와 Windows font fallback을 시도하고 필요한 본문 글꼴을 읽지 못하면 ImGui default로 내려간다. 본문 글꼴의 private-use 범위를 제외해 아이콘 코드포인트를 침범하지 않게 한다. `merge_aligned_icons`는 실제 glyph bounds 기준으로 text와 icon baseline을 맞춘다. 정렬 방식은 [별도 계약](EditorIconAlignmentValidation.md)에 있다.

Fluent Emoji 3D는 entity/file/folder artwork를 담당한다. [Fluent 자원](EditorFluentEmojiValidation.md)을 기능 icon font로 바꾸거나 모든 control을 emoji로 대체하는 선택은 현재 계약이 아니다.

## 이전 연구의 취급

Tabler, Codicons, Font Awesome, s&box 등의 초기 비교 링크는 당시 대안 연구였으며 현재 dependency 목록이 아니다. 기능 아이콘 교체가 필요하면 라이선스, glyph coverage, font merge 동작, fallback, semantic header와 provenance 재생성까지 한 변경으로 검토한다. 이미지를 수정하지 않는 문서 개정에서 glyph 수·hash를 새로 생성한 것처럼 보고하지 않는다.

검증 출발점은 `verify-editor-icon-resources.ps1`과 alignment probe다. 자원 존재/manifest 검사와 실제 toolbar·popup·DPI 시각 수용은 구분해야 한다. 현재 코드가 고정된 의존성을 가리킨다는 사실은 모든 Unicode/IME 상태의 typography 수용을 의미하지 않는다.

## 구현 근거

- [Resources/Editor/Fonts/MaterialSymbols.json](../../Resources/Editor/Fonts/MaterialSymbols.json)
- [Resources/Editor/Fonts/MaterialSymbols.provenance.json](../../Resources/Editor/Fonts/MaterialSymbols.provenance.json)
- [Tools/fonts/build-editor-symbol-font.py](../../Tools/fonts/build-editor-symbol-font.py)
- [Editor/ImGuiHelper/EditorIcons.h](../../Editor/ImGuiHelper/EditorIcons.h)
- [Editor/EngineGUIWindow/EditorFontResources.cpp](../../Editor/EngineGUIWindow/EditorFontResources.cpp)
- [Tools/regression/verify-editor-icon-resources.ps1](../../Tools/regression/verify-editor-icon-resources.ps1)
