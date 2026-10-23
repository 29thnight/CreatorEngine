# TR3 — view별 history·SSGI 수명 및 복구 수용

> 2026-10-23 · master `0509753a380866e8dd96bc648fd71c5c5d8792e2` + 기존 로컬 변경 보존 · Debug/Release DX12

## 구현

기존 `EnhancedTemporalViewState`와 CameraView별 `EnhancedSSGIPass`, `PassResourceRetirement` 수락/중단 통지를 사용했다. history 저장소나 제출 체계를 중복 구현하지 않았다.

- `Begin`의 카메라 역행렬·display extent·view ID 검사를 설정 및 history 변경 앞으로 옮겼다. 잘못된 projection 입력으로 실패해도 이전 제출 camera·jitter·history 세대가 유지되고 다음 정상 입력이 연속 history로 복구된다.
- SSGI reset마다 history 세대를 전진한다. reset 이전에 기록된 그래프가 수락되더라도 이전 세대 콜백은 camera·누적 수·ping-pong 슬롯을 되살리지 않는다.
- 기존 성공 제출 Commit, 뷰별 이전 입력, cut·lens·scene·view 교체·explicit reset 정책은 유지했다.

## 실행 결과

| 검사 | Debug | Release |
|---|---:|---:|
| CreatorEditor 빌드 오류 | 0 | 0 |
| 실제 view state 검사 | 21 | 21 |
| native GPU 픽셀 비교 | 16 | 16 |
| 비교한 RGBA scalar | 999,424 | 999,424 |
| native 최대 픽셀 오차 | 0 | 0 |
| 실제 제품 장면 캡처 / 비교 쌍 | 13 / 6 | 13 / 6 |
| SSGI / HDR / 최종 화면 최대 오차 | 0.0 / 0.0 / 0.0 | 0.0 / 0.0 / 0.0 |
| DX12 validation messages / problems / dropped | 0 / 0 / 0 | 0 / 0 / 0 |
| 검증 프로세스 정상 종료 | exit 0 | exit 0 |

3개 logical view의 이전 camera·세대를 검사했다. 일반 이동·유효 준비 중단·잘못된 projection 실패/복구·FOV·실제 projection matrix·orthographic 전환·cut·scene 교체·history revision·view 재배정·explicit reset·invalidate·frame 역행을 포함한다.

GPU 검사는 서로 다른 카메라와 trace 설정의 두 SSGI 인스턴스를 교대로 실행하고 각각의 독립 실행과 비교한다. 한쪽 뷰만 4번째 프레임 뒤 reset한 경우도 별도로 비교한다. prepare/declaration/recorded graph 중단 및 reset 이전 기록 수락 뒤의 8프레임 결과를 독립 대조군과 비교했다. cut·resize-down/up·extent-only 재생성은 첫 프레임과 8번째 프레임을 새 pass와 비교한다.

제품 장면은 독립 프로젝트의 바닥·큐브·광원과 실제 Game view를 사용했다. 움직인 카메라가 depth·HDR·최종 RGB를 바꾼 것을 확인한 뒤 cut과 두 창 크기의 첫/8번째 프레임을 비교했다. raw GPU attachment·manifest·행동 영수증·binary hash를 보존했다.

## 검증 도구 수정

`Verify-LiveSSGIRecovery.ps1`은 시작 창이 목표와 같을 때 다른 baseline 크기를 선택한다. 고정 0.5초 대기를 실제 baseline view 제출 확인으로 바꾸고, RT capture ready를 확인한 뒤 동작을 실행한다. 초기 검사의 `viewport did not resize`는 resize 영수증 뒤 이전 크기의 프레임을 읽던 검사 순서 문제였다. 실패 기록도 보존했다.

라이브 실행은 현재 commandlet 전용 `model.async` 대신 기존 project fixture·`scene.switch`·request ID 기반 `scene.load.status`를 사용한다. 바닥을 카메라 아래에 두고 큐브를 추가해 빈 history가 통과하지 않도록 했다.

## 재실행

```powershell
Tools/regression/verify-tr3-history.ps1 -Configuration Debug -OutputDirectory Artifacts/TR3-New-Native-Debug
Tools/regression/verify-tr3-live-history.ps1 -Configuration Debug -OutputDirectory Artifacts/TR3-New-Live-Debug
```

Release는 Configuration만 바꾼다. 기존 `Verify-SSGIAbortRecovery.ps1`과 `Compare-LiveSSGIRecovery.py`를 그대로 진입점/픽셀 oracle로 활용한다.

## 수용과 다음 작업

TR3 5/5·기존 산정 5인일을 수용했다. SDK별 전체 품질·material route·해상도·비용은 TR2/TU 항목에서 계속 추적하고 Vulkan 장치 실행은 PHASE 4.9에서 추적한다.

빌드 경고는 Debug MSB8027 1건, Release MSB8027 1건과 기존 Utility_Framework PDB type record LNK4020 8건이다. Release 디버거의 일부 형식 정보가 손상된 경고이며 실행 픽셀 검사는 위 결과를 통과했다.

[수치 및 바이너리 신원](TemporalTr3History20261023.json) · [진척 집계](Phase4Progress20261022.html) · [소유 계획](../plans/TemporalReconstructionPlan.html)

증거: `Artifacts/tr3-20261023/native-{Debug,Release}-final/native/results.json`, `live-{Debug,Release}-validated/recovery/{pixels,recovery}.json`, `result.json`, `source-freeze.json`, `before/`.
