# PHASE 21 W1 — DX12 DPI/resize 수정 검증

2026-09-12, HEAD `3efbb23f` 위의 W1 작업 트리. 사용자 요청에 따라 DX12를 먼저 수정했다.
[이전 원인 분석](EditorW1DpiResizeRootCauseAndFixPlan.md) ·
[정본 W1](../plans/EditorWorkspaceRedesignPlan.md).

## 원인과 수정

DX12는 창 크기가 바뀔 때 완료된 표시 결과를 무효화한 뒤 `LivePipeline`을 해체하고
모든 패스를 다시 초기화했다. 화면 크기와 무관한 셰이더 준비, ShaderMeta 적용과 IBL 생성도 반복됐다.
캐시를 유지해도 셰이더 컴파일 경로는 캐시 조회 전에 Slang 세션과 소스 모듈을 준비하므로
패스 재초기화 자체가 무료가 아니다. 표시 수명 잠금을 잡은 구축 구간과 새 결과 대기가 겹쳐
Scene에 오랫동안 준비 배경이 남았다.

크기 snapshot 동기화만 적용한 진단 실행에서도 DPI 150→100% 뒤 `result_pending`이
**8,578.926ms** 지속됐다. IBL만 유지한 후보에서도 **14,649ms**로 재현됐다.
후자는 Release 빌드와 동시에 실행해 시간의 직접 성능 비교에는 쓰지 않는다.
따라서 데이터 경쟁이나 IBL 생성 하나를 단독 원인으로 판정하지 않았다.

최종 수정은 다음과 같다.

- `ScreenResizeBus`: width/height/generation을 잠금 아래 한 번에 읽고 쓰며 동일 크기는 generation을 올리지 않는다.
  프레임 패킷·카메라 크기·진단의 width/height 소비자는 같은 snapshot을 사용한다.
- `LiveState::ResizePipeline`: GPU 완료를 기다린 뒤 표시 슬롯, 완료 그래프의 transient 풀,
  뷰별 SSGI 히스토리를 해제하고 새 크기의 표시 슬롯을 만든다. 패스 인스턴스·PSO·ShaderMeta·IBL은 유지한다.
  각 패스는 기존 `PrepareFrame`/`Declare` 경로에서 새 크기를 반영한다.
- `EnhancedSSGIPass::ReleaseHistory`: 옛 해상도의 히스토리 핸들을 실제로 해제하고 시간축 누적을 초기화한다.
  GPU 완료 뒤에만 호출하며 다음 준비에서 새 크기의 히스토리를 만든다.
- `dx12.live`: GPU 완료 generation과 UI가 실제 조회한 generation/frame, 텍스처 미준비 횟수·시간,
  IBL 생성 횟수를 함께 관측한다. `[LiveResize]`는 크기 자원 교체 시간을 기록한다.

최종 경로에서는 리사이즈가 ShaderMeta 재적용이나 IBL 재생성을 유발하지 않았다.
패스 전체 재구축 제거 전후의 소스 경로와 표시 지연 변화를 해결 근거로 삼는다.
개별 셰이더 함수의 비용을 따로 측정한 결과는 아니다.

## 최종 검증

VS18/v145, `Editor/CreatorEditor.vcxproj`, x64 Debug/Release 빌드가 모두 통과했다.
Release에는 기존 `LNK4229 /DELAYLOAD:vulkan-1.dll` 경고가 남는다.
최종 실행은 빌드 완료 후 각 구성의 새 실행 파일로 수행했다.

| 검사 | Debug | Release |
|---|---:|---:|
| 연속 창 크기 변경 | 10/10 통과 | 10/10 통과 |
| 해당 변경의 Scene 텍스처 공백 최대 | 73.920ms | 39.447ms |
| 실제 OS DPI 100→150% 공백 | 118.401ms | 95.969ms |
| 실제 OS DPI 150→100% 공백 | 118.492ms | 62.719ms |
| 리사이즈 후 IBL 생성 누적 | 1회 유지 | 1회 유지 |
| GBuffer/Forward ShaderMeta 적용 누적 | 각각 1회 유지 | 각각 1회 유지 |

