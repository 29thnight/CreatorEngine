# N3 실제 프레임·CCT·렌더 보간 수용 계약

2026-10-23. [네트워크 계획](../plans/NetworkFrameworkPlan.md)의 N3 사내 제품 검증을 소유한다. C# 저작은 C++ 계약 `OnUpdate(dt)`를 유지하며 PrSM 계약 전까지 dt는 1/60초다. 새 생명주기 훅을 추가하지 않는다.

## 실제 실행과 시계

`render.pacing play <fps|off>`는 개발 Editor 세션의 Play 렌더 상한이다. Preferences 파일에 저장하지 않으며 off/default는 기존 Play 정책을 따른다. 편집 상한과 별도이므로 Play에서 무시되던 편집 속도 설정을 검증 근거로 삼지 않는다.

`simulation.pacing.capture <새 절대 경로> [moving]`는 committed paused Play의 명시적 진단 소비자다. existing SimulationExchange의 shared-owner adapter에 복사 값 명령 120개를 넣고 실제 host frame과 production fixed Step을 기록한다. 씬/교환 epoch를 검사하며 게임 코드를 대체하는 simulation loop나 합성 frame dt를 사용하지 않는다. 120번째 성공 tick에서 pause하고 adapter를 해제한다. 진단 출력 성공은 독립 oracle 통과와 구분한다.

주 카메라는 CCT 자식이며 local offset (0, 1.95, -10)이다. 매 프레임 camera eye·렌더 frustum·LOD는 보간 자세를, AI가 쓰는 기존 TryGetFrustum은 시뮬레이션 자세를 읽는지 각각 검사한다. GPU view도 sourceEngineFrame으로 같은 camera eye에 연결한다. 텔레포트에서만 명시적 camera cut을 기록한다.

CCT는 바닥 위에서 가속·60틱 뒤 방향 전환·30틱 점프·100틱 텔레포트를 수행한다. 강체는 중력 없이 2m/s로 이동한다. `N3PacingProbe`는 실제 C# OnUpdate의 tick/frame과 dt 범위를 기록한다. 자료는 명령 응답 한도 안에 들어오는 compact 값 배열로 반환한다.

## 판정 조건

| 항목 | 기준 |
|---|---|
| 30/60/144fps | 120틱 구간에서 초기/종료 경계를 제외한 실제 steady-clock 프레임 속도가 목표 ±5%; 실제 발행 프레임 진행도 동반 |
| 고정 틱/물리/C# | 120개 tick 순서·SDK step 증가·OnUpdate 1회씩 대응; dt=1/60 ±1e-7초 |
| FPS별 CCT 궤적 | 같은 tick 입력의 최대 위치 차이 0.0002m 미만; 강체는 시작 위치를 뺀 궤적 대조 |
| 프레임 보간 | 마지막 두 성공 tick과 alpha의 독립 선형 보간에 CCT/강체/자식 행렬 위치 오차 0.0002m 미만 |
| 0틱·다중틱 | 144fps에서 0틱 80프레임 초과 및 실제 보간 이동 60프레임 초과; 30fps에서 다중틱 35프레임 초과 |
| 불연속·수명 | native 이력 회귀 및 실제 paused GPU prepare/remove, prepare/disable/enable/동일 위치 warp 소비; 하위 메시와 부착 카메라가 완료 위치에 수렴 |
| 실제 GPU 출력 | manifest draw가 보간 위치를 소비; 실제 baseColor 실루엣이 투영한 cube bounds와 3.5px 이내; 유한값·검증 메시지 0 |
| 144fps 0틱 GPU 프레임 | 별도 moving capture의 sourceEngineFrame으로 native trace와 정확히 연결; 해당 프레임이 0틱이며 draw/픽셀 판정 통과 |

moving GPU readback은 진단 정지/파일 출력 비용을 포함하므로 정상 cadence matrix와 별도 실행한다. 순수 계산 보간만 통과한 결과나 명령의 요청 FPS를 실제 FPS 통과로 취급하지 않는다. geometry·pixel 검증은 해당 DX12 장치/작은 진단 장면의 수용이며 전체 게임의 성능 예산이나 물리 플랫폼 간 결정성을 의미하지 않는다.

## 구현과 회귀

CCT의 이전 위치는 실제 이동 전에 보존하고 마지막 두 fixed tick 위치만 표현에 보간한다. Teleport/활성 전이/재시작은 이력을 초기화한다. Scene::PhysicsRenderMatrix는 CCT와 강체 및 자식의 표현 행렬을 계산하며 simulation Transform으로 역기록하지 않는다. Scene render dirty traversal은 CCT 하위 메시를 0틱 프레임에도 게시한다. CCT disable/remove/동일 위치 warp에서 simulation Transform이 바뀌지 않아도 하위 proxy를 명시적으로 무효화한다. CameraComponent의 FrameSnapshot·render frustum·LOD와 animation/foliage/gizmo 소비자는 presentation 자세를 사용하고 AI frustum은 기존 simulation 계약을 유지한다.

```powershell
./Tools/regression/verify-physics-character-motion.ps1 -Configuration All -RequireGpu
./Tools/regression/verify-n3-pacing-product.ps1 -Configuration Release
./Tools/regression/verify-n3-pacing-product.ps1 -Configuration Release -Rates 144 -MovingQuality
./Tools/regression/run-all.ps1 -Suite NetworkHandoff -Configuration All -Product -EditorExe <새 Release Editor> -CookedStage <현재 cooked 패키지>
```

Product suite는 기존 13그룹에 cadence matrix와 moving GPU gate를 추가한다. 독립 oracle에는 실제 산출물을 입력으로 하는 격리 변이 15건을 배선하며 전후 파일 hash 불변을 확인한다. 전체 parser 라이브러리 unlink·Dedicated Server target·wire schema/권한/transport는 별도 소유다.
