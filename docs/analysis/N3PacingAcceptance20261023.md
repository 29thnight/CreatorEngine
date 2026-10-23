# N3 실제 FPS·CCT·렌더 보간 수용 — 2026-10-23

N3 사내 선행 수용을 완료했다. [기계 판독 기록](N3PacingAcceptance20261023.json) · [판정 계약](../design/SimulationPacingAcceptanceContract.md) · [네트워크 계획](../plans/NetworkFrameworkPlan.md).

## 수정과 기존 소비자

기존 공통 clock/C++ 계약 OnUpdate(1/60초)/API43 명령·성공 commit 구조를 유지했다. 강체에만 있던 렌더 보간을 CCT에도 연결했다. ScenePhysicsSimulation은 실제 이동 전 위치를 보존하고 Scene::PhysicsRenderMatrix가 이전/현재 tick 위치와 공통 alpha로 표현 행렬을 계산한다. simulation Transform은 보간 값으로 덮어쓰지 않는다. CameraComponent의 FrameSnapshot·render frustum·LOD 및 animation/foliage/gizmo 소비자는 같은 표현 자세를 사용하고, AI가 쓰는 기존 TryGetFrustum은 simulation 자세를 유지했다. CCT에 부착한 카메라의 eye/LOD·두 frustum과 실제 GPU view를 프레임마다 확인한다.

Scene render dirty traversal은 CCT 하위 메시를 0틱 프레임에도 게시한다. 텔레포트·disable/enable·재시작·stop에서 이전 공간을 끌고 오지 않도록 이력을 처리했다. CCT 제거·활성 전이·동일 위치 warp는 simulation Transform이 바뀌지 않아도 하위 proxy를 명시적으로 무효화한다. paused prepare/remove 및 prepare/disable/enable/warp의 실제 GPU draw·카메라·픽셀 소비를 별도로 통과했다. existing 강체 경로와 자식 affine 행렬 경로는 유지했다. 기존 CCT movement·Scene render proxy·C# OnUpdate·cooked Player 소비자를 함께 검증했다.

Play에서 무시되던 편집 FPS 설정 대신 `render.pacing play <fps|off>` 세션 상한을 연결했다. Preferences 파일 불변을 검사한다. 실제 host wall-clock/발행 프레임·C# tick/frame·SDK step을 기록하며 합성 frame dt를 넣지 않는다. 수용 조건은 목표 ±5%다.

## 실제 FPS·궤적·품질

동일한 120개 copied-value tick 명령으로 바닥 위 가속·방향 전환·점프·텔레포트 CCT와 이동 강체를 비교했다. 카메라는 CCT 자식이며 local offset은 (0, 1.95, -10)이다. isolated 640×480 Editor 창, 실제 Game render 332×114, DX12, temporal upscale none이다. 큰 장면의 성능 예산을 측정한 결과가 아니다.

| 목표 FPS | 실제 FPS | 0틱 프레임 | 다중틱 프레임 | 최대 CCT 보간 오차 m |
|---|---|---|---|---|
| 30 | 30.00 | 0 | 60 | 1.14e-07 |
| 60 | 59.51 | 5 | 6 | 1.19e-07 |
| 144 | 143.10 | 163 | 0 | 1.19e-07 |

60/144fps와 30fps 사이의 같은 tick CCT 위치 및 시작 위치를 뺀 강체 궤적 차이는 모두 **0m**다. C# OnUpdate와 성공 tick/SDK step은 각 120개씩 대응하고 dt 범위를 검증했다. 144fps의 0틱 프레임 163개에서 실제 보간 위치가 움직였다. 최대 GPU 실루엣 bounds 오차는 정상 행렬에서 0.365px다.

별도 moving GPU 실행은 capture 비용을 정상 cadence와 분리한다. 실제 frameId 467 / sourceEngineFrame 467을 native trace와 연결했고 그 프레임이 0틱임을 확인했다. manifest draw 위치와 독립 투영 실루엣·유한값·GPU validation 0을 통과했다. 이 실행의 평균 FPS는 readback 비용 때문에 정상 cadence 통과로 계산하지 않는다.