Release 종료 전 원래 150%로 복원하는 추가 100→150% 전환도 통과했으며 공백은 124.179ms였다.

Scene을 계속 표시한 상태에서 1440×900, 1100×700, 1600×900, 1000×640, 1280×800을
두 차례 요청했다. 실제 client 크기는 명령 응답과 display snapshot에 기록했다.
각 단계에서 새 generation의 GPU 완료 결과와 UI 텍스처 조회를 확인했다.
명령 호출부터 확인 응답까지의 시간과 표의 텍스처 공백은 서로 다른 지표다.

사용자 배율은 1.5로 유지했다. OS 100/150%에서 본문은 24/36px이며
`editor.theme` clean, Material Symbols 59역할·누락 0을 확인했다.
Scene의 하늘·그리드·기즈모와 Game의 하늘 표시를 화면으로 확인했다.
정상 종료까지의 로그에서 device loss, D3D12 error, assertion, 렌더 프레임 실패를 찾지 못했고 stderr는 0바이트다.

추가로 `verify-screen-resize-snapshot.ps1 -Configuration All`이 통과했다.
각 구성에서 두 writer의 40만 쓰기와 세 reader의 60만 읽기로 크기 쌍·generation 일관성을 확인하고,
중복 크기·콜백 재진입·높이 0의 종횡비도 검사했다.

## 증거와 범위

실행 산출물은 `Artifacts/phase21-w1-dx12-fix/`에 보관한다.

- `build-verified-debug.*`, `build-verified-release.*`: 최종 빌드 기록.
- `verified-debug-resize.json`, `verified-release-resize.json`: 단계별 크기·generation·표시 확인.
- `verified-*-dpi150.json`, `verified-*-dpi100-return.json`: 실제 DPI 전환의 표시 관측.
- `verified-*-theme*.json`, `verified-*-dpi*.png`, `verified-*-game*`: 폰트·Scene/Game 화면 증거.
- `verified-debug.out/.err`, `verified-release.out/.err`: 실행과 종료 로그.
- `restore-proof.json`: 설정 파일과 두 구성의 imgui.ini를 원래 바이트로 복원한 결과.
- `diagnostic-dpi100-recovered.json`, `fixed-debug-dpi100-display-late.json`: 해결 전과 IBL 단독 후보의 실패 증거.

`lastMissingTextureMs`는 UI 텍스처 조회 사이의 공백이다. GPU present 시간이나 픽셀 단위 무검정 보장은 아니다.
숨겨진 탭은 조회가 중단되므로 시작 generation의 최대 공백을 resize 결과에 포함하지 않았다.
검증 씬은 기본 SampleScene이며 무거운 모델·모든 후처리 조합·장시간 실행의 성능 보장은 범위 밖이다.
최초 기동의 셰이더/IBL 준비 지연은 이번 크기 변경 수정과 별개다.

Windows 1번 모니터 배율은 원래 150%로 복원했고 2번 모니터 설정은 변경하지 않았다.
`EngineSettings.asset`과 Debug/Release `imgui.ini` 3개 파일 모두 실행 전 바이트와 일치한다.
검증용 Editor·설정·제어판 창은 종료했다.

DX12의 장시간 Scene 표시 지연은 이 재현 조건에서 해결됐다.
**후속 사용자 결정(2026-09-12): Vulkan 대응은 우선 보류하고 W1을 DX12 기준 `done`으로 판정한다.**
Vulkan 모니터 경계 resize 후 device loss는 미수정·미재검증으로 별도 보류한다.
이번 상태 변경은 완료 범위의 조정이며 Vulkan 오류 해결이나 새 검증 결과를 뜻하지 않는다.
