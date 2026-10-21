# REAC 2025 비교에 따른 렌더링 파이프라인 후속 구현

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/MaterialGraphProduct.h:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphProduct.h#L34) — `enum class Tier : std::uint8_t`
- [Engine/RenderEngine/MaterialGraphProduct.h:41](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphProduct.h#L41) — `enum class Route : std::uint8_t`
- [Engine/RenderEngine/MaterialGraphProduct.h:111](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphProduct.h#L111) — `enum class TextureSampleEncoding : std::uint32_t`
- [Engine/RenderEngine/MaterialGraphPrimitiveCache.h:32](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphPrimitiveCache.h#L32) — `const std::array parameters{RHILayout::Cbv(0),`
- [Engine/RenderEngine/MaterialGraphPrimitiveCache.h:126](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphPrimitiveCache.h#L126) — `class ScenePrimitiveCacheFrame final`
- [Engine/RenderEngine/MaterialGraphPrimitiveCache.h:129](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphPrimitiveCache.h#L129) — `ScenePrimitiveCacheFrame() = default;`
- [Engine/RenderEngine/DataSystem.cpp:55](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L55) — `for (const auto& entry : file::directory_iterator(directory))`
- [Engine/RenderEngine/DataSystem.cpp:129](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L129) — `throw std::runtime_error("Asset image decoder apartment initialization failed.");`
- [Engine/RenderEngine/DataSystem.cpp:139](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L139) — `std::ranges::transform(value, value.begin(), [](unsigned char character)`
- [Tools/regression/verify-reac-shaders.py:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-reac-shaders.py#L18) — `import struct`
- [Tools/regression/verify-reac-shaders.py:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-reac-shaders.py#L23) — `SHADERS = Path("Dynamic_CPP/Assets/Shaders/DefaultPassShader")`
- [Tools/regression/verify-reac-shaders.py:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-reac-shaders.py#L36) — `class ShaderCase:`
- [Tools/regression/verify-primitive-cache.py:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-primitive-cache.py#L19) — `ROOT = Path(__file__).resolve().parents[2]`
- [Tools/regression/verify-primitive-cache.py:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-primitive-cache.py#L20) — `HOST = Path('Engine/RenderEngine/MaterialGraphSceneHost.cpp')`
- [Tools/regression/verify-primitive-cache.py:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-primitive-cache.py#L21) — `CACHE = Path('Engine/RenderEngine/MaterialGraphPrimitiveCache.h')`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L46) — `bytes += std::uint64_t(width) * height * sizeof(float);`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L53) — `const auto found = std::ranges::find_if(`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:495](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L495) — `std::array<RHIBindingDesc, GpuGeometryOcclusion::kMaximumLevels> GpuGeometryOcclusion::Pyramid::BindingInputs(`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:3533](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L3533) — `if (budget.m_primitiveCache && ScenePrimitiveCacheFrame::SupportsPacketDispatch(*work))`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:3874](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L3874) — `std::array<uint32_t, 3> SceneHost::ShadowBucketSubmissionCounts() const`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:3901](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L3901) — `std::array<uint32_t, 2> SceneHost::LayerBucketCounts() const`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:94](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L94) — `std::bit_cast<std::array<std::uint32_t, 16>>(input.World()),`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:95](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L95) — `std::bit_cast<std::array<std::uint32_t, 4>>(input.View().eye)};`
- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp:1163](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphMeshSurface.cpp#L1163) — `const std::array indices{source.indices_[std::size_t(triangle) * 3],`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

작성일: 2026-10-19. 기준 master: [`0d3c2cac6d3bd16a2c4511ccb067749feafcd33c`](https://github.com/29thnight/CreatorEngine/commit/0d3c2cac6d3bd16a2c4511ccb067749feafcd33c).

구현 코드: [`deeaff2785fa059fe13e632ff934ce292ccf2801`](https://github.com/29thnight/CreatorEngine/commit/deeaff2785fa059fe13e632ff934ce292ccf2801). 검증한 로컬 코드와 게시한 GitHub 코드의 tree는 `6565bd9bf82e768bb86c9252d52895415d37f90e`로 동일하다.

[전체 비교 보고서](CreatorEngine_REAC2025_Rendering_Comparison_20261019.html)와 [검증 근거](reac-20261019/verification.json)를 함께 읽는다. 기존 보고서의 18개 비교 항목을 유지하면서 기준선을 최신 master로 갱신하고, 변경 전 상태와 이 PR의 구현 상태를 구분했다.

## 1. 해결하는 문제와 구현 범위

기준 코드에서는 메시릿마다 정점을 복원한 뒤 경계를 계산한다. 인스턴스 전체가 제거될 수 있어도 그 메시릿 작업이 GPU ring에 게시되고, 같은 LOD 그룹에 대한 요구 비트도 여러 메시릿이 반복 갱신한다. 또한 SurfaceResolve와 Temporal은 화면에 남은 같은 삼각형의 변환을 반복한다. 그림자의 첫 컬링은 light-space history를 사용할 수 있지만 현재 카메라 깊이를 받는 입력 계약은 없다.

이 PR은 다음 세 부분을 구현한다.

| 작업 | 변경 후 동작 | 기본 상태 | 직접 줄이려는 비용 |
| --- | --- | --- | --- |
| 1A. 인스턴스 조기 판정 | 기존 Reset에서 현재 포즈의 보수적 경계를 계산하고, Cull이 하위 메시릿을 ring에 게시하기 전에 거절한다. | direct/object-space의 유효 경계에 적용. 경계가 없으면 기존 메시릿 판정으로 진행. | 제거되는 인스턴스의 ring job, 메시릿 decode/skinning, 반복 그룹 feedback atomic |
| 1B. 가시 프리미티브 캐시 | GPU material packet에서 보이는 삼각형의 현재 표면·clip 및 이전 object-space 위치를 계산하고 SurfaceResolve/Temporal이 재사용한다. | 명시적 opt-in, 기본 꺼짐. | 같은 가시 삼각형의 반복 변환 |
| 3. 그림자 예측 입력 | 첫 shadow 컬링이 현재 camera HZB를 예측 입력으로 읽는다. 최종 복구는 기존 현재 light-space HZB를 사용한다. | 명시적 opt-in, 기본 꺼짐. | 첫 shadow 목록의 불필요한 래스터 작업 |

REAC의 독립 80-byte instance 목록, 해상도 비례 압축 vertex cache, 64×64 light-space 깊이 재투영, 이전 ray-hit 주입을 그대로 재현한 구현은 아니다. 기존 엔진의 입력·복구·수명 계약에 맞춰 구현한 범위를 아래에 명시한다. CPU의 `RenderGraphDeclare`와 `MaterialGraphScenePrepare`를 직접 단축하는 별도 PR의 성능 결과로 합산하지 않는다.

원문 비교 위치: [Capcom REAC 2025 PDF](https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf)의 PDF 페이지 20–24, 43–48, 53–66. 발표의 69페이지에 적힌 향후 과제는 Capcom의 구현 완료 항목으로 세지 않는다.

## 2. 저장된 계측 결과를 기준선으로 삼은 방법

원본은 [`20261019-cpu-gpu-data/manifest.json`](https://github.com/29thnight/CreatorEngine/blob/0d3c2cac6d3bd16a2c4511ccb067749feafcd33c/docs/analysis/measurements/20261019-cpu-gpu-data/manifest.json)이다. `after-baseline`은 이전 CPU 작업 이후의 캡처 이름이며 **이번 PR의 변경 후 측정값이 아니다**.

측정 코드 신원은 `946ef4fa9b79f4e74ad09a03d79aa117277ded87`이며 공개 provenance의 `DataSystem.cpp` include 보정이 적용돼 있다. 이 커밋부터 현재 master `0d3c2cac`까지의 398개 변경 파일은 모두 docs 아래에 있다. 주요 생산 코드의 Git blob과 LF/CRLF 정규화 SHA-256을 대조했고, 같은 include 보정을 이번 코드에 포함했다. 측정 바이너리의 해시는 공개됐지만 실제 DLL/PDB 전체가 저장돼 있다는 뜻은 아니다.

manifest의 360개 데이터 파일은 Git tree의 크기·blob 신원과 일치한다. 내려받아 재검증한 182개 파일은 142,989,631 byte이며 크기, SHA-256, Git SHA-1을 확인했다. 이 범위에 103개 timeline 파일, 네 캡처 metadata와 `after-baseline` 원시 이벤트가 포함된다. CPU scope를 동일 Game view 표본으로 재집계했고, 원시 GPU 이벤트와 공개된 GPU 이벤트의 중복·일치도도 확인했다. 세부 기준과 한계는 검증 JSON에 보존한다.

### 동일한 444개 Game 표본

| 지표 | 평균 ms | p50 ms | p95 ms |
| --- | ---: | ---: | ---: |
| Game CPU scope | 11.742154 | 11.687150 | 14.451700 |
| MaterialGraphScenePrepare | 2.156798 | 2.140400 | 2.714100 |
| RenderGraphDeclare | 4.219918 | 3.973700 | 5.703700 |
| RenderLoop CPU scope | 26.594877 | 25.883150 | 31.215700 |
| 계측된 Game GPU pass 구간의 합집합 | 5.194939 | 5.162550 | 5.768600 |

CPU 준비와 선언의 평균 합은 6.376716 ms, Game CPU scope 평균의 54.31%다. GPU 값은 유효 Game submission의 계측 pass 합집합이며 전체 GPU frame time, GPU 이용률, FPS가 아니다. CPU와 GPU의 값도 서로 더하지 않는다.

CPU 값은 QPC로 측정한 scope의 wall time이다. RenderLoop는 해당 Game view를 포함하는 전체 루프이며, Game과 그 내부 Prepare·Declare를 포함하므로 이 행들을 서로 더하지 않는다. 444개 Game 표본과 444개 enclosing loop의 대응도 확인했다.

주요 GPU 컬링 세 구간은 `Shared.ShadowRecovery.Cull` 0.769335 ms, `Shared.First.Cull` 0.734084 ms, `CameraMeshlet.Recovery.Cull` 0.472195 ms로 평균 합 1.975614 ms다. 이는 계측 GPU pass 합집합의 약 38.03%다. 반면 Material Resolve는 평균 0.186160 ms, Resolve와 Temporal 합은 0.266720 ms로 약 5.13%다. 따라서 항상 작동하는 새 대규모 캐시보다 컬링 작업량 감소를 먼저 구현하고, 캐시는 비용을 비교할 수 있는 선택 경로로 둔다.

캡처는 Debug, 8 workers, Scene/Game 1186×514, Editor 전경, RenderThread Normal, binding detail 0이다. CPU/GPU 모델, OS/driver 및 동작 클록이 충분히 공개되지 않아 플랫폼을 넘는 성능 결론이나 Release 수용에 사용할 수 없다. ETW 표본의 running/waiting/ready는 별도 캡처이므로 위 baseline scope와 섞지 않는다. 큰 `RenderGraphDeclare` 병렬 분기의 marker 89/102/103/104는 조사한 원시 캡처에 0건이다. 이 자료만으로 해당 분기의 실제 가동률이나 최적 worker 수를 판정하지 않는다.

## 3. 인스턴스 조기 판정의 코드 계약

### 불변 경계와 현재 포즈

`MeshSurfaceInput::SealInstanceBounds()`는 정적 partition을 봉인할 때 cooked meshlet sphere들의 합집합을 만든다. QMC2 위치 오차와 구면 검증의 여유를 포함하고, double 누적 후 float 변환은 바깥쪽으로 반올림한다. 이 값은 불변 입력 소유자에게 저장되므로 warm frame에서 메시릿 descriptor를 다시 순회하지 않는다. 스트리밍의 전체 입력과 각 chunk도 같은 봉인 과정을 사용한다.

GPU Reset은 현재 bone palette의 각 본이 그 경계를 변환한 AABB를 합친다. 정규화된 비음수 skin 가중치의 변형 위치는 이 합집합 안에 들어간다. 모든 가중치가 0인 기존 정점 동작을 위해 bind pose 경계도 포함한다. 현재 world 변환까지 적용한 뒤 frustum 판정을 저장한다. shear·reflection을 포함한 affine 변환은 interval 계산과 바깥쪽 반올림으로 보수적으로 처리한다. 수치가 유효하지 않거나 overflow이면 기존 메시릿 경로로 통과시킨다. bind-pose 경계만으로 움직이는 skinned geometry를 제거하지 않는다.

CPU가 coarse 경계를 제공하는 대상은 `UsesDirectClusters() && IsObjectSpace()`다. compatibility와 경계가 없는 입력은 통과한다. 이는 스키닝의 coarse LOD를 새로 생성한다는 뜻이 아니다. Reset의 현재 pose 읽기는 RenderGraph의 명시적 read dependency에 추가됐다.

### 기존 GPU 작업 배분에 삽입

별도 coarse dispatch를 추가하지 않는다. 기존 Reset의 bin당 64개 lane에서 경계를 만들고, persistent Cull이 비어 있지 않은 source partition을 처음 얻을 때 coarse 결과를 검사한다. 거절된 partition은 메시릿 descriptor를 decode하거나 ring job을 게시하기 전에 종료된다. producer lease는 기존 종료 경로에서 반납한다.

frustum 계산은 bin당 한 번이다. HZB 검사는 기존 16개 논리 partition 중 비어 있지 않은 partition마다 최대 한 번 실행한다. 인스턴스 survivor 목록 자체를 먼저 compaction한 완전한 별도 front stage는 아니며, 거절된 bin도 초기 partition claim 비용은 남는다. Reset의 bone 경계 합집합과 1,792 byte의 고정 groupshared scratch 비용도 추가된다. 이 범위를 과장해 모든 Cull 비용이 사라진다고 해석하지 않는다.

첫 단계의 메시릿 survivor가 하나라도 있으면 recovery의 coarse HZB 거절은 건너뛴다. 개별 메시릿에서도 기존 first-stage classification을 유지한다. 새 경계로 이미 살아남은 기하가 최종 목록에서 사라지는 것을 막기 위한 계약이다.

### ABI와 feedback

| 자원 | 변경 | 수명 / 한계 |
| --- | --- | --- |
| 기존 world upload | bin당 float4 3개 → 5개, +32 byte | 기존 batch upload 하나에 포함. 마지막 두 행은 불변 local min/max. |
| 기존 출력 UAV | bin당 8 words, +32 byte | posed world min/max, valid, frustum admission. 기존 scan 앞에 배치. 정렬 padding 별도. |
| candidate flags | `kInstanceBounds = 512` | 새 CPU producer는 모든 bin에 같은 5행 stride를 게시. 경계가 없으면 valid 0. |
| indirect arguments | 기존 5 words 유지 | count, empty-list, firstInstance 0 계약 유지. |
| ring header word 15 | 거절된 source partition 수 | 기존 크기의 진단 word 사용. 새 프레임 readback을 추가하지 않음. |

같은 LOD 그룹의 desired feedback은 Reset에서 한 번만 갱신한다. coarse에서 거절된 오브젝트도 기존 desired-cut 스트리밍 요청이 유지된다. first/recovery와 공유 group owner의 bitmask 소유권을 바꾸지 않는다. `MeteorGeometryPosition()`은 위치만 필요한 소비자의 계약을 명시하고 full `MeteorGeometryPoint()`와 실제 decode/skinning 결과를 비교했다. 컴파일러가 기존 미사용 속성 계산을 이미 제거할 수 있으므로 이 helper 자체의 속도 향상을 주장하지 않는다.

## 4. 가시 프리미티브 캐시

### 자원과 생산자

`ScenePrimitiveCacheFrame`은 현재 frame의 scene records, material packets, descriptor recording과 graph epoch를 봉인한다. `ClearPrimitiveCache`는 key만 초기화한다. `PopulatePrimitiveCache`는 기존 8×8 record/tile 패킷과 각 geometry page를 사용해 간접 dispatch하고, 실제 visibility에 나타난 primitive address를 bounded hash에 게시한다. CPU가 가시 primitive 목록을 새로 만들지 않는다.

전역 visibility address가 key이므로 서로 다른 record의 동일 primitive index가 섞이지 않는다. 고정 16,384개 slot과 최대 8번의 linear probe를 사용한다. 네 개 인접 pixel의 중복 CAS는 64-lane 고정 groupshared scratch로 줄인다. subgroup 크기나 vendor 전용 WaveMatch 확장을 가정하지 않는다. producer의 packet/record는 작업 그룹 전체에서 동일하므로 geometry descriptor index도 동일하며 불필요한 NonUniform 표시를 하지 않는다. 소비자의 서로 다른 record 접근은 기존 NonUniform 계약을 유지한다. hash에 들어가지 못한 primitive는 소비자의 기존 decode로 처리한다. cache miss가 렌더링 누락으로 변환되지 않는다.

key를 먼저 CAS하는 생산자와 pixel 소비자는 같은 dispatch 안에서 동작하지 않는다. Clear → Populate의 UAV 경계와 Populate → Surface/Temporal의 명시적 SRV dependency가 data 게시를 완성한다. Populate의 다른 producer는 선점된 key의 data를 읽지 않는다.

### 정밀도와 소비자

한 primitive에 현재 월드 표면의 세 정점, 현재 raster clip, 이전 object-space 위치를 full float로 저장한다. 기존 homogeneous barycentric 복원, near-plane 처리, quad helper 미분값 계산과 Temporal의 이전 위치 보간 후 world/projection 적용 순서를 유지한다. REAC의 vertex당 16-byte 포맷과 동일하지 않다. 임의의 half/packed 정밀도를 도입해 영상·motion 오차를 늘리지 않는 초기 구현이며, 저장 형식 축소는 native 화질·대역폭 비교 이후의 별도 작업이다.

기본 `LXSceneGBufferPS`와 `LXSceneTemporalPS`는 cache 사용 여부를 compile-time false로 전달한다. opt-in은 별도 cooked `LXSceneCachedGBufferPS`와 `LXSceneCachedTemporalPS`를 선택한다. 상수 material fixture의 DXIL을 기준 master와 대조해 기본 entry의 t174/t175 및 추가 cache resource/query가 제거됨을 확인했다. 전체 정규화 IR은 wrapper의 출력 합류 등으로 동일하지 않으며, 실제 시간 동등성은 미측정이다. cache Temporal은 lookup 결과를 복원과 이전 위치 경로에 함께 넘겨 같은 pixel의 중복 hash 탐색을 피한다. 캐시 PSO는 봉인된 generation과 기존 PSO id를 확인해 재사용하며, 프레임의 Draw에 준비된 handle을 게시한다.

### 비용과 호환성

| 항목 | 비용 / 정책 |
| --- | --- |
| Key storage | 16,384 × 4 = 65,536 byte |
| Triangle storage | 16,384 × 336 = 5,505,024 byte |
| 합계 | 활성 뷰의 retained frame당 5,570,560 byte = 5.3125 MiB. allocator 정렬·상위 풀 예약량 별도. |
| Constants | 80 byte, 기존 upload allocator 정렬 적용 |
| Descriptor | 기존 t170 table 4 → 6 SRV. table당 +2, material/legacy table을 모두 만드는 일반 뷰는 +4 descriptors. root DWORD 수는 유지. 기본 off도 이 비용은 존재. |
| CPU 저장소 | frame은 고정 2-buffer 배열, 동적 declaration scratch와 PSO map은 resources owner가 용량/항목을 재사용. |
| 기본 off | cache frame, cache GPU buffers, cache PSO를 만들지 않음. 두 cooked shader blob의 저장·로드·SceneShaderSet→Program 복사와 descriptor 증가는 별도 비용. |
| Packet 한계 | compute dispatch의 축별 65,535 한도를 넘는 packet plan이면 기존 복원 경로 유지. |
| 자산 신원 | `SceneHostIdentity` 40 → 41. scene material shader를 다시 쿠킹해야 함. 오래된 cooked blob을 runtime shader compile로 대체하지 않음. |

사용 설정은 `SceneHostBudget::m_primitiveCache` 또는 live renderer의 `CREATOR_METEOR_PRIMITIVE_CACHE=1`이다. 해상도 임계값 같은 미보정 자동 활성화 정책은 넣지 않았다. 캐시를 켠 경우 Clear/Populate, lookup, 임시 메모리와 Surface/Temporal의 합을 비교해야 한다. hit rate만으로 수용하지 않는다.

## 5. 현재 카메라 깊이를 사용하는 그림자 예측

설정은 `SceneHostBudget::m_shadowDepthPrediction` 또는 `CREATOR_METEOR_SHADOW_CAMERA_PREDICTION=1`이며 기본값은 꺼짐이다. 입력 표면이 없어 camera HZB를 만들 수 없는 뷰는 기존 경로를 유지한다. 고정 char 배열로 설정을 읽어 프레임별 동적 문자열/컨테이너를 만들지 않는다.

### 그래프 의존성과 신원

활성화할 때 camera first만 먼저 선언한다. Camera Occluder가 실제 current-depth HZB를 만든 뒤, 세 shadow first를 `METEOR.Shared.ShadowCameraPrediction` 묶음으로 선언한다. 기존 네 뷰 shared first를 그대로 유지한 채 거기에 camera HZB read를 붙이면 스스로 자기 생산자를 기다리는 cycle이 생기므로, 활성화된 경로에서만 묶음을 나눈다. 기본 경로의 기존 shared 묶음은 유지한다.

frustum과 LOD는 light projection/extent를 유지하고, 예측 HZB의 투영·크기만 camera ViewKey에서 받는다. 해당 키는 같은 device, upload recording, descriptor version, frame, scene epoch의 현재 카메라를 가리켜야 한다. ShadowFirst가 받은 camera depth는 실제 occluder depth의 동일 RG handle/version이어야 한다.

`Pyramid::RequireCurrentView()`는 ViewKey가 같더라도 retained history reader를 거부한다. 일반 `DeclareWithOcclusion`/`DeclareOcclusionBatch`에는 prediction frame을 넣을 수 없다. 같은 graph/epoch와 같은 생산자의 재선언만 허용하며, 다른 depth producer로 바꾸는 요청은 거부한다.

### 정확성 보존과 미구현 범위

현재 camera clip volume 안에 경계 전체가 들어온 경우만 예측 HZB로 거절한다. 부분적으로 밖에 있거나 near/far/W 또는 수치가 불확실하면 통과한다. 이는 offscreen caster를 camera visibility만 보고 제거하지 않기 위한 조건이다.

예측은 첫 shadow 목록에만 적용한다. 첫 survivor는 최종 복구에서 유지되고, 첫 거절 대상은 현재 실제 light-space occluder HZB로 다시 검사한다. static shadow cache와 최종 light-space depth/history에 camera depth를 복사하지 않는다. 카메라와 광원에서 반대 가시성인 경우까지 포함해 이 복구 계약을 검증한다.

이 구현은 **camera-space HZB를 직접 읽는 prediction-only 경로**다. REAC의 카메라 깊이를 64×64 light-space atlas에 재투영하는 생산자, 이전 ray-hit position의 주입, 그림자 software rasterizer는 추가하지 않았다. ray-hit 생산자나 history 수명 계약이 없는 상태에서 이름만 붙인 자원을 만들어 완료로 세지 않는다. 새 camera → shadow dependency가 병렬성을 줄일 가능성도 전체 GPU schedule에서 측정해야 한다.

## 6. 할당, 소유권과 컨벤션

새 엔진 멤버는 `m_` 접두사, 새 상수는 `kPascalCase`, 지역 변수는 camelCase를 사용한다. 새 코드의 제어문은 중괄호와 Allman 배치를 사용하고, C++ 변경 구간은 저장소 `.clang-format`에 맞춘다. Slang은 HLSL formatter가 entry/loop attribute를 잘못 결합하는 경우가 있어 기존의 attribute 별도 행과 함수 배치를 보존하고 수동 대조했다. 기존 코드의 광범위한 포맷 변경은 하지 않는다.

핫패스의 작업 목록을 지역 vector/map으로 새로 만드는 코드는 추가하지 않는다. 고정 정보는 `std::array`와 GPU groupshared 배열을 사용한다. Shared HZB descriptor 조립의 기존 지역 vector는 8×16 고정 배열로 교체했다. local instance bounds는 정적 입력 owner에 저장하며, 캐시의 declaration uses/normalization scratch는 resources owner가 재사용한다. GPU cache buffers는 기존 `TransientBufferPool`의 제출·완료 수명에 따라 회수한다. opt-in cold PSO 준비와 프레임 소유자 객체 자체는 할당이 존재하므로 전체 시스템의 무할당을 주장하지 않는다.

원자적 갱신 횟수, 유지 메모리와 추가 dependency도 비용이다. 빈 목록, overflow, stale recording, graph reset, 실패 후 소유권과 descriptor page 경계를 성능과 함께 확인한다.

## 7. 검증과 재현

정확한 실행 결과, source SHA-256, compiler/validator 신원과 baseline countercheck는 [verification.json](reac-20261019/verification.json) 및 같은 디렉터리의 보고서에 기록한다. 원문에서 추출한 생산 함수를 가짜로 재구현한 뒤 통과시키지 않도록, Slang include closure를 고정해 실제 compiler에 전달하고 실행 전후 source 변경을 확인한다.

### 현재 환경에서 수행할 수 있는 검사

- 실제 Slang의 bounds helper → C++를 독립 double corner/plane/skin oracle와 비교.
- 실제 position-only/full-surface decoder의 bitwise 위치 비교와 독립 packet/skin oracle 비교.
- 실제 single/shared/recovery HZB wrapper → C++에서 camera/light 반대 투영, 전체 footprint, odd extent, first-survivor 보존을 확인. coverage 또는 recovery를 의도적으로 제거한 변이는 실패해야 함.
- 실제 cache header/입장 조건/바인딩 코드의 portable fixture에서 할당·실패·retained ownership·stale epoch를 확인. 이 fixture의 device/encoder 경계는 대역이며 실제 RHI build가 아님.
- 실제 셰이더 변형의 DXIL 생성과 `dxv` 검증, SPIR-V 생성과 `spirv-val` 검증. 현재 RHI 옵션과 capability를 추가한 진단 모드의 결과를 따로 기록.
- `git diff --check`, runtime/editor include boundary, 변경 구간 포맷, native probe assertion의 정적 대조.

Slang 2026.14, DXC 1.9.2607, SPIRV-Tools 2026.4를 사용했다. 도구의 출처·checksum은 검증 근거에 보존한다. C++ oracle는 `-fno-fast-math -ffp-contract=off`를 사용한다. Slang의 C++ 출력이 HLSL `precise`를 그대로 남기는 경우 host keyword adapter를 적용하며 그 사실을 기록한다. ASan/UBSan과 LeakSanitizer는 구분한다. sandbox에서 `/proc` task 접근이 막혀 LeakSanitizer를 실행할 수 없는 경우 leak 검증을 통과한 것으로 세지 않는다.

### 실제 실행 결과

| 검사 | 결과 | 근거와 해석 |
| --- | --- | --- |
| 실제 RHI 옵션의 DXIL 컴파일 및 `dxv` | **34/34 통과** | [셰이더 집계](reac-20261019/shaders/summary.json). 전체 실행과 최종 material/producer 재실행을 source 신원으로 연결한 결과. |
| 실제 RHI 옵션의 SPIR-V 컴파일 및 `spirv-val` | **14/34 통과, 20개 실패** | [현재 옵션의 결과](reac-20261019/shaders/matrix-engine.json). 생산 Vulkan 수용은 미완료. |
| capability 12개를 명시한 진단 SPIR-V | **32/34 통과, 2개 실패** | [진단 결과](reac-20261019/shaders/matrix-diagnostic.json). 기본·캐시 GBuffer의 dynamic QuadBroadcast Index가 SPIR-V 1.3 검증에 실패. 기본 entry의 같은 오류는 master에서도 재현. |
| 최종 cache Clear/Populate producer | **DXIL 2/2, SPIR-V 2/2 통과** | 최종 bytes와 실제 RHI 옵션으로 별도 재실행. SPIR-V triangle stride도 **336 byte**로 확인. |
| 실제 Slang bounds / frustum CPU oracle | **1,069,124 checks 통과** | 72,000 normalized skin samples 및 실제 거절 13,821개 포함. [ASan/UBSan 결과](reac-20261019/shaders/cpu-oracles-sanitized.json). |
| 실제 Slang position-only/full decode CPU oracle | **12,336 checks 통과** | 514 fixtures, 2,056 vertices. 독립 decode 기준과 bitwise parity 확인. 같은 ASan/UBSan 근거 참조. |
| 실제 cache C++ 수명·그래프·admission fixture | **176 checks 통과** | [Normal](reac-20261019/primitive-cache-normal.json), [ASan/UBSan](reac-20261019/primitive-cache-asan.json). device/graph 서비스 경계는 대역. |
| 실제 Slang cache 변환·조회 CPU oracle | **27 fixtures / 56,377 checks 통과** | 위 cache 보고서의 `shader_cpu`. cache hit/miss·이전 위치·복원 계약 확인. |
| 실제 Slang shadow prediction/recovery CPU oracle | **33,425 checks / 15,271 helper calls 통과** | [ASan/UBSan](reac-20261019/shadow-cpu.json). coverage 및 recovery를 제거한 두 변이도 해당 경로에서 예상대로 실패. |
| 기본 cache-off entry의 DXIL 정적 비교 | cache resource/query/load 제거 확인 | [IR 비교](reac-20261019/shaders/default-ir-comparison.json). resource table은 동일하나 전체 IR은 다름. native 시간 비교가 아님. |
| Include boundary | 전체 실패, **추가 위반 0개** | [기준 코드 대조](reac-20261019/include-boundary.json). master와 같은 27개 상향 include 및 70개 물리 경계 부채. |

셰이더 집계는 한 번의 새로운 전체 재실행으로 표현하지 않는다. 원래 full matrix와 최종 material 9개·producer 2개 entry 재실행의 명령·종료 코드·binary SHA를 보존하고, [source snapshots](reac-20261019/shaders/source-snapshots.json)에 달라진 producer·Host include·runner와 변하지 않은 입력을 구분했다. 최종 기본 GBuffer/Temporal의 DXIL은 이전 current IR 비교 바이너리와도 일치한다. 진단 옵션을 실제 RHI에 추가하지 않았다. 새 cached GBuffer의 SPIR-V 실패도 기존 기본 entry와 원인이 같다는 이유로 통과 처리하지 않는다.

### 아직 수행하지 못한 검사

이 환경에는 Windows/MSVC 전체 엔진 빌드와 실제 GPU가 없다. native Debug/Release 빌드, DX12 GPU validation, native Vulkan, 새 영상·shadow·motion 결과, 실제 frame 성능의 A/B 측정은 미실행이다. 작성된 native probe의 성공 marker를 실행 결과로 기재하지 않는다.

추가한 coarse native probe는 normal/partial-wave의 16개 case로 empty/tail/skewed/4096 bins, 완전히 거절되는 partition, 기존 visible-ID/indirect/ring 종료 oracle를 검사하도록 했다. 기존 48개 dynamic ring case도 유지한다. 이 fixture의 경계는 compatibility/unskinned 입력에 수동으로 제공하므로 cooked CPU bounds 생산자와 GPU bone reduction을 end-to-end로 검증하는 범위는 아니다.

shadow native probe에는 8개 prediction case와 missing depth 및 RG version·epoch 거부, retained-history 거부를 추가했다. 현재 CPU oracle의 성공과 native probe 미실행을 별도로 기록한다. cache의 전용 `--primitive-cache-only` mode는 기존 visibility reconstruction 32 frames와 temporal bucket 12 frames에 실제 `SceneHostBudget::m_primitiveCache=true`를 전달하도록 작성했다. Clear/Populate의 존재, packet 생성 및 Resolve와의 실행 순서, cache 두 버전의 Pixel SRV read를 compiled graph에서 확인한 뒤 기존 독립 pixel/motion oracle를 수행한다. 이 44 frames는 아직 실행된 결과가 아니다. live renderer 환경변수만으로 native probe의 기본 budget이 바뀐다고 가정하지 않는다.

현재 RHI SPIR-V 옵션으로 baseline의 일부 subgroup capability 오류가 재현된다. 해당 오류를 없애려고 장치의 supportedOperations/stages 검증 없이 capability만 선언하지 않는다. 진단용 capability/profile로 컴파일돼도 생산 Vulkan 수용은 아니다. 기존 baseline에도 있는 오류와 이 PR의 새 오류는 비교 근거로 구분한다. include boundary 검사도 전체 통과가 아니다. 기준 master의 1,785개 source/project 파일 미러와 현재 코드에서 동일한 27개 상향 include 및 70개 물리 경계 부채가 보고됐으며, 추가 위반은 0건이다. allowlist를 수정하거나 기존 실패를 통과로 표시하지 않았다.

### 재현 명령

도구 경로는 검증 환경에 맞게 지정한다. runner는 도구를 자동 다운로드하지 않는다.

```sh
python Tools/regression/verify-reac-shaders.py \
  --slangc /path/to/slangc --dxc-root /path/to/dxc \
  --spirv-val /path/to/spirv-val --output /tmp/reac-current

python Tools/regression/verify-reac-shaders.py \
  --slangc /path/to/slangc --cpu-bounds-only --sanitize \
  --output /tmp/reac-bounds

python Tools/regression/verify-reac-shaders.py \
  --slangc /path/to/slangc --cpu-positions-only --sanitize \
  --output /tmp/reac-positions

python Tools/regression/verify-shadow-camera-prediction-cpu.py \
  --slangc /path/to/slangc --sanitize --output /tmp/reac-shadow

python Tools/regression/verify-primitive-cache.py \
  --slangc /path/to/slangc --sanitize \
  --output /tmp/reac-primitive-cache.json
```

위 shader runner의 기본 strict SPIR-V mode는 알려진 생산 RHI 옵션 결함을 실패로 반환한다. `--diagnostic-capabilities`를 별도 새 출력 디렉터리에서 사용한 결과는 진단용이다. source tree의 제약에 맞는 도구 버전을 사용해야 한다.

native에서는 `SceneHostIdentity` 41의 material 자산을 재쿠킹하고 다음을 수행한다. 실제 빌드 명령은 [CONTRIBUTING.md](../../CONTRIBUTING.md)와 [회귀 검사 README](../../Tools/regression/README.md)의 해당 프로젝트 절차를 따른다.

```powershell
& .\Tools\regression\verify-material-scene-probes.ps1 -Configuration Release `
  -OutputDirectory 'Artifacts/REAC20261019-Release-fresh' `
  -Modes '--meshlet-dispatch-only','--meshlet-hzb-only','--cluster-lod-only',`
         '--shadow-correctness-only','--visibility-reconstruction-only',`
         '--material-bucket-only','--temporal-bucket-only','--primitive-cache-only'
```

성능 표본은 GPU validation 및 상세 readback 검사를 끝낸 다음 별도로 수집한다. 같은 카메라·장면·해상도·build·driver·clock 조건에서 기본, cache만, shadow prediction만, 둘 다의 네 경로를 비교한다. 최소한 CPU Prepare/Declare wall time, GPU Reset/Cull/Clear/Populate/Resolve/Temporal/shadow raster, 전체 GPU frame p50/p95, 빈 목록·최종 이미지·motion 동등성, 메모리 최고값을 남긴다. coarse 변경은 기준 commit과 동일 장면으로 비교하고 published Debug 값을 새 Release 수치의 분모로 쓰지 않는다.

## 8. cpu-performance-engineering 활용 범위

[`usamahz/cpu-performance-engineering`](https://github.com/usamahz/cpu-performance-engineering)의 설치된 전용 MCP connector는 없었다. 로컬 `cpu-perf==0.1.1`의 offline selftest는 성공했으며, source snapshot은 `deb5a0bac467`이다. 공식 MCP client와 직접 stdio JSON-RPC 모두 `initialize` 응답을 받지 못했다. 따라서 MCP 분석을 성공적으로 수행했다고 기록하지 않는다. 해당 증거는 [cpu-perf-stdio.json](reac-20261019/cpu-perf-stdio.json)에 보존한다.

대신 저장소가 강조하는 환경·컴파일러·workload·baseline·측정 방법의 신원 확보, 병목 가설과 correctness oracle의 분리, 불필요한 메모리 이동·할당·반복 연산 최소화, 동일 조건 재측정 원칙을 적용했다. 공개된 Debug CPU/GPU 캡처를 서로 다른 ETW 표본이나 새로운 코드의 결과로 섞지 않는 것도 이 원칙에 따른다. 외부 서버가 엔진을 측정하거나 최적 성능을 보증한 결과는 없다.

## 9. 남은 후속 작업과 CPU 최적화의 관계

| 항목 | 이 PR 이후 남는 일 | 판단 기준 |
| --- | --- | --- |
| CPU Prepare/Declare | 큰 Declare 병렬 분기를 실제로 활성화한 표본과 worker/core scaling 확보. Prepare의 작업량·wait/ready 분리. | 동일 Game view의 wall time과 전체 RenderLoop p50/p95. GPU 단계 단축으로 대체 해석하지 않음. |
| 인스턴스 front stage | 별도 compacted instance survivor 목록, spatial/bone-local tighter bounds, first/recovery bounds 재사용의 손익. | 전부 보이는 장면의 Reset 비용까지 포함한 native A/B. |
| 프리미티브 cache | 실제 hit/miss·hash 충돌·소형 삼각형·helper 비율, 압축/가변 용량/이전 위치 필요성. | 추가 producer와 읽기 대역폭까지 합한 순이익. |
| Material packet 밀도 | 기존 8×8 record/tile의 record 중복, 64-entry page에 따른 shader 분할 계측. | 새 정렬 기능이 아니라 현행 패킷 밀도 개선으로 범위 한정. |
| Shadow 확장 | light-space 재투영 atlas, 유효한 ray-hit 생산자/history, SW shadow raster 및 point/spot 지원. | offscreen/광원 이동/수직 공간에서 무누락, dependency 증가 포함 시간. |
| Residency/LOD | active working set 초과 시 정책, 가변 heap, skinned coarse LOD의 변형 오차. | 상주 페이지 보호·완료 fence·장시간 스트레스. |
| 멀티큐/aliasing | 동시 사용 계약, 장치별 비용 보정, 실제 overlap/대역폭 경쟁. | 예측 mode와 실제 queue 제출을 구분한 전체 프레임 측정. |
| Vulkan | 현재 RHI capability/profile와 device feature admission 정합, native probe. | SPIR-V 진단 성공을 실제 장치 수용으로 올리지 않음. |

이번 구현은 앞선 CPU 작업과 같은 렌더링 파이프라인을 대상으로 하는 후속 확장이다. 두 CPU scope의 내부 병렬 실행을 직접 끝냈다는 의미는 아니다. 특히 현재 기준선에서 CPU Prepare/Declare의 비중이 크므로, GPU 작업량을 줄인 뒤에도 CPU 전용 측정과 개선은 독립적인 수용 과제로 남는다.

## 10. 게시한 코드의 고정 근거

아래 링크는 구현 commit `deeaff2785fa059fe13e632ff934ce292ccf2801`에 고정한다. 실행한 모든 변경 파일의 SHA-256은 검증 JSON에 보존한다.

| 구성 | 코드 근거 |
| --- | --- |
| 정적 instance bounds 생산 | [MaterialGraphMeshSurface.cpp](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/MaterialGraphMeshSurface.cpp) |
| GPU 조기 판정 / feedback | [GeometryVisibility.slang](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang), [GeometryInstanceBounds.slang](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/GeometryInstanceBounds.slang) |
| GPU cache 자원 / graph 수명 | [MaterialGraphPrimitiveCache.h](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/MaterialGraphPrimitiveCache.h) |
| GPU cache 생산자 / 소비자 | [MaterialGraphPrimitiveCache.slang](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphPrimitiveCache.slang), [MaterialGraphSceneHost.slang](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang) |
| CPU admission / cooked PSO 선택 | [MaterialGraphSceneHost.cpp](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/MaterialGraphSceneHost.cpp), [MaterialGraphProduct.h](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/MaterialGraphProduct.h) |
| Shadow prediction 의존성 / current-only HZB | [MaterialGraphSceneShadowStage.inl](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/MaterialGraphSceneShadowStage.inl), [GpuGeometryVisibilityShared.inl](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/GpuGeometryVisibilityShared.inl), [GpuGeometryOcclusion.cpp](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Engine/RenderEngine/GpuGeometryOcclusion.cpp) |
| Native 검사 진입과 실행 marker | [verify-material-scene-probes.ps1](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Tools/regression/verify-material-scene-probes.ps1), [material_raster_surface_probe.cpp](https://github.com/29thnight/CreatorEngine/blob/deeaff2785fa059fe13e632ff934ce292ccf2801/Tools/regression/material_raster_surface_probe.cpp) |

</details>
