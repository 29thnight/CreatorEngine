# Material Graph 공간·시선 평가 배치

MAT-7의 Scene lookup 입력 단계. `MaterialGraphSurfaceBatch`는 실제 엔진 RHI에서
텍스처와 typed instance를 평가해 `IblBakePoint` GPU buffer를 만든다.
`IblBaker::RecordGpu`가 그 buffer를 직접 읽어 기존 물리 적분을 기록한다.
평가와 bake 사이에 CPU readback이나 GPU wait를 넣지 않는다.

## 1. 입력과 shader host

`SurfacePoint`는 UV·명시적 LOD, 월드 위치, 월드 normal/tangent/bitangent를 담는 80B다.
host가 object/skin 변환을 끝낸 좌표를 제공한다. `SurfaceView`는 eye와 0이 아닌
scene epoch/view revision/geometry revision을 가진다. eye-position으로 각 지점의
시선을 계산하므로 동일 재질의 모든 지점에 하나의 시선을 대입하지 않는다.

LOD는 0~32의 유한 값이다. compute shader가 pixel derivative를 만들었다고 가정하지
않는다. host가 평가 지점과 LOD 정책을 정해야 한다. 이 단계는 보간이나 mesh tessellation,
screen-space sampling을 구현하지 않는다. 제품 정점 형식과 skin/world 입력 생성은
[MaterialGraphMeshSurface.md](MaterialGraphMeshSurface.md)에 별도 구현했다.
배치 상한은 기존 bake와 같은 4096개다.

`BuildSurfaceSource`/`VerifySurfaceProduct`는 생성 graph 뒤에 정확한
`MaterialGraphSurfaceHost.slang`을 붙여 CS와 bounded reference VS/PS를 검증한다.
material b2/t16+/s3+ reflection이 CS/PS·DXIL/SPIR-V에서 같아야 한다.
host와 compiler/include dependency는 기존 semantic identity에 포함된다.
기존 LXMC 2의 surface VS/PS 필수 조건을 유지하면서 CS도 함께 저장한다.
runtime evaluator는 cooked CS를 소비하며 Slang compiler를 호출하지 않는다.

reference PS는 한 pixel에 한 지점을 대응시키고 graph를 같은 입력으로 평가해
bake lookup을 소비한다. 실제 Scene mesh/instance/light shader로 인증하는 host가 아니다.

## 2. 평가·유효성·게시 경계

`SurfaceEvaluator::Initialize`는 candidate PSO/layout을 성공한 뒤 교체한다.
`Record`는 instance semantic key와 layout, 현재 recording/descriptor, 입력과 budget을
검사한다. 모든 allocation과 binding 검사를 dispatch 전에 끝낸다. 실패는 caller가 가진
이전 batch를 보존한다. texture가 `ShaderResource`인 상태를 준비하는 것은 host 책임이다.
동일 graph의 준비/선언/기록은 [MaterialGraphPassRecording.md](MaterialGraphPassRecording.md)에 구현했다. immediate wrapper는 split RenderGraph callback에서 호출하면 안 된다.

GPU는 실제 graph texture/SRGB view, sampler, numeric override, normal/tangent와 view를
평가한다. authored IOR/Specular Level과 회전 전 tangent를 저장하므로 bake에서 IOR 보정과
anisotropy rotation을 두 번 적용하지 않는다. base/coat normal을 각각 보존한다.

유한 범위·front-facing NdotV·Core/Layered 입력 검사를 GPU에도 둔다. 거부한 지점은
`viewTier.w = -1`이다. bake는 그 지점의 적분을 건너뛰고 0 radiance와
`baseAverage.w = -1`을 기록한다. consumer가 이 표식을 유효한 재질로 소비하지 않는다.

`Record`와 `RecordGpu` 성공은 명령 기록 성공이다. Scene에 게시하기 전에 host는
**해당 batch의 완료된** point readback을 `ValidateReadback`으로 검사해야 한다.
실패 진단은 지점 index를 포함한다. accepted batch 선택과 마지막 정상 packet 보존은
host의 책임이다. 다른 recording에서 읽는 batch는 완료 readback 검증을 통과해야 한다.
취소/미제출 batch를 다음 recording에서 그대로 bake하지 않는다.

현재 검증 host는 GPU wait/readback으로 이 acceptance 경계를 확인한다. 제품 Scene의
비동기 진단·게시 정책은 아직 연결하지 않았다. 이 검증 비용을 실시간 Scene 비용으로
간주하지 않으며 Core/Layered Scene capability도 켜지 않는다.

## 3. 재사용·소유권

