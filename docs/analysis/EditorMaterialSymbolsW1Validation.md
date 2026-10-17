# Material Symbols: 현재 폰트 경로와 W1 검증의 범위

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 소비 경로

역할 이름은 `Resources/Editor/Fonts/MaterialSymbols.json`이 소유한다. 고정된 upstream commit, 축 값, fontTools 버전으로 `build-editor-symbol-font.py`가 subset TTF와 `EditorIcons.h`를 생성한다. Editor 코드는 역할 상수를 사용하며 임의의 PUA 숫자를 화면마다 복제하지 않는다. 생성 헤더를 직접 수정하면 다음 생성에서 사라진다.

`EditorFontResources`는 본문과 아이콘 source를 atlas에 구성한다. Material Symbols 범위는 텍스트 fallback에서 제외해야 한다. `EditorTextFallback`의 `0xE000..0xF8FF` exclusion은 config가 참조하는 동안 살아 있는 정적 저장소다. ImGui의 source config가 임시 vector를 가리키게 하면 merge 뒤 glyph 출처를 보장할 수 없다.

## W1에서 보존할 판단

W1은 기존 문자 폰트가 PUA를 먼저 점유하여 Material Symbols 대신 다른 glyph를 선택하는 문제를 찾은 역사적 검증이다. 단순히 TTF 파일 존재나 역할 문자열이 nonempty인지만 확인하면 이 문제를 놓친다. 현재 `EditorChromeProbe`는 atlas의 실제 source와 `GlyphExcludeRanges`를 확인하고 HTTP 진단의 `iconSourcePolicyValid`에 반영한다.

당시의 59개 역할·54개 고유 glyph·7,920-byte subset 수치는 W1 당시 입력에만 해당한다. 현재 manifest에는 Material/Node Editor 역할 등이 추가되어 있으므로 그 수치를 현재 자산 크기나 완전성 지표로 재사용하지 않는다. provenance와 라이선스 파일은 생성물과 함께 유지한다.

## 다시 확인할 항목

1. manifest의 모든 역할이 생성 헤더와 subset cmap에 존재하는지 확인한다. 같은 glyph를 여러 역할이 공유하는 것은 정상이다.
2. atlas에서 본문 fallback의 PUA exclusion과 아이콘 source의 실제 선택을 확인한다. 화면에 비슷한 아이콘이 보인다는 것만으로 source policy를 통과시키지 않는다.
3. DPI/배율, 밝고 어두운 배경, disabled 상태, 실제 Node Editor toolbar의 baseline·정렬·명암을 확인한다.
4. 생성 도구와 provenance의 입력 pin이 일치하는지 확인한다. 폰트를 변경했다면 기존 golden screenshot을 새 실행 결과로 취급하지 않는다.

이번 갱신은 자산·생성·런타임 진단 연결을 읽은 결과다. W1 화면을 다시 캡처하거나 현재 모든 glyph/DPI 조합을 실행 검증하지 않았다. [Node Editor 계약](../design/MaterialNodeEditor.md)의 제품 UI 게이트는 별도로 남는다.

## 구현 근거

- [`Resources/Editor/Fonts/MaterialSymbols.json`](../../Resources/Editor/Fonts/MaterialSymbols.json)
- [`Resources/Editor/Fonts/MaterialSymbols.provenance.json`](../../Resources/Editor/Fonts/MaterialSymbols.provenance.json)
- [`Resources/Editor/Fonts/LICENSE-MaterialSymbols.txt`](../../Resources/Editor/Fonts/LICENSE-MaterialSymbols.txt)
- [`Tools/fonts/build-editor-symbol-font.py`](../../Tools/fonts/build-editor-symbol-font.py)
- [`Editor/ImGuiHelper/EditorIcons.h`](../../Editor/ImGuiHelper/EditorIcons.h)
- [`Editor/ImGuiHelper/EditorTextFallback.h`](../../Editor/ImGuiHelper/EditorTextFallback.h)
- [`Editor/EngineGUIWindow/EditorFontResources.cpp`](../../Editor/EngineGUIWindow/EditorFontResources.cpp)
- [`Editor/EngineGUIWindow/EditorChromeProbe.cpp`](../../Editor/EngineGUIWindow/EditorChromeProbe.cpp)
