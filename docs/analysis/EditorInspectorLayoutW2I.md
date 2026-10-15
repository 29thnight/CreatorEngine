# Inspector 반응형 속성 배치와 편집 안정성

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 배치 API

`EditorPropertyRow`의 property_layout_state는 호출자가 프레임 간 보유한다. `measure_property_layout(inputs,state)`는 ImGui 호출 없이 content 가용폭, 고정 라벨 hint, 값 최소폭, 축 badge/간격, 보조 버튼 reserve, hysteresis 및 editing 여부로 결과를 결정한다. 라벨 열은 현재 값의 길이나 창 폭의 일정 비율로 흔들리지 않도록 고정 라벨 집합과 최소폭을 기준으로 한다.

넓은 폭에서는 inline_row, 좁으면 stacked로 값 줄을 아래에 놓는다. 세 축이 한 행에 들어가지 않으면 axis_stacked를 별도로 결정한다. active 편집 중에는 이전 모드를 유지하고, 넓어질 때 hysteresis를 통과해야 복귀한다. 이는 입력 ID/드래그 위치를 지키는 규칙이며 단순 clip rect만 넣는 해결과 다르다.

## 실제 소비 경로

property_sheet가 라벨과 값 커서를 배치하고 표준 Input/Drag 위젯 또는 EditorAxisField3가 편집한다. reflection drawer와 custom inspector가 같은 current_property_layout을 이용한다. `widen_property_line`은 명시적인 wide 줄을 만들며 폭 부족으로 자동 stacked가 된 경우와 결과 모양은 같아도 원인은 다르다. `display_label`은 m_ 제거와 대문자 경계 등을 적용하는 표시 규칙이며 반환 버퍼를 오래 저장하는 API가 아니다.

texture typed import와 일반 YAML drawer도 같은 property sheet를 사용한다. 중첩 map/sequence의 라벨은 그 위치의 실제 가용폭으로 계산해야 한다. 저장 단위·Undo·검증 오류는 각각의 소비자가 소유한다. 배치 helper가 모든 reflected 값에 validation을 자동 부여하지 않는다.

## 측정과 실패 기준

property_field_tally는 공통 줄 수, 실제 값 위젯 수, 최소 폭, 축 폭과 ID digest, 첫 field rect를 기록한다. 단순 overflow 0만으로 통과시키면 1px로 눌린 입력을 놓친다. ID digest는 폭/DPI 변화 전후의 동일 fixture에서 비교하고, active 편집 중 값과 Undo round-trip도 확인한다. 실제 폭과 interaction을 확인하지 않은 source 검색은 실행 수용이 아니다.

기존 W2-I의 9일/6일 견적은 현재 구현 상태의 척도로 사용하지 않는다. [워크스페이스 현재 계약](../plans/EditorWorkspaceRedesignPlan.md#phase21-current-status)과 [Inspector 절](../plans/EditorWorkspaceRedesignPlan.md#w2-inspector-layout)이 정본이다. 남은 요구는 여러 drawer·긴 한글/배열·작은 폭·DPI·active drag의 실실행 matrix와 multi-object mixed 편집의 별도 정책이다.

## 구현 근거

- [Editor/ImGuiHelper/EditorPropertyRow.h](../../Editor/ImGuiHelper/EditorPropertyRow.h)
- [Editor/ImGuiHelper/EditorPropertyRow.cpp](../../Editor/ImGuiHelper/EditorPropertyRow.cpp)
- [Editor/ImGuiHelper/EditorAxisField3.cpp](../../Editor/ImGuiHelper/EditorAxisField3.cpp)
- [Editor/EngineGUIWindow/ReflectionTypedDraw.h](../../Editor/EngineGUIWindow/ReflectionTypedDraw.h)
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
- [Tools/regression/verify-inspector-layout-matrix.ps1](../../Tools/regression/verify-inspector-layout-matrix.ps1)
- [Tools/regression/verify-inspector-edit-roundtrip.ps1](../../Tools/regression/verify-inspector-edit-roundtrip.ps1)