`SurfaceBatch::Matches`는 immutable instance pointer, eye/revision과 모든 spatial bytes를
정확히 비교한다. `IblBakeResult::MatchesGpu`는 immutable batch identity와 환경의
handle/metadata/generation/owner를 비교한다. 재질·텍스처 override·시점·geometry·환경 변경을
한 material-wide LUT의 재사용으로 덮지 않는다. caller는 instance/geometry를 불변으로
유지하고 변화한 데이터의 revision을 제공해야 한다.

batch는 material binding/instance owner를, bake result는 batch와 environment owner를
소유한다. 반환 buffer는 `ShaderResource`다. 모든 owner를 마지막 GPU 제출 완료까지
보존하고 device shutdown 전에 해제한다. 재사용해도 host는 texture cache/residency를
갱신해야 한다. CPU Texture owner만으로 GPU 환경 eviction을 막는다고 가정하지 않는다.
이 배치 객체 자체는 recording 자동 보관/제출 게시 slot이 아니다.

## 4. 검증

```powershell
& Tools/regression/verify-material-surface-batch.ps1
```

실제 DX12 device/root/PSO/texture cache로 평가 → GPU buffer bake → graphics consumer를
기록한다. 37개 지점으로 32-thread dispatch의 끝 경계도 검사한다.
SRGB 색·선형 alpha→roughness·명시적 mip, 위치·기하 normal/tangent·eye 변경,
IOR/Specular Level override, coat/sheen/aniso/rotation/thin-film을 CPU double 기준과 비교한다.
균일 HDR 환경과 방향별 HDR 환경, 정확한 재사용/변경 무효화, PSO·stale binding·잘못된
좌표/LOD/budget 실패 보존, back-facing/범위 초과 GPU 진단과 완료 뒤 buffer 해제를 검사한다.

Debug/Release 각각 **19,387개 검사·GPU 19,092성분·37지점·8개 제출 프레임**을 통과했다.
추가 두 recording의 abort와 미제출 batch 재사용 거부도 검사했다.
CS/VS/PS의 DXIL/SPIR-V와 공용 bake CS **14개**를 컴파일했고 D3D12 GPU validation
WARNING 이상은 0건이다. RenderEngine/Utility_Framework Debug/Release 링크 결과다.
전체 Editor 빌드·GUI 또는 Vulkan runtime 결과로 세지 않는다.

물리 계산의 최대 정규화 오차는 **0.0000138251**이다. SRGB source 비교는 기존
texture-binding gate와 같은 절대 오차 0.001 기준이며 최대 **0.000689566**이었다.
SRGB 입력만 이 별도 기준으로 확인한 뒤 실제 decode 값을 CPU double 적분 입력에 쓴다.
나머지 point ABI·물리 bake·consumer 비교는 정규화 오차 0.0001을 유지한다.
색 decode 허용 오차로 transport 검증 기준을 넓히지 않는다.

원본 로그는 `Build/Obj/MaterialProductProbe/surface-batch-{build-,}{Debug,Release}.log`와
`surface-batch-gate-final.log`다. 기존 `TypeTrait.h` C4189 및 Debug LNK4075 외
새 빌드 경고는 없었다. 새 경로의 formatter/project XML/검증 script 구문 검사도 통과했다.

공용 bake shader와 CPU reference 추출의 회귀로 기존 IBL gate의 Debug/Release 각
**19,351개 검사·GPU 18,240성분**과 Scene packet gate의 각 **165개 검사·GPU 16성분·
실제 두 in-flight 제출**도 통과했다. 원본 로그는 같은 폴더의
`ibl-bake-surface-regression-{build-,}{Debug,Release}.log` 및
`scene-packet-surface-regression-{build-,}{Debug,Release}.log`다.

## 5. Scene 설치에 남은 작업

- 검증된 mesh/skin 입력·triangle 분할/샘플 생성을 실제 Scene draw packet과 per-view owner에 연결.
- 가시 픽셀 수집의 독립 RHI RenderGraph 패스는 `MaterialGraphRasterSurface.md`에 구현했다.
  실제 Scene 호출과 texture별 LOD 설치는 남는다. 명시적 footprint LOD 및 재질 평가 전
  frame 보간의 범위는 `MaterialGraphMeshSurface.md`가 소유한다.
- 물리 lookup의 해상도·배치·보간/재사용 정책과 오차·GPU 시간·메모리 예산 판정.
- RenderGraph usage/recording 전환과 opaque Forward depth·광원·투명 합성 순서 연결.
- GPU packet의 완료 검증 후 게시 API는 메시 입력 게이트에서 연결했다. 실제 Scene의
  비동기 polling·submission ticket 배선과 환경 수명 검증은 아직 남는다.
- 환경 MIS/수렴, Special SSS/refraction/Volume 합성과 자동 host/compiler cook closure.

MAT-7 전체와 Editor LX 편집은 아직 완료되지 않았다.
