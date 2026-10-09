# RenderDoc 백엔드 비교 계획 (PHASE 4.9)

**정본 2026-10-01 · BP-0~5 미착수 · 6행 22인일 · DX12/Vulkan 비교의 단독 소유자.**

2026-09-15의 사용자 결정을 재확인한다. RHI 중립화와 backend 구현은 이미 공통 구조의 책임이다. Vulkan과의 실행·리소스·픽셀 비교를 다른 페이즈의 완료 조건으로 다시 넣지 않는다. 다른 페이즈는 DX12에서 구현 전후를 검증하고, 비교할 기능의 DX12 기준선과 캡처 입력을 이 계획에 인계한다. 4.9의 결과를 BASE-0, RG6, MAT-9, TR/TU/FG, CSRP, 라이트맵, renderer 품질 또는 GPU 설계의 착수 선행으로 역연결하지 않는다.

## 1. 수행 순서

### 2026-10-07 graph-only 재질 전환 인계

live Shadow/GBuffer/Forward의 native 재질 대체와 이전 graph instance 선택을 제거했다. DX12 D/R 장면 검사와 Release 전체 raster·shadow/decal·refraction·SSS·volume 검사는 통과했다. [변경 및 증거](../analysis/GraphOnlyMaterialPasses20261007.md). Release Vulkan probe는 빌드를 통과했지만 `--scene-only` 최초 재질의 `WaitSceneProgram` 준비 단계에서 `Scene asynchronous preparation`으로 exit 1, 첫 장면 fixture 실행 전에 종료했다. 이 검사의 SPIR-V 준비 제한은 600초이며 구체적인 shader/driver 오류 문자열은 비어 있다. 실행·픽셀·validation 수용은 미완료다. 실패 로그는 `Build/Verification/GraphOnlyMaterials20261007/scene-Vulkan-Release.log`에 보존한다.

BP-0/1에서 같은 입력의 cold pipeline 준비 위치와 worker/PSO 단계별 시간을 분리하고 재현한다. 준비 실패/제한 초과를 구분한 뒤, 준비 완료 상태에서 새 Shadow/GBuffer clear와 exact-generation 거부·graph 재질의 실제 리소스/픽셀을 검사한다. 타임아웃 증가나 native/이전 재질 fallback 복원으로 수용하지 않는다. 이 Vulkan 잔여를 RG6의 기존 DX12 수용 선행으로 역연결하지 않는다.

1. 동일 입력을 밀봉한다. 정지 fixture는 BASE-0의 자산·카메라·광원·재질·샘플·히스토리 초기화 계약을 재사용한다. 움직이는 fixture는 simulation tick, delta, jitter seed, 애니메이션 pose, 이전 프레임과 history warmup 길이까지 기록한다. 재현 불가 입력은 비교 전에 거부한다.
2. **RenderDoc으로 DX12와 Vulkan의 실제 제품 프레임을 각각 캡처한다.** 사용한 RenderDoc 버전·GPU·driver·binary와 `.rdc`를 보존한다. 두 backend의 event 번호는 같다고 가정하지 않고 Pass 이름/역할/입출력으로 대응표를 만든다. 캡처 파일을 열어 재생할 수 있어야 한다.
3. **리소스를 먼저 확인한다.** 대응 event의 geometry/index/vertex 입력, constant buffer 값·layout, texture format/extent/mip/layer/색공간/초기값, SRV/UAV/RT/depth binding, sampler, PSO, blend/cull/depth, 이전 history·barrier/ownership을 추적한다. 잘못된 리소스나 입력을 이미지 허용치로 덮지 않는다.
4. **확인된 대응 리소스를 추출해 픽셀별로 비교한다.** GBuffer, shadow/depth, 직접광·IBL HDR, SSGI/fog/history, transparent/SSS/transmission/volume, pre-tone HDR, post/display를 단계별로 대조한다. 선형 float 원본을 보존하고 동일한 색공간·채널·depth 규약으로 명시적으로 해석한다. LDR 스크린샷만으로 수용하지 않는다.
5. 최초 불일치 Pass·리소스·픽셀 좌표를 원인과 연결하고 수정한 뒤 **같은 입력으로 RenderDoc 재캡처**한다. DX12 기존 기준선도 확인한다.
6. Debug/Release·지원 장치의 독립 반복과 실패 변이를 검증하고 재현 패키지를 남긴다. 캡처 도구가 개입한 GPU 시간을 일반 성능 개선의 증거로 사용하지 않는다.

