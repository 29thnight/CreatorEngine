# PHASE 21 W1 — 실제 화면·OS DPI 전환 검증

2026-09-11, 기존 W1 Material Symbols 적용 Debug/Release 실행 파일과 현재 작업 트리 기준.
[정본 계획](../plans/EditorWorkspaceRedesignPlan.md) ·
[폰트 적용·자동 회귀](EditorMaterialSymbolsW1Validation.md) ·
[후속 원인 분석·해결안](EditorW1DpiResizeRootCauseAndFixPlan.md).

## 판정

**W1은 `progress` 유지.** 실제 화면과 OS DPI 값 검증은 수행했지만,
DX12의 Scene 표시 복구 지연과 Vulkan의 모니터 경계/리사이즈 후 device loss를 발견했다.
폰트·토큰 검사 성공을 렌더링 안정성 통과로 확대하지 않는다.
이번 작업은 검증과 기록이며 제품 코드는 추가 수정하지 않았다.

2026-09-12 후속 소스 분석에서는 공통 크기 전달의 데이터 경쟁, Vulkan 실제 extent 전달 및 실패 상태 처리 결함을 확인했다.
DX12 표시 공백의 지연 단계와 Vulkan 장치 손실의 직접 원인은 당시 계측이 부족해 미확정이다.
구체적인 수정 순서와 재검증 기준은 위 후속 해결안을 따른다. 아래 내용은 최초 재현 당시의 관측 기록이다.

## 실행과 증거

로컬 산출물: `Artifacts/phase21-w1-final-visual/`.
실행 중인 사용자 Editor가 없는 상태에서 검증용 프로세스만 실행했고, 로컬 제품 명령으로 값을 읽었다.
Windows 설정 UI에서 1번 모니터의 배율을 실제로 변경했다. 합성 DPI 메시지를 사용하지 않았다.

| 실행 | 사용자 배율 | 실제 OS DPI | 본문 크기 | 판정 |
|---|---|---|---|---|
| Debug DX12 | 100% | 150→100→150% | 24→16→24px | 폰트·DPI 값 통과, 100% 전환 후 Scene 검은 표시 관찰 |
| Release Vulkan | 100% | 150→100% | 24→16px | 해당 구간 화면·팝업 및 폰트·DPI 값 통과 |
| Release Vulkan 모니터 경계 | 100% | 100→150→100% | 16→24→16px | 값은 변경되나 복귀 리사이즈 직후 device loss |
| Debug DX12 재실행 | 150% | 150→100→150% | 36→24→36px | 폰트·DPI 값 통과, 100% 전환 후 Scene 검은 표시 재현 |

모든 기록된 `editor.theme` 관측은 59개 역할 누락 0, source policy valid,
본문/창 DPI 일치, geometry 일치를 보고했다. Vulkan 오류 발생 뒤에도 같은 명령이
`clean=true`를 반환했으므로 이 검사는 GPU 표시 성공을 보증하지 않는다.

실제 모니터 경계 검증은 다음 순서였다.

1. 1번 모니터 100%, 2번 모니터 150% 상태에서 Editor를 아래쪽 2번 모니터 방향으로 드래그했다.
2. 제품 관측 DPI가 1.0에서 1.5로 바뀌었다(`release-vulkan-monitor2-transition.json`).
3. 창이 모니터 경계에 걸친 상태에서 제품 명령 `window.resize 960 400`을 실행했다.
   DPI 변경과 함께 실제 client는 639×296으로 조정됐고 다시 DPI 1.0으로 관측됐다.
   복귀는 역방향 드래그가 아니라 창 면적이 1번 모니터 쪽에 속하도록 크기를 줄인 결과다.
4. 이 리사이즈 직후 Vulkan 오류가 시작됐다. 이후 150% 원복의 CPU DPI 값은 확인했지만
   해당 구간의 GPU 화면은 통과 처리하지 않았다.

## 실제 화면 확인

- Scene/Hierarchy/Inspector/Content Browser 탭, 검색, 선택·이동·회전·스케일, 재생·일시정지의
  Material Symbols가 문자 대체나 assertion 없이 표시됐다. 큰 배율에서도 본문과 아이콘의 모양을 확인했다.
- 모델 폴더에서 유형 아이콘과 파일명이 표시됐고, Hierarchy의 Main Camera 선택은 Inspector에 반영됐다.
- 100% DPI에서 Camera 팝업을 클릭해 열고 150% 복귀 뒤 도구 모드를 바꿨다.
  검사한 버튼과 팝업의 클릭 위치는 표시 위치와 일치했다. 씬 picking 전체를 검증한 것은 아니다.
- 증거: `debug-dx12-dpi150-start.png`, `debug-dx12-dpi100-camera-popup.png`,
  `debug-dx12-dpi150-return.png`, `release-vulkan-dpi100-camera-popup.png`,
  `debug-repeat-user150-dpi150-start.png`.
