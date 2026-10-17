# Add Component 탐색과 C# 생성·부착 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 목록과 검색

Inspector의 Add Component는 native ComponentFactory와 ClrHost의 compiled script 이름으로 catalog를 만든다. 표시명은 native Component suffix 또는 managed namespace를 정리하고 단어 경계를 넣는다. Rendering, Animation, Physics, Audio, AI, Input, UI, Other와 Scripts 분류는 `EditorComponentCatalog`가 소유한다. 검색은 표시명·원래 타입명·카테고리에 모든 검색 단어가 포함되는지 검사하고 이름의 공백·대소문자 차이를 흡수한다.

검색 중에는 현재 카테고리에 제한하지 않으며 검색이 없을 때는 카테고리 탐색을 한다. 방향키/Enter 및 마우스 선택이 연결되어 있다. 같은 native 타입이 이미 붙어 있으면 Added 표시와 disabled 처리가 적용된다. managed script는 Script 표시와 별도 부착 경로를 사용한다. 검색 결과가 비면 compiled script 없음과 일반 검색 실패를 구분한다.

## Create and Add

New Script는 요청 당시 EntityHandle을 붙잡고 `EditorScriptAuthoring::CreateAndAttach`를 호출한다. 이름은 100자 이하 ASCII letter/digit/underscore, 첫 글자 letter/underscore 규칙이며 C# 키워드·Windows 장치명·이미 등록된 타입을 거부한다. `Assets/Script/<Name>.cs`를 CREATE_NEW로 열어 기존 파일을 덮어쓰지 않는다. 템플릿은 CreatorEngine.Scripts namespace의 sealed partial Component이며 OnBeginSimulation을 포함한다.

소스와 meta 작성 후 외부 빌드를 시작하고 EditorMain의 Tick이 게임 스레드에서 결과를 처리한다. 팝업을 닫거나 다른 대상을 선택해도 자동 부착 대상은 바뀌지 않는다. 다만 실제 적용 시 handle의 생존, scene 및 Play 사전조건을 다시 확인한다. 생성과 재시도는 Play 중 제한되며 compile 성공이 component 부착 성공과 같지는 않다.

## 실패와 검증

busy, 잘못된 이름, 읽기 전용 디렉터리, 파일 중복, compile/reload 실패, 취소를 status와 build log로 구분한다. 생성한 source가 남아 있다는 것과 빈 ScriptComponent가 붙었다는 것은 다르다. source 정리 여부를 compile 성공으로 추정하지 않는다.

`verify-editor-component-catalog.ps1`은 목록/등록, `verify-editor-component-browser.ps1`은 UI 연결, script 자동화 runner는 빌드·재연결을 다룬다. 과거 `Artifacts/phase21-add-component`와 component-polish 화면은 당시 외관이며 현재 catalog의 타입 수나 최신 CLR 동작의 증명이 아니다. 이번에는 실제 Windows 입력이나 .NET 빌드를 수행하지 않았다. 좁은 폭·한글 필드·중복 script·삭제된 대상·compile 중 Play 전환은 계속 필요한 회귀 사례다.

## 구현 근거

- [Editor/EngineEntry/EditorComponentCatalog.h](../../Editor/EngineEntry/EditorComponentCatalog.h)
- [Editor/EngineGUIWindow/InspectorWindow.cpp](../../Editor/EngineGUIWindow/InspectorWindow.cpp)
- [Editor/EngineEntry/EditorScriptAuthoring.cpp](../../Editor/EngineEntry/EditorScriptAuthoring.cpp)
- [Editor/EngineEntry/EditorObjectOperations.cpp](../../Editor/EngineEntry/EditorObjectOperations.cpp)
- [Tools/regression/verify-editor-component-browser.ps1](../../Tools/regression/verify-editor-component-browser.ps1)
- [Tools/regression/verify-editor-component-catalog.ps1](../../Tools/regression/verify-editor-component-catalog.ps1)