## 2. 작업과 공수

1인 전담 엔지니어의 계획 추정치다. 현재 알려진 비교 표면에 대한 예산이며 실측 완료 공수가 아니다. 알려지지 않은 결함이 예산을 넘으면 원인별로 별도 산정한다. 실제 GPU 기능의 신규 구현 비용은 포함하지 않는다.

| ID | 작업 | 선행 | 인일 | 종료 산출물 |
|---|---|---|---:|---|
| `BP-0` | 비교 입력·시간축·지원 행렬 고정 | BASE-0 및 각 기능의 DX12 기준선 | 3 | scene/asset/material/카메라/광원/seed/노출/해상도/히스토리 프레임열과 binary·driver 신원 고정 |
| `BP-1` | RenderDoc 양 backend 실제 프레임 캡처 | BP-0 | 3 | DX12/Vulkan 별도 프로세스의 .rdc와 event/Pass 대응표, 캡처 재생 성공 및 반복 캡처 |
| `BP-2` | Pass별 입력·출력 리소스와 바인딩 대조 | BP-1 | 5 | format/extent/mip/layer/채널/상수/descriptor/sampler/PSO/depth·blend·cull/수명 확인, 최초 불일치 event 특정 |
| `BP-3` | 선형 리소스 추출·픽셀별 수치 비교 | BP-2 | 4 | GBuffer/depth/HDR/history/final의 RMSE·최대 오차·초과 좌표·차영상, NaN/Inf와 alpha 별도 검사 |
| `BP-4` | 불일치 원인 수정과 RenderDoc 재캡처 | BP-3 | 5 | 확인된 backend/binding/state/history 결함 수정 후 같은 입력으로 재캡처, 기존 DX12 기준선 회귀 확인 |
| `BP-5` | 반복·실패 변이·회귀 패키지와 수용 보고서 | BP-4 | 2 | Debug/Release·지원 장치별 결과, 누락/손상/다른 입력 거부, 미지원과 미통과 분리, .rdc/리소스/비교 결과 보존 |
| **합계** | | | **22** | 완료 0 / 잔여 22 |

## 3. 이관 목록

2026-10-08 신설 [PHASE 4.85](PathTracingHybridPipelinePlan.md)의 RT/PT/Hybrid·ReSTIR PT Enhanced·tetrahedral cages는 지원 가능한 구현이 DX12 기준선·fixture·raw radiance/history/AS 신원을 확보한 뒤 비교 대상으로 인계한다. 본 문서의 기존 22인일에 신규 RT 연구/기능 구현이나 미확인 교차 비교 비용을 포함한 것으로 해석하지 않는다. 지원 행렬·캡처 도구 제약·추가 비교 범위를 BP-0에서 확인하고 필요 공수를 별도 산정한다. 4.9 완료는 4.85 DX12 착수/수용의 선행이 아니다.