- 확대 배율에서 우측 툴바와 방향 기즈모의 겹침·잘림, 고정 폭 폴더 트리의 이름 잘림도 확인했다.
  현재 정본의 **W2-V / W2-B**에 이미 구체적인 수정 범위가 있다. 이 검증으로 해당 작업을 완료 처리하지 않는다.

## 완료 차단 항목

### DX12 — DPI 축소 후 Scene 표시 복구 지연

두 독립 Debug 실행에서 OS 배율을 150%에서 100%로 낮춘 뒤 Scene 영상 대신 검은 배경이 남았다.
본문·아이콘·도구·FPS 표시는 살아 있었으며 `editor.theme`도 정상 값이었다.
첫 실행은 Camera 버튼을 누른 뒤 영상이 복구됐다. 재실행은 추가 UI 입력 없이 진단 조회 후의
캡처에서 복구됐으므로 **클릭이 필수 조건이거나 복구 원인이라고 단정하지 않는다**.
정확한 복구 지연 시간과 원인은 아직 계측하지 않았다.

- 전/후 증거: `debug-dx12-dpi100-black-scene.png`, `debug-dx12-dpi100-camera-popup.png`,
  `debug-repeat-user150-dpi100-black-scene.png`, `debug-repeat-user150-dpi100-recovered.png`.
- `debug-black-live-status.json`은 DX12 pipeline ready와 렌더 프레임 진행을 보고한다.
  이것만으로 Scene의 표시 텍스처가 준비됐다고 판정할 수 없다.
- 소스상 `SceneViewWindow.cpp`는 표시 ID가 0이면 준비 배경을 그린다.
  `EnhancedSceneRenderer::GetLiveDisplayImTextureId`의 대상 ready/key와 DX12 표시 텍스처 개방 경로를
  DPI/resize generation 및 표시 프레임과 함께 관측해 원인을 분리해야 한다. 특정 분기 실패는 아직 확인하지 않았다.

### Vulkan — 모니터 경계에서 창 크기 조정 후 device loss

`release-vulkan.out` 1057행의 창 크기 조정 보고 바로 다음 1058행에서
`Vulkan BeginFrame 실패: 펜스 대기 실패 — VK_ERROR_DEVICE_LOST`가 시작됐다.
이어 프레임이 열리지 않았다는 렌더 실패가 반복됐고 종료 시 `vkDeviceWaitIdle`도 같은 오류였다.
`render-log-review.json`에 최초 문맥과 device-loss 로그 275,198행을 기록했다.

단순 OS 배율 변경 구간과 모니터 경계의 리사이즈 구간을 구분한다.
확인한 직접 선행 동작은 `window.resize 960 400`이며 DPI 변경만을 유일한 원인으로 확정하지 않는다.
정확한 장치 손실 원인과 swapchain/표시 자원 동기화는 추가 분석이 필요하다.
CPU 폰트 검사의 성공 응답이나 정상적인 종료 절차를 이 오류의 통과 근거로 쓰지 않는다.

참고로 `render.livecheck`는 현재 제품 서비스에서 `command.unknown`으로 거절됐다.
소스의 테스트 명령 선언을 실행 가능한 제품 검사로 취급하지 않았으며, 해당 시도는 통과 증거가 아니다.

## 복원과 다음 검증

- Windows 1번 모니터는 원래 **150%**로 복원했다. 2번 모니터의 설정은 변경하지 않았다.
- `EngineSettings.asset`과 Debug/Release의 `imgui.ini`는 실행 전 바이트로 복원했다.
  `restore-proof.json`의 세 경로 해시/일치 결과로 확인했다. 원래 사용자 배율 1.5, backend DX12다.
- 검증용 Editor·Windows 설정·제어판 창은 종료했다. 다른 창과 별도 작업 트리 변경은 보존했다.
- 다음 완료 조건은 위 두 표시 오류의 원인 분리·수정 후 실제 DPI/resize 경로 재검증이다.
  폰트 값뿐 아니라 표시 프레임 진행, 장치 손실/프레임 실패 로그까지 함께 판정해야 한다.

## 2026-09-12 DX12 후속 수정

사용자 요청에 따라 DX12 크기 변경 경로를 먼저 수정했다. 패스·셰이더·IBL은 유지하고
표시 슬롯·transient 풀·SSGI 히스토리만 GPU 완료 뒤 교체한다.
최종 Debug/Release 빌드, 각 10회 resize와 실제 OS DPI 왕복이 통과했다.
DPI 150→100%의 Scene 텍스처 공백은 기존 8,578.926ms에서 Debug 118.492ms,
Release 62.719ms로 줄었다. Scene·Game이 새 generation을 표시하는 것과 폰트 정상 여부를 함께 확인했다.
상세와 범위는 [EditorW1Dx12ResizeValidation.md](EditorW1Dx12ResizeValidation.md)에 둔다.
이전 Vulkan device loss는 이번에 수정·재검증하지 않았다.
이후 사용자가 Vulkan 대응을 우선 보류하기로 결정해 현재 W1은 **DX12 기준 `done`**이다.
Vulkan 오류는 별도 보류하며 해결된 것으로 세지 않는다.
