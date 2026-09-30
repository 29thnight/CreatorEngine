# Material Graph 가시 픽셀 수집

## 1. 구현 범위

`RasterSurfaceCollector`는 제품 RHI와 `EnhancedRenderGraph`로 불투명 삼각형을
래스터하고, 깊이 검사에서 살아남은 **픽셀 위치의** UV·world position·normal·tangent·
bitangent를 재질 평가에 전달한다. 정점 재질이나 정점 IBL 결과를 보간하지 않는다.

현재 범위는 한 재질의 mesh chunk 묶음, 독립 D32 depth, 단일 viewport이며 최대
4096픽셀·64 draw다. viewport 전체를 dense buffer로 전달한다. 이는 정확성 검증용
제품 RHI 기반 패스이며 전체 화면 실시간 Scene 설치가 완료됐다는 뜻은 아니다.

`EnhancedSceneRenderer`의 draw pool·공유 Scene depth·조명/컬러·투명 합성·async 게시에
아직 설치하지 않았다. 서로 다른 재질·legacy draw 사이 가림도 독립 depth로 해결되지
않는다. 기존 Scene의 LX 미설치 거부 경계는 유지한다. MAT-7은 진행 중이다.

## 2. 준비와 기록

1. `MeshSurfaceInput`을 밀봉하고 `MeshSurfaceEvaluator::Record`로 world frame을 만든다.
2. `Prepare`는 카메라 행렬·viewport·texture footprint·동일 view revision·unsampled
   source·예산을 검사하고, MRT 6장·depth·출력 buffer·descriptor·constant/index upload를
   모두 준비한다. 실패하면 이전 batch를 보존한다.
3. batch의 `Declare`를 동일 device/recording/descriptor generation의 graph에 한 번
   호출한다. graph callback은 owner를 보유하고 전달된 encoder만 사용한다.
4. `LX.OpaqueRasterCapture`에서 MRT/depth를 기록하고 `LX.ResolveVisibleSurface`가
   dense `SurfacePoint` buffer를 만든다. `LX.VisibleSurfaceReady`가 외부 소비를 위한
   ShaderResource 상태와 그래프 보존을 선언한다. 전이는 graph가 관리한다.
5. 같은 graph에 surface·IBL packet을 준비/선언하고 순차 또는 병렬로 기록한다.
   자세한 prefix·generation·resource identity·retirement 계약은
   [MaterialGraphPassRecording.md](MaterialGraphPassRecording.md)에 있다.
   GPU 단계 사이 CPU readback이나 대기를 넣지 않는다.
6. GPU 제출/완료까지 batch·source·material·environment owner를 유지한다.
   readback acceptance는 vertex → raster pixels → material 순서다.

Prepare/Declare/Record 경계를 넘어 native prefix가 바뀌면 기존 transient binding을
사용하지 않는다. generation 검사 실패는 graph 기록 실패와 ready=false로 전달된다.
host는 제출/게시를 중단해야 한다. 전체 graph 기록 성공과 ready는 GPU 완료/readback
acceptance를 대체하지 않는다. graph packet은 한 번 기록하는 계약이다.

## 3. 깊이·미분·배경

MRT write는 하드웨어 depth test/write를 따른다. 픽셀 셰이더에서 structured UAV에
직접 쓰지 않아 가려진 삼각형이 결과를 덮는 경쟁을 피한다. normal/tangent frame도
원근 보간 후 전달하며, zero frame·nonfinite·back-facing material은 기존 평가/검증
정책으로 거부한다. 근평면 clipping과 앞뒤 면 제거는 graphics pipeline이 처리한다.

UV의 `ddx_fine/ddy_fine`와 **한 texture**의 실제 extent/mip/bias로 explicit LOD를
구한다. `RHIShaderCompileOptions::fineDerivatives`는 SPIR-V DerivativeControl을
명시적으로 요청하며 compiler cache identity에 포함된다. 일반 셰이더에 자동으로
기능을 허용하지 않는다. frame MRT 6번째는 이 미분을 진단용으로 보존한다.

GraphContext의 현재 LOD는 공용 한 값이다. texture마다 다른 크기·UV 변환·anisotropic
footprint를 자동 처리한다고 해석하면 안 된다. host가 실제 texture와 footprint가
일치함을 보장해야 한다.

