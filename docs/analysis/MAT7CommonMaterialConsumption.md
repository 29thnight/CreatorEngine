# MAT-7 공통 Material 값·바인딩·쿠킹 복구

**2026-10-02 · 두 번째 구현 단계 · MAT-7 전체는 진행 중**

**실행 환경의 소유는 LX다.** ShaderMeta는 생성 계약을 표현하고 검증하는 데 재사용한다. 이번 구현의 graph generation/instance가 계약·값·texture owner를 소유하며, 기존 ShaderMeta cache slot에 graph를 등록하지 않는다. 이후 세 번째 단계에서 authored ShaderMeta/코드 셰이더의 값·자원을 같은 LX runtime 타입과 frame snapshot에 연결했다. [후속 검증 기록](MAT7CodeToLXRuntime.md).

## 구현 범위

`CompileSceneProduct`가 실제 생성 ShaderMeta 문서·전체 host Slang·공통 reflection layout을 `VerifiedProduct::materialShader`에 보존한다. `GenerationStore`와 Material instance는 이 immutable owner를 공유한다. 생성 파일의 주소 수명이나 별도 ShaderMeta cache slot에 기대지 않는다.

- `Material::GetGeneratedShaderMeta`, `GetShaderBindingLayout`, `GetShaderPropertyValues`, `GetConstantBufferData`, `BuildShaderPropertyBlock`은 같은 accepted graph generation/instance를 소비한다.
- 기존 typed getter/setter도 생성 CB/property 이름으로 동작한다. 숫자 편집은 stable Blackboard ID override를 갱신하고 공통 `MaterialPropertyPacker`로 다시 패킹한다. 그래프 편집과 공통 편집 사이에 별도 값 원본을 만들지 않는다.
- 텍스처 override는 source ID로 보존한다. 같은 ID가 SRGB/Data 등 여러 물리 texture slot에 나타나면 모든 별칭의 GUID·CPU owner를 함께 갱신한다. 공통 texture owner/name 조회가 이 snapshot을 사용한다. owner만 덮는 legacy `UseTextureMap`은 graph generation을 변경하지 않는다.
- `RenderBindingCache`는 공통 packer가 만든 accepted uniform을 `PrepareResourcesWithUniforms`로 업로드한다. 같은 값을 매 프레임 LX packer로 다시 패킹하지 않는다. 기존 texture view/handle/encoding 및 sampler 검증은 유지한다.
- LXMC와 CEMF의 MaterialProgram **artifact version은 3**이다. CEMF 자체 schema는 유지한다. generated metadata/source와 typed product table·backend/entry/profile bytecode를 한 artifact에 저장한다. 기존 v2는 재쿠킹이 필요하다.
- `ShaderMetaLoader::ParseGenerated`는 파일 읽기 없이 제공된 source bytes의 SHA-256과 기존 schema를 검증한다. 복구 시 canonical metadata/generation identity를 재생성·대조하고 기존 verified product layout을 공통 reflection 계약으로 복원한다. declared pass와 양 backend의 stage 집합이 일치해야 한다.
- Editor warm cache도 같은 LXMC v3 reader를 사용한다. 생성 계약 없는 제품 graph generation은 DataSystem이 거부한다. 값/type/private property/texture/cook 오류는 기존 Material snapshot 또는 caller output을 보존한다.

## 검증

| 게이트 | 결과 |
|---|---|
| 실제 Slang DXIL/SPIR-V·typed schema·공통 패킹·texture alias·실패 보존·source-free LXMC 복구 | 통과, standalone 68항목 |
| 실제 DataSystem/Material 소비·값 편집·저장/재개방·authoring warm cache | Debug/Release 각각 25항목 통과 |
| 실제 Debug/Release AssetCooker의 반복 cook·source-free consumer | 두 구성 모두 생성 쌍 hash 일치, Derived-only 패키지 복구 통과 |
| CreatorEditor Debug 및 엔진/도구 빌드 | 전체 Debug 에디터, Debug/Release RenderEngine·AssetCooker·consumer 빌드 통과 |
| 기존 텍스처 DataSystem 소비·공통 texture get/set·clone·저장·reload/removal 회귀 | Debug/Release 각각 31항목 통과 |
| 기존 low-level product/runtime 회귀 | product 121항목·compiled 10·GPU components 60, runtime 43항목·동시 load 8 통과 |

