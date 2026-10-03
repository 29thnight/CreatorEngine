# MAT-7 코드 ShaderMeta 입력의 LX 실행 환경 연결

**2026-10-02 · 세 번째 구현 단계 · MAT-7 전체는 진행 중**

## 구현 범위

`LX::Runtime::ShaderGeneration`이 ShaderMeta 계약·reflection layout을 소유하고, `LX::Runtime::Instance`가 값·uniform bytes·texture owner·keyword selection을 소유한다. 그래프의 generated shader/instance와 코드 재질이 이 공통 타입을 사용한다. ShaderMeta는 계약 표현이며, 그래프 전용 Principled/BSDF ABI를 코드 셰이더에 강제하지 않는다.

- `Material::ConfigureShaderProperties`는 코드 입력을 LX shader/instance로 변환한다. Float/Float2/Float3/Float4/Int/Bool/Float4x4/Texture2D를 기존 공통 packer로 처리한다. typed getter/setter와 texture/keyword 수정은 accepted instance를 조회하거나 새 immutable instance를 게시한다.
- 코드 shader 계약은 compatibility handle과 전체 metadata/layout 일치로 재사용한다. weak cache가 계약 수명을 소유하지 않으며 accepted instance가 계약을 보존한다. reload·값/type/private property/keyword/layout 검증 실패는 기존 owner와 bytes를 유지한다.
- 코드의 기존 공개 property/cbuffer/texture 필드는 저작·직렬화 호환 입력으로 동기화한다. 런타임 조회는 LX owner를 사용한다. Graph의 stable parameter ID 입력은 기존 graph 어댑터에서 해석하며 동일 공통 타입으로 내려간다.
- `ExperimentMaterialSealing::SealCore`가 실제 GBuffer/Forward 코드 재질의 property block과 LX instance를 함께 준비한다. `EnhancedSceneRenderer`는 이 owner를 draw snapshot에 보존한다. texture/UV 검증까지 성공한 뒤 출력 전체를 게시한다.
- 기존 experiment 저작 값 variant가 지원하지 않는 Float4x4는 legacy Material의 generic 값 입력에서 보존해 frame sealing으로 전달한다. experiment 저작 variant 자체에 새 행렬 타입을 추가한 것은 아니다.
- 그래프의 cooked LXMC v3 형식과 CEMF schema는 유지한다. 이번 변경은 기존 코드 재질의 GPU bytecode 쿠킹·PSO cache를 대체하지 않는다.

## 검증 범위

`MaterialCommonConsumerProbe`의 코드 재질 검증은 실제 Slang DXIL/SPIR-V compile/reflection을 사용한다. typed 값·immutable 수정·실패 보존·공유 shader owner·GBuffer/Forward frame sealing·실제 DataSystem authoring/binary 저장 왕복을 검사한다. 컴파일 fixture는 generic compute shader이며, 새 코드 재질의 GPU 렌더 이미지나 전체 모델 성능을 검사하는 게이트는 아니다.

실행 로그는 `Build/Obj/MaterialShaderMetaProbe/`에 보존한다. 최종 빌드·실행 결과는 아래 표로 기록한다.

| 게이트 | 결과 |
|---|---|
| 코드 shader DXIL/SPIR-V·generic 값·immutable 편집·실패 보존·frame seal·DataSystem 왕복 | Debug/Release 각각 35항목 통과 |
| 그래프 common Material·Derived-only 소비·warm cache | Debug/Release 각각 25항목 통과 |
| 실제 AssetCooker 반복 cook·source-free consumer 재실행 | Debug/Release 모두 생성 쌍 hash 일치, 코드 35·그래프 25항목 재통과 |
| 기존 texture DataSystem·owner·편집·clone·저장·reload/removal 회귀 | Debug/Release 각각 31항목 통과 |
| 기존 생성 어댑터·typed schema·reflection·source-free cooked 복구 | 68항목 통과 |
| 기존 low-level product/runtime 및 GPU component 회귀 | product 121항목·compiled 10·GPU components 60, runtime 43항목·동시 load 8 통과 |
| 전체 CreatorEditor Debug 빌드 및 최종 엔진 재링크 | 통과 |
| 대시보드 및 게이트 스크립트 | 전체 JS 구문·모든 phase 진행률 finite, MAT-7 progress/미산정 유지. PowerShell 게이트 2개 구문 통과 |

코드 fixture는 `material_code_runtime_tests.cpp`, 기존 graph consumer는 `material_common_consumer_probe.cpp`이며 같은 실제 엔진 링크 도구에서 실행한다. 최초 실행 로그는 `lx-code-debug-consumer.log`·`lx-code-release-consumer.log`다. adapter/product 회귀 로그는 `lx-common-adapter-gate.log`·`lx-common-product-gate.log`다.

Release/LTCG에서 graph Float3/Float4 분기의 16바이트 임시 배열과 generic matrix 호출의 64바이트 크기를 함께 인라인하며 C4789가 검출됐다. 해당 분기에 명시적인 크기 거부를 추가한 뒤 두 구성의 빌드·generic matrix 회귀를 통과했다. Debug 에디터의 기존 unity C1128을 피하기 위한 `/bigobj`는 이번 빌드 환경에만 적용했고, LNK4229(`/DELAYLOAD:vulkan-1.dll`) 경고는 남아 있다.

최종 반복 cook/consumer는 `lx-code-cook-consumer-final.log`, texture 회귀는 `lx-code-datasystem-final.log`와 `Build/Obj/MaterialProductProbe/datasystem-Debug.log`·`datasystem-Release.log`에 있다. 새 cook fixture/run은 `Run-7cd61e88a7aa4d408d28c6a88b693025/`다. 최종 엔진/consumer 빌드는 `lx-code-debug-build-final.log`·`lx-code-release-build.log`, 쿠커는 `lx-code-cooker-debug-build-final.log`·`lx-code-cooker-release-build-final.log`, 에디터는 전체 `lx-code-creatoreditor-debug-build.log` 및 수정된 엔진 재링크 `lx-code-creatoreditor-debug-build-final.log`에 보존한다.

Release 쿠커의 기존 `.ipdb`가 C1354를 내어 별도 `IntDir`에서 다시 생성했다. 같은 임시 폴더를 사용하는 RuntimeLauncher의 MSB8028 경고가 있었으나 순차 빌드·실제 Release cook/consumer는 통과했다. 기존 산출물 캐시를 복구 완료했다고 선언하거나 저장소의 빌드 설정을 변경한 것은 아니다.

## 남은 통합

1. coverage·host compile options/permutation/include identity와 backend bytecode·PSO를 LX shader owner로 연결한다. 코드 compatibility handle/native pass cache와 기존 graph Scene host PSO는 아직 남아 있다.
2. 일반 alpha Blend graph와 코드 재질을 공통 Forward+ light list·혼합 정렬·depth/HDR 합성에 연결한다.
3. Preview/Scene/Game·cooked Player의 공통 PSO 전환 이후 mixed Graph/Code draw, GPU 이미지, 전체 모델 성능 및 encrypted PAK 재실행을 검증한다.

MAT-7 `progress`/미산정, PHASE 4.25 열림, BRDF 1024 / environment 4096을 유지한다.