배경은 `SurfacePoint.position.w=-1`, 나머지 19개 float는 0이다. graph는 명시적
`viewTier.w=-1`과 zero payload를 출력하고 IBL은 기존 rejection marker와 zero radiance를
유지한다. 완료 raster validation에서 수용한 coverage만 배경 판단에 사용한다.
가시 픽셀의 잘못된 재질 출력을 배경으로 숨기지 않는다.

## 4. 검증

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`가 독립
native D3D12 검사를 빌드/실행한다. 최초 독립 수집 게이트의 2026-09-28 Debug/Release 결과는 다음과 같다. 이후 병렬 기록 확장 증거는 MaterialGraphPassRecording.md가 소유한다.

| 항목 | 구성별 결과 |
|---|---|
| 검사 / GPU 성분 | 408,806 / 392,560 |
| 래스터 프레임 / DXIL·SPIR-V artifact | 7 / 22 |
| 가시 픽셀 / 배경 픽셀 | 2,435 / 6,269 |
| 근평면 clipping 뒤 노출된 뒤쪽 픽셀 | 63 |
| fractional LOD 픽셀 | 1,198 |
| 최대 정규화 오차 / SRGB 오차 | 0.0000859499 / 0.000689566 |
| 최대 LOD 절대 오차 | 0.000169724 |
| D3D12 GPU validation WARNING 이상 | 0건 |

독립 double screen barycentric/reciprocal-W reference로 world/UV frame과 픽셀 coverage,
2×2 fine UV derivative를 대조한다. D3D top-left edge ownership도 적용한다.
Core/Layered 재질은 이 검증을 거친 GPU 픽셀 입력을 독립 CPU 식에 넣어 대조하며,
분산된 가시 픽셀의 IBL 적분도 CPU reference와 비교한다.

물리 계산·frame/미분의 정규화 오차 기준은 0.0001, SRGB는 0.001이다.
클리핑/래스터 보간의 finite difference가 `log2`에 확대되는 LOD transport만 별도로
절대 0.0005 이내를 검사한다. 물리 계산의 허용 오차를 확대하지 않는다.

겹친 삼각형, 두 chunk의 깊이 가림, clipping, 역방향 winding/양면/뒷면 제거,
4096픽셀 상한, singular camera/잘못된 mip/중복 source 거부, 미기록 입력과 배경 payload
오염 거부, 모든 chunk의 검증 순서, 실패 시 결과 보존, 준비 중 native prefix 전환과
abort 후 재사용 거부를 검사했다. DerivativeControl이 없는 요청이 compiler cache를
통해 기능을 얻지 못하는 것도 검사한다.

원본 로그는 `Build/Obj/MaterialProductProbe/raster-surface-gate-final.log`,
`raster-surface-{build-,}{Debug,Release}.log`에 있다. GPU runtime은 D3D12이며
Vulkan은 artifact 컴파일만 확인했다. 전체 Editor 빌드/실제 Scene/병렬 기록 검증은
이번 증거에 포함하지 않는다.

공용 geometry source/배경 ABI 변경의 회귀로 기존 MeshSurface gate의 Debug/Release 각
621,160개 검사·GPU 286,156성분과 SurfaceBatch gate의 각 19,387개 검사·GPU 19,092성분도
통과했다. 로그는 같은 폴더의 `raster-mesh-regression.log`, `raster-surface-regression.log`다.
프로젝트 XML·PowerShell parser·저장소 포맷·dashboard JavaScript/변경 범위 공백 검사를
통과했다. 별도 전체 dashboard checker와 기존 미해결 실패 항목은 이번 판정에 포함하지 않는다.

## 5. 다음 설치

current mesh producer와 여러 재질의 read-only depth·coplanar winner 검증은
[MaterialGraphSharedDepth.md](MaterialGraphSharedDepth.md)에 확장했다. 아래 실제 Scene 호출과
legacy/Masked depth ownership 설치는 계속 남는다.

- 실제 Scene draw pool/per-view snapshot과 공유 depth/coverage ownership.
- 실제 컬러·직접광·Core/Layered ambient 소비 및 기존 SSAO/SSR/투명 pass 순서.
- texture별 footprint ABI, 전체 화면 해상도·lookup 재사용·오차·시간/메모리 예산.
- 실제 Scene 병렬 pass 설치·async 완료/진단 게시, 환경 eviction/device recreation.
- Special 합성 및 raster host artifact의 자동 cooker dependency closure.

MAT-7 완료 공수는 추가하지 않는다.
