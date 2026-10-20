# Core/Layered evaluated-point IBL bake

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang#L16) — `[shader("compute")][numthreads(32, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang#L22) — `if (!IsValidPrincipledIblBakePoint(point))`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang#L29) — `const PrincipledSurface material = EvaluateIblBakePoint(point);`
- [Engine/RenderEngine/MaterialGraphIblBake.h:83](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphIblBake.h#L83) — `bool RecordCommands(RHIEncoder& encoder, std::string& error) const;`
- [Engine/RenderEngine/MaterialGraphIblBake.h:120](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphIblBake.h#L120) — `bool Record(IRenderDeviceServices& device, const IblEnvironment& environment, std::span<const IblBakePoint> points,`
- [Engine/RenderEngine/MaterialGraphIblBake.h:127](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphIblBake.h#L127) — `bool RecordGpu(IRenderDeviceServices& device, const IblEnvironment& environment,`
- [Tools/regression/verify-material-ibl-bake.ps1:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-ibl-bake.ps1#L8) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-material-ibl-bake.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-ibl-bake.ps1#L9) — `if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {`
- [Tools/regression/verify-material-ibl-bake.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-ibl-bake.ps1#L10) — `$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'`
- [Tools/regression/verify-principled-layered.ps1:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-principled-layered.ps1#L7) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-principled-layered.ps1:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-principled-layered.ps1#L39) — `if ([Math]::Abs($actual[$index] - $expected[$index]) -gt 0.0000001) {`
- [Tools/regression/verify-principled-layered.ps1:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-principled-layered.ps1#L46) — `$tableText = [IO.File]::ReadAllText($tablePath).Replace("rn", "n")`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang#L8) — `struct PrincipledIblBakePoint`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang#L23) — `struct PrincipledIblBakeSample`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang#L36) — `bool PrincipledValidBakeVector(float4 value)`
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h:82](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRenderDeviceServices.h#L82) — `virtual uint64_t GetCurrentUploadRecordingId() const = 0;`
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h:428](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRenderDeviceServices.h#L428) — `!assets::IsSupportedModelVertexLayout(vertexAttributeMask) ||`
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h:429](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/IRenderDeviceServices.h#L429) — `vertexStride != assets::StrideOf(vertexAttributeMask) ||`
- [Engine/RenderEngine/MaterialGraphSurfaceBatch.h:140](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSurfaceBatch.h#L140) — `bool Record(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings, const SurfaceView& view,`
- [Engine/RenderEngine/MaterialGraphSurfaceBatch.h:144](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSurfaceBatch.h#L144) — `bool RecordGpu(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,`
- [Engine/RenderEngine/MaterialGraphSurfaceBatch.h:154](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSurfaceBatch.h#L154) — `bool RecordInputs(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,`
- [Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h#L36) — `class EnhancedIBLGenerator : private IRHIUploadTransactionListener`
- [Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h:69](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h#L69) — `EnhancedIBLGenerator() = default;`
- [Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h:70](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.h#L70) — `EnhancedIBLGenerator(const EnhancedIBLGenerator&) = delete;`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 1. 완료한 범위

`MaterialGraphIblBake.h/.cpp`는 엔진 중립 RHI를 사용하는 GPU compute bake다.
`PrincipledIblBake.slang`이 Core/Layered 반사 계수와 방향별 환경 응답을 만들고,
`PrincipledIblLookup.slang`의 소비 함수는 적분 루프 없이 그 결과를 읽는다.
기존 Standard glTF의 2채널 DFG와 `EnhancedIBLGenerator`는 변경하지 않았다.

이 결과는 **평가된 지점 묶음**이다. 모든 재질을 대체하는 범용 2D LUT가 아니며,
Scene host에 설치된 lookup/renderer capability를 뜻하지 않는다.

## 2. 입력과 결과

입력 한 지점은 11개 float4, 176바이트다. 다음 값을 모두 포함한다.

- 평가된 Base Color/Alpha, Normal/Roughness, Metallic/IOR/Specular Level/AO.
- Specular Tint, Anisotropy, Tangent/Rotation, Thin Film Thickness/IOR.
- Coat Weight/Roughness/IOR/Tint/Normal, Sheen Weight/Tint/Roughness.
- Emission Color/Strength, world-space view, Core/Layered tier.

호출자는 graph/texture를 먼저 평가해 공간적으로 고정된 입력을 공급한다.
float 값은 finite이고 절댓값이 1e6 이하여야 한다. view는 0이 아니며
평가된 base normal과 `NdotV >= 1e-4`이어야 한다. 다른 0 방향은 공용
`EvaluateMaterial`의 fallback 규칙을 따른다. Core 입력에 Layered 항을 넣으면 거부한다.
한 recording의 묶음은 1~4,096개 지점이다. Shader ABI의 tier 값은 Core=0, Layered=1이다.

환경은 선형 HDR `RGBA16Float/RGBA32Float` 정사각형 6면 cube와 명시적인
0이 아닌 환경 generation, owner다. 환경 mip 0을 point sampler로 읽는다.

결과 한 지점은 9개 float4, 144바이트다.

| 결과 | 의미 |
|---|---|
| base/coat single RGB + directional albedo, average Fresnel | Core 물리 Fresnel/F82 또는 Layered correlated Smith 적분 |
| base irradiance | base normal의 cosine 평균, E/PI |
| base/coat prefiltered | 실제 시선과 각 normal/tangent의 RGB Fresnel 가중 방향 convolution |
| coat irradiance | coat normal의 별도 cosine 평균, E/PI |
| sheen irradiance | view/normal/roughness의 inverse LTC cosine 표본 평균 |

반사 응답은 `sum(L * F * visibility) / sum(F * visibility)`로 채널마다
정규화한다. 단일 Schlick scale/bias 또는 isotropic roughness mip로 대체하지 않는다.
roughness=0은 거울 방향을 직접 읽는다. 평균 Fresnel은 64개 cosine 표본,
각 반사/조도/sheen convolution은 1,024개 결정적 중요도 표본을 사용한다.
Layered coat 다중산란은 coat irradiance를 소비한다. 기존 5인자 소비 함수는
균일 환경의 이전 규약을 유지하고, 새 소비자는 별도 조도를 받는 6인자 함수를 쓴다.

아주 작은 Fresnel에서 정규화된 RGB 비율은 float cancellation에 민감하다.
해당 비율은 유한한 환경 radiance 범위 안인지 검사하고, CPU 대조는 실제
`singleScatter * prefiltered` 반사 에너지와 최종 픽셀을 함께 판정한다.
이를 제거하거나 임의의 기본 반사량으로 바꾸지 않는다.

## 3. 기록·교체·수명

`IblBaker::Initialize`는 이미 컴파일/쿠킹된 CS bytecode와 엔진의 root/PSO cache를 받는다.
baker 내부에서 Slang compiler를 만들지 않는다. 초기화 실패는 이전 pipeline을 유지한다.

`Record`는 열린 recording의 immediate encoder를 사용한다. RenderGraph pass callback용
함수가 아니다. 외부 host가 환경을 `ShaderResource`로 전이해 두고 GPU 환경 등록/owner를
보존한다. 새 출력 buffer는 Common→UAV→ShaderResource 순서로 기록된다.
입력·출력·upload·descriptor를 모두 준비한 뒤 dispatch하며 실패는 이전 result를 유지한다.

반환은 **기록 성공**이다. 제출/완료 성공을 의미하지 않는다. host는 환경 cache와
새/이전 result를 각 마지막 제출 완료까지 보존한다. CPU Texture owner만으로 GPU cache
eviction을 막을 수 없으므로 GPU 환경 owner/cache의 보존·사용 갱신도 필요하다.
buffer 해제는 중립 `IRenderDeviceServices::ReleaseBuffer`를 거친다. 기존 DX12/Vulkan
해제를 같은 인터페이스로 노출했으며 fence 대기는 여전히 owning host의 책임이다.

`IblBakeResult::Matches`는 장치, 환경 handle/format/크기/mip/generation/owner와
입력 전체 바이트를 비교한다. 축약 hash 충돌을 허용하지 않는다. texture evaluation,
numeric override, normal/tangent/azimuth/view 또는 환경 세대가 달라지면 새 요청이 필요하다.
result는 device shutdown 전에 파괴한다.

## 4. 검증과 남은 제품 작업

재현 명령:

```powershell
& Tools/regression/verify-material-ibl-bake.ps1
& Tools/regression/verify-principled-layered.ps1
```

`MaterialIblBakeProbe`는 현재 RenderEngine/Utility_Framework에 링크한다. 실제 DX12
device/root/PSO/texture cache, upload, compute dispatch, SRV 소비 graphics draw와 readback을
사용한다. 152개 Core/Layered 지점, 균일/면별 HDR 환경, 3프레임의 변경 후 다시 굽기를
독립 double CPU 수식 및 pinned LTC 계수와 대조한다. DXIL/SPIR-V CS/VS/PS를 모두 컴파일한다.
invalid input/environment/view/tier/budget/initialization, IOR/tangent/view/environment 세대
변경 검출과 실패 result 보존, 완료 뒤 native buffer 등록 해제를 검사한다.

Debug/Release 각각 **19,351개 검사·18,240개 GPU 성분**이 통과했다. 각 실행의
CS/VS/PS DXIL/SPIR-V 컴파일은 6개이며 GPU validation WARNING 이상은 0건이다.
최대 정규화 오차는 0.00000950694다. 비교 상한은 0.0001이며 weak reflection 에너지는
0.000001 상한을 별도로 적용한다. 기존 Layered 216,776개 검사와 energy bound 5,712개,
numeric golden 1,960행/7,840성분, 컴파일 14개·지정 거부 8개도 재실행해 통과했다.
로그는 `Build/Obj/MaterialProductProbe/ibl-bake-{build-,}{Debug,Release}.log`,
`ibl-bake-gate-final.log`, `ibl-layered-regression.log`에 남긴다.

남은 MAT-7 작업:

[MaterialGraphScenePacket.md](MaterialGraphScenePacket.md)의 render owner가 IBL 결과를
graph instance·coverage/queue·PSO·binding과 함께 소유하며, 실제 두 in-flight 제출의 교체와
완료 해제·abort를 검증한다. 아래 Scene pass/lookup 설치와 환경/성능 검증은 여전히 남는다.

[MaterialGraphSurfaceBatch.md](MaterialGraphSurfaceBatch.md)의 GPU graph 평가 배치와
`RecordGpu`를 추가했다. UV/LOD·world frame·eye·typed instance에서 만든 GPU point buffer를
CPU wait/readback 없이 bake가 직접 읽는다. invalid point는 -1 진단 표식으로 적분을
건너뛴다. Scene 게시 전 완료 readback 검증과 실제 mesh/skin sampling·보간/예산·RenderGraph
설치는 여전히 host 후속이다. 본 문서의 CPU evaluated-point 경로도 유지한다.

1. Scene host에 평가 입력·공간/시선 lookup 배치·보간/재사용 정책을 연결한다.
   texture-varying lobe와 매 view 변경을 재질별 단일 LUT로 축약하지 않는다.
2. 작은 HDR 광원까지의 환경 중요도/MIS 및 표본 수 수렴, lookup 해상도/메모리/시간 예산을
   실제 Scene에서 검증한다. 이 제한된 환경 fixture는 임의 HDR의 수렴이나 실시간 성능 증거가 아니다.
3. `SceneMaterialSlot`의 함께 게시·두 in-flight 교체/실패/abort/회수는 엔진 RHI host에서
   검사했다. 이를 제품 RenderGraph usage/draw/pass에 설치하고 actual Scene 교체와
   환경 eviction/device 재생성을 검증한다. 설치 전 capability는 계속 꺼져 있다.
4. 자동 host/compiler 쿠킹과 graph 의존성 폐포, SSS/refraction/Volume 자원 연결을 마친다.

actual GPU runtime은 D3D12다. SPIR-V 컴파일은 Vulkan runtime 검증을 뜻하지 않는다.
Blender rendered parity와 Standard 성능 수용은 MAT-9, Editor canvas는 LX-3이다.

동일 RenderGraph의 준비/선언/순차·병렬 기록과 GPU owner retirement는 [MaterialGraphPassRecording.md](MaterialGraphPassRecording.md)에 구현·검증했다. 위 실제 Scene 설치·환경 수렴·실시간 예산 항목은 계속 남는다.

</details>