## 회귀와 새 Player

- NetworkHandoff **15그룹 통과**, 실패 0: `Build/Verification/NDA/Suite-9883ae0a29ce4ae3897c4788716cdd3a/result.json`.
- CCT SDK 운동/보간·0틱·불연속·활성·stop·stale/thread 경계: Debug/Release/ASan 각 2304, Shipping 2302, 총 **9214검사**와 실제 GPU scene 경로 통과. ASan 검사도 통과했다.
- 실제 산출물을 입력으로 한 oracle 격리 변이 15건: 누락/중복 tick·SDK step·frame dt·가짜 FPS·CCT/자식/카메라 보간 제거·AI frustum 역류·stale GPU draw·제거 후 proxy 잔류·validation·투영 변이를 거부했다.
- Product runner 격리 검사: 15개 그룹 모두 필수 실행하며 cadence/moving gate 각각의 exit37이 전파된다. 이 stub 검사는 실제 제품 수용과 별도다.
- 새 SDK로 primitive CCT 프로젝트를 cook/package/smoke하고 실제 Player CCT **12검사**, 정상 종료 0, 패키지 **239파일 불변**을 확인했다. 전체 suite의 N1 Player 검사에서도 실제 text parser 호출 0을 검증했다.
- Editor·Player·AssetCooker·AssetPacker Release를 새로 빌드했다. 원격 CI는 배선했으며 실행 완료를 주장하지 않는다.

Editor runtime SHA256: `7ED8F5EB3C664D562028571914BF92AA37BB3171000AF32280FED8389F57546A`. Player runtime SHA256: `EC24874F247D62942278188EE16EC45E0714E3F708117D1181BB65673073505E`. 상세 source hash·SDK·개별 산출물 위치는 JSON에 보존한다.

추가 기존 소비자 회귀: 카메라 C# 경계 15건, 실제 CreatorRobot animation 100 actors × 12 rounds / 234835검사·managedThreadErrors 0, CCT HTTP DDOL 이송을 통과했다. Native 9214검사는 ScenePhysicsSimulation/Physics SDK 경계의 ASan 수용이며 전체 Editor ASan 수용을 의미하지 않는다. 기존 소비자 파일·행 inventory는 JSON에 보존한다.

## 보존한 실패와 수용 한계

수정 전 CCT 위치 보간 오차 최대 약 6.86cm를 실제 실행에서 재현했다. 초기 재질 예열 실패와 144 목표에서 135.42/136.79fps였던 큰 화면 실행은 통과로 계산하지 않았다. 원본 증거를 보존하고 작은 진단 장면에서 다시 수용했다.

기존 primitive Player fixture가 사용하지 않는 Prim_Cube를 함께 cook하던 경로는 Slang E41012/SPIR-V capability profile 오류로 실패했다. 원본 로그 `Build/n3-player-package.log`를 보존했다. `-NoUnusedModel`은 primitive CCT 전용 패키지에서 미사용 모델만 제외하며 기존 mesh/geometry fixture 기본 동작은 유지한다. 이 결과를 범용 모델/셰이더 패키지 수용으로 확대하지 않는다.

전체 서버 target·wire ID/schema·권한·replication/transport, 다른 GPU/API 및 TAA/FSR/DLSS/FG 품질·장시간 성능 수용은 별도다. 이전 seven-route Temporal Motion fixture 준비 실패도 이번 전용 N3 gate와 구분한다. 전체 문서 index의 기존 RHI src HTML drift는 별도이며 이번 변경의 문서 검증 범위를 구분한다.

문서 검증: 대상 8페이지 로컬 링크 156건 누락 0, 전체 inline script 670페이지/350개 실패 0, Dashboard 453항목 full parse/render 통과, 계획 4종 HTML 재생성 및 390/1280 visual check 통과. 전체 문서 index의 기존 RHI drift와 구분한다.