standalone 증거는 `Build/Obj/MaterialShaderMetaProbe/adapter.log` 및 `Run-82d078371c334254857d6da5cd15e2d1/`에 있다. actual consumer는 `Tools/regression/MaterialCommonConsumerProbe.vcxproj`, 재현 게이트는 `verify-material-shadermeta.ps1 -VerifyAssetCooker -VerifyConsumer`다.

최종 실제 cook/consumer 증거는 위 run의 `Cook-Debug-071857505f8546fe8f2c960708640f65/`와 `Cook-Release-41f7f32e362b40b99743f86430ec8478/`에 있다. `Accepted.log`·`Repeated.log`와 `consumer.log`를 보존한다. 패키지 검증 구간에서는 Slang DLL을 로드하지 않으며, 뒤이어 별도 authoring 구간에서 cold compile 후 생성 파일을 치워도 warm cache가 같은 계약을 복구하는지 검사한다.

에디터 빌드 증거는 `Build/Obj/MaterialShaderMetaProbe/common-creatoreditor-debug-build.log`다. 기존 unity 컴파일의 C1128을 피하기 위해 이번 빌드에서만 `/bigobj`를 적용했다. 기존 LNK4229(`/DELAYLOAD:vulkan-1.dll`), Release Utility_Framework의 LNK4020 PDB 경고는 남아 있으며 경고 없는 빌드나 Release 심볼 완전성을 주장하지 않는다.

기존 DataSystem 회귀 도구는 low-level product fixture 대신 실제 Scene compiler로 generated 계약을 포함한 fixture를 쿠킹하도록 변경했다. reflgen 이전의 `Meta::Register<Material>` 호출도 엔진의 생성 등록 함수·공유 registry 초기화로 교체했다. fixture를 기준으로 제품 코드의 등록 방식을 되돌리지 않는다.

기존 텍스처 회귀의 재현은 `verify-material-datasystem.ps1`이며 이번 실행의 엔진/도구 사전 빌드 뒤에는 `-SkipProjectReferences`를 사용했다. 결과는 `Build/Obj/MaterialProductProbe/datasystem-Debug.log`·`datasystem-Release.log`에 있다. 실제 WIC decoded SRGB texture owner, 공통 texture 값/owner 일치와 stable-ID setter, 로드 실패 시 owner 보존, clone·저장/재개방·변경/제거를 확인한다. malformed document를 의도적으로 거부하는 오류 로그는 실패 보존 게이트의 입력이다.

대시보드 전체 스크립트 구문 및 모든 페이즈 진행률의 finite 계산을 확인했다. MAT-7 `progress`/`days: null`, PHASE 4.25 열림을 유지한다.

`verify-material-product.ps1`의 새 adapter 의존 소스·라이브러리 경로를 보완하고 기존 low-level product/runtime fixture도 재실행했다. 증거는 `Build/Obj/MaterialShaderMetaProbe/common-product-regression.log` 및 `Build/Obj/MaterialProductProbe/product.log`다. 실제 RTX 4070 Ti에서의 기존 GPU component 검사는 보존됐으며, 새 공통 Material의 전체 PSO/Scene 렌더 통합 검증으로 확대하지 않는다.

## 남은 통합

이 단계는 **공통 값/바인딩 소비와 cooked generation 복구**다. 기존 Scene host PSO/renderer가 공통 shader/PSO owner로 전환된 것은 아니다. authored ShaderMeta cache handle과 graph generation owner도 아직 최종 단일 shader handle로 합치지 않았다.

1. 코드 generic 값/자원 적응은 후속 세 번째 단계에서 연결했다. coverage·host compile options/permutation/include와 LX shader/PSO 소비는 계속 남는다.
2. 일반 alpha Blend graph와 코드 재질을 공통 Forward+ light list·sort·depth/HDR 합성에 연결한다.
3. Preview/Scene/Game 및 cooked Player의 PSO 전환 이후 제품 회귀를 검증한다. GPU 이미지·전체 모델 성능·encrypted PAK의 새 generated 계약 재실행은 이번 CPU/loose consumer 게이트로 대체하지 않는다.

MAT-7 `progress`/미산정, Phase 4.25 열림, BRDF 1024 / environment 4096을 유지한다.
