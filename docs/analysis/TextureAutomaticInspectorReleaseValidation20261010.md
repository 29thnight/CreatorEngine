# Release 입력 전달과 저장 대기 종료 검증 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 입력 전달의 제품 계약

숫자 키와 Ctrl+C/Ctrl+V는 다른 경로를 검증한다. Editor의 WinProcProxy는 Win32 메시지를 presentation 쪽으로 전달하며 메시지 시점의 keyboard state를 함께 보존한다. 소비 측에서 이를 복원한 뒤 ImGui backend에 전달해야 Ctrl 상태가 다른 스레드의 나중 시각으로 해석되지 않는다. 이 계약을 확인하려면 단순 명령 주입 대신 실제 복사·붙여넣기로 입력값을 바꾸어야 한다.

현재 자동 Inspector에서는 유효한 변경이 EditorAssetDatabase에 예약된다. field 값, sidecar 최종 값, import 상태와 씬 표시를 함께 확인한다. 무효 설정·손상 원본·Retry 실패에서 직전 정상 결과를 보존하는 동작은 [자동 UI 계약](TextureAutomaticInspectorUiValidation20261010.md)과 같다.

## pending probe가 하는 일

`assets.texture.pendingprobe`는 `texture_auto_ui_pending_probe.inl`에 구현된 commandlet 관찰 경로다. 격리 fixture와 marker를 요구하며 실제 Inspector가 만든 대기 저장의 age 및 디스크 바이트를 관찰한다. 저장 설정을 대신 만드는 UI 대체물이 아니다. pending을 찾은 뒤 기존 창 닫기 큐 또는 정상 quit을 요청해 400ms보다 이른 요청 시점을 재현한다.

여기서 request 시점과 UI 스레드가 닫기를 적용한 시점은 다르다. age가 정수 밀리초 0이라고 지연이 없다는 뜻도 아니다. 종료는 App::Finalize의 FlushAutomaticSaves 경로를 타며, 재시작 뒤 sidecar 값이 남았는지 별도 확인해야 한다.

## 역사적 Release 결과의 해석

2026-10-10 기록은 `TAUI-Release-manual-075e7c23`, `TAUI-Release-close-84c0265b`, `TAUI-Release-shutdown-d9d7470b`의 작은 4×4 fixture에서 입력·제어된 pending close/정상 종료를 관찰했다. 집계 위치는 `Build/TextureAutomaticReleaseUi/result.json`이었다. 이 경로들은 로컬 증거 식별자이며 저장소 배포 파일이나 현재 실행 결과가 아니다.

당시 실제 붙여넣기와 sidecar 값, 재시작 값은 UI 수용 범위였지만 native 마우스 종료를 400ms 안에 수행했다는 검증은 아니었다. 화면 유지용 wait 요청을 장시간 완료 검사로 읽어서는 안 된다. 현재 문서 개정에서는 Release 빌드 또는 UI 재실행을 하지 않았다.

## 남은 회귀

동일 keyboard-state 변경의 Debug 실입력, IME/다른 modifier 조합, 다중 DPI, 반복 close/reopen, 강제 종료, cook 중 project 전환, 여러 자산의 경쟁 저장은 별도 수용이 필요하다. 외부 writer의 디스크 변경은 [충돌 계약](TextureAutomaticInspectorConflictValidation20261010.md)에 분리한다. 전체 데이터 흐름은 [정본](../plans/TexturePipelinePlan.md)에 있다.

## 구현 근거

- [Editor/EngineEntry/WinProcProxy.cpp](../../Editor/EngineEntry/WinProcProxy.cpp)
- [Editor/EngineEntry/WinProcProxy.h](../../Editor/EngineEntry/WinProcProxy.h)
- [Tools/regression/texture_auto_ui_pending_probe.inl](../../Tools/regression/texture_auto_ui_pending_probe.inl)
- [Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