| 인계하는 페이즈 | 4.9에서 비교할 대상 | 원 페이즈 완료 조건 |
|---|---|---|
| 4 / 4.25 | PBR, Standard/LX, GBuffer/Deferred/Forward, material routes, SSS·투과·volume, `vk.shadow/gbuffer/forward/deferred` | DX12 배선·Blender/reference 의미·품질·성능. Blender 비교를 Vulkan 비교와 혼동하지 않음 |
| 4.3 BASE-0 / RG6 / RG8 | sealed frame, compiled graph, resource lifetime/barrier, history·queue·aliasing, 현재 SSGI 잔여 차이 | DX12 재현·변이·제품 전후 회귀와 RHI 중립 계약 |
| 4.5 TR/TU/FG | motion·jitter·history·upscale·FG 입력/출력, Vulkan SDK 지원·proxy present/fallback | DX12 기능·지원 하드웨어·지연/실프레임 성능 |
| 4.6 CSRP | C# IR → native node/slot/output·reload/fence | DX12 C++/C# 조립 전후 동등성 |
| 4.7 | bake 입력·UV1/geometry·직접/간접광 출력·큐 | DX12 베이크 정확도·취소/재개·Editor 비차단 |
| 4.75 / ENV | probe/AO·shadow·post/display·환경의 background/IBL 의미·package | DX12 기능별 golden·성능 |
| 4.8 후속 구현 | GPU Scene·indirect·meshlet·RT capability/fallback과 실제 출력 | 설계는 중립 계약과 구현 공수; 실제 기능은 별도 DX12 구현 게이트 |
| 4.85 | RT scene/AS·hit material·Hybrid/PT radiance·denoise/reservoir·cage geometry·지원/fallback | 두 파이프라인 DX12 수용과 연구별 판정; Vulkan 실행 비교는 본 페이즈에만 인계 |
| Editor/기타 렌더 소비 페이즈 | viewport/UI·DPI/resize·present의 Vulkan 화면/리소스 차이 | 각 페이즈의 DX12 제품 게이트 |

기존 자동 비교기와 `vk.*` 검사는 보조 자료로 재사용한다. 원 페이즈의 기본 실행을 `dx12,vulkan`으로 되돌리지 않는다. `.f32` 자동 비교만으로 이 계획의 RenderDoc 리소스 확인을 통과시킬 수 없다.

## 4. 수용 기준과 보존할 증거

- 같은 입력의 같은 backend 반복이 먼저 성립해야 한다. 지원 불가·캡처 실패·validation 오류·리소스 누락·nonfinite를 PASS나 비교 제외 픽셀로 숨기지 않는다.
- 기존 HDR 상한 `0.002 + 0.005 * max(abs(a), abs(b))`를 유지한다. 리소스별 원래 승인 기준과 encoding을 보고서에 명시하고 실패를 통과시키기 위해 완화하지 않는다.
- 픽셀별 절대/상대 오차·RMSE·최대값·초과 개수/좌표·차영상과 최초로 갈리는 Pass를 기록한다. history 배열 번호나 event 번호의 물리 차이는 역할 대응으로 설명하며 잘못된 바인딩을 정규화하지 않는다.
- 모든 수용 항목에 입력 manifest, `.rdc` 두 벌, event/리소스 대응표, 원본 추출 데이터, 수치 결과, Debug/Release·장치 신원, 수정 전후 재캡처 경로를 연결한다.
- 미지원 기능은 capability와 fallback 근거로 별도 기록한다. Vulkan에 없는 기능을 조용히 생략하고 동등하다고 판정하지 않는다.

## 5. 인계받은 관측 기록

2026-09-14~19의 sampler/시각 고정/기동 `gCubeMap`/`vk.*` 기록은 [PBR 계획](PBRWiringStabilizationPlan.md)의 해당 날짜를 따른다. 오래된 기동 결함은 4.9 착수 시 현재 binary에서 재현 여부를 확인하고 수정 완료로 가정하지 않는다.

2026-10-01에 BASE-0에서 범위를 벗어나 수행한 비교와 수정은 [BASE-0 기록](../analysis/RenderBase0Baseline.md)에 보존한다. history guard 수정 뒤 Debug/Release 교차 잔여는 HDR 466픽셀(max 0.435546875), display 186픽셀(max 0.011764705)이며 SSGI 이후 시작한다. 이는 RenderDoc 검증 전 참고 자료다. **4.9는 미착수이며 이 기록을 BP 완료 공수로 올리지 않는다. BASE-0의 실패 조건으로도 사용하지 않는다.**
