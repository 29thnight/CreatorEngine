# 머테리얼 · 셰이더 파이프라인 재설계 (PHASE 3.5)

2026-08-11 작성, 2026-08-27 모델 임포트 교차 배선 재감사. Vulkan 골격
(vk.selftest)이 선 직후 작성. RhiBoundaryPlan §7.2가 다음
슬라이스로 V5(셰이더 컴파일 93건)·V6(파이프라인 상태 기술 107건)를 지목하는데,
그 둘에 착수하기 전에 **그 인터페이스를 섬길 소비자 — 머테리얼·셰이더 애셋
파이프라인 — 의 모양을 먼저 정한다.** 구 RHI가 죽은 이유가 "소비자 없는
추상"이었고(RhiBoundaryPlan §1.1), V5를 지금 모양의 셰이더 시스템에 맞춰 자르면
같은 실수를 반복한다. 목표 사슬:

```
Material → 셰이더 메타데이터 → (Defines · Pass · RenderStates)
        → 셰이더 퍼뮤테이션 → HLSL 소스 → Slang 컴파일러 → DXIL | SPIR-V
```

---

## 0. ★ 이 문서가 하루도 못 가 낡았다 (2026-08-11 정정)

작성 직후 확인해 보니 **§1의 전제 절반이 이미 사실이 아니었다.** 계획을 쓰는
동안 같은 트리에서 두 커밋이 나갔고, 그것이 M0와 M4의 대부분을 실행했다:

| 커밋 | 한 일 | 이 문서에 미친 것 |
|---|---|---|
| `cc12ba4f` 02:56 | 패스 24곳의 인라인 HLSL 45블록(3,792줄)을 파일로. `RHIShaderSource`(중립 읽기) / `DX12ShaderCompiler`(백엔드 컴파일) 분리 | **M4 후반 완료 · M1 골격 완료** |
| `24e784ce` 02:56 | 옛 셰이더 시스템 폐기 — 소스 17파일 · 자산 136파일 · 약 14,800줄 | **M0 완료(범위 초과)** |
| `1ab3d12b` 07:42 | V5 범위 재측정 93 → 66, 성격 변경 | §1.4 · §4 M1 수치 무효 |
| `a253a22d` 07:49 | 자가 검증 기준선 33종 → 35종 | §4 판정 문구 무효 |
| 2026-08-16 M1A 수직 슬라이스 | `IRHIShaderCompiler` + 단일 DXC DXIL/SPIR-V 구현 + transitive include 콘텐츠 캐시. 구 DX12/Vulkan 컴파일러 제거 | **중립 서비스 기준선 완료** |
| 2026-08-24 Slang 전환 평가 | 현재 HLSL 52파일은 런타임 define 반영 시 Slang 프런트엔드 52/52 통과. GBuffer·Forward Cull·SSAO·SSGI·Shadow·IBL 대표 8변형은 DXIL/SPIR-V 16/16 코드 생성 | **M1B로 컴파일러 백엔드 전환. 전면 언어 재작성은 하지 않음** |

**가장 크게 틀린 것은 §1.1의 골자다 — "컴파일 시스템이 둘"은 이제 거짓이고,
하나다.** 애셋 경로(`ShaderSystem`·`ShaderPSO`·`ShaderDSL`·`VisualShader*`·
`HLSLCompiler`·`Shader.h`·`PSO`·`ShaderSelectionWindow` + `.hlsl` 113 ·
`.hlsli` 11 · `.shader` 12)가 통째로 사라졌다. 폐기 근거는 이 문서 §1.1이
적은 것과 같다(그림에 닿지 않는다). 즉 **결론은 맞았고 그 결론에 따른 실행이
계획보다 먼저 도착했다.**

아래 §1은 정정하되 원문의 판단은 남긴다 — 무엇을 보고 이 페이즈를 세웠는지가
근거이기 때문이다. 갱신된 현재 위치는 §1.0에 따로 적는다.

### 1.0 지금 실제 위치 (2026-08-24 재실측)

| 항목 | 값 | 판정 |
|---|---|---|
| 셰이더 컴파일 시스템 | **1개**(`RHIShaderCompiler`) | `IRHIShaderCompiler` 뒤 단일 Slang 구현 |
| 목표 컴파일러 | **Slang 2026.14, 이관 완료** | M1B가 인터페이스·패스 호출부를 유지한 채 구현·배포를 교체 |
| `D3DCompile` 호출 지점 | **0곳** | FXC 은퇴 · 구 DX12/Vulkan 컴파일러 소스 제거 |
| 패스의 `D3DCompile` | **0건** (착수 전 19) | 〃 |
| 컴파일 헬퍼 | **1개** (착수 전 22) | 오류에 파일·엔트리·타깃이 항상 붙는다 |
| HLSL 소스 | `Assets/Shaders/DefaultPassShader/` 루트 **38** + `SelfTest/` **11** = **49** | 파일이다(문자열 아님), `#include` 가능 |
| `D3DReflect` | **0건** | 리플렉션 몫은 폐기와 함께 소멸 |
| 컴파일 결과의 `ID3DBlob` | **0** | `RHIShaderBlob` 소유 바이트로 이관 완료 |
| `D3D_SHADER_MACRO` · `RHIShaderDefine` | **0 · 0** | M2A가 4패스의 손 배열 6그룹을 소유형 `RHIShaderPermutation`으로 대체 |
| 코드 기반 퍼뮤테이션 | **4패스 · 6그룹** | Shadow 1 · Forward 2 · SSAO 2 · SSGI 1, 정렬된 name/value와 128-bit key가 컴파일·캐시 정체성 |
| `.shadermeta` | **schema v1 · strict YAML loader** | property·keyword·pass/stage·RHI state·queue를 소유, catalog GUID를 재사용하고 별도 전역 registry는 없음 |
| `.cso` · 읽는 코드 | **0 · 0** | 정리됨 |
| `.meta` 고아 | 129 | **손댈 것 없음** — git 미추적(`.gitignore:465`)이고 `DataSystem` 초기화의 `ScanAndCleanupInvalidMeta`가 다음 실행에서 지운다(호출자 확인: `DataSystem.cpp:96,98`) |
| `Material::TrySetValue` 계열 | **M5-A에서 재가동** | `ConfigureShaderProperties`가 M7 소유 layout을 붙인 뒤 타입 검증된 논리 값과 CB byte view를 함께 갱신한다. 미구성 legacy material은 계속 fail-closed다 |

### 1.0.1 Slang 전환 결정 (2026-08-24)

- **1차 목표는 언어 이사가 아니라 컴파일러 백엔드 교체다.** `.hlsl`·명시적
  `register(...)`·현재 Vulkan `b/t/u/s` 시프트를 그대로 두고, `IRHIShaderCompiler`
  구현만 Slang API로 옮긴다. `.slang` 일괄 개명·모듈화·자동 바인딩은
  동일 슬라이스에서 하지 않는다.
- **검증용 이중 컴파일은 한시적 도구일 뿐이다.** DXC와 Slang을 영구히 런타임
  선택지로 남기지 않는다. 전수 산출·바인딩·픽셀 동등성이 닫히면 제품
  경로는 Slang 하나로 전환한다.
- **`.shadermeta`가 저작 스키마의 정본이다.** 프로퍼티 기본값·에디터 라벨·키워드
  축·패스·렌더 상태는 Slang reflection으로 유출하지 않는다. reflection은
  스키마와 실제 cbuffer·리소스 바인딩을 대조하는 컴파일 산출물이다.
- **배포는 설치된 Vulkan SDK에 기대지 않는다.** 선택한 Slang 릴리스의
  헤더·`slang-compiler.dll`·라이선스·DXIL 산출에 필요한 구성을 하나의
  `ThirdParty/Slang` 번들로 고정한다. 캐시 identity에는 그 정확한 버전을 넣는다.
- 위 52/52·16/16은 **소스 호환성 프로브**다. 전체 엔진 빌드·DX12/Vulkan 실행·
  reflection 동등성·픽셀 회귀를 통과한 구현 완료 증거로 쓰지 않는다.

★ **M0에서 "죽은 사슬을 지우지 않는다"고 한 판단은 유지됐으나 내가 적은
이유는 틀렸다.** 나는 "`.shader` 애셋 로드가 아직 그 위에 있다"고 했는데 그
`.shader`가 함께 폐기됐다. 실제 이유는 `24e784ce`가 적은 것이다 — 게임
스크립트와 C# 인터롭이 부르고, **이것이 다음 셰이더 저작 언어가 다시 물릴
자리**다. 폐기 범위를 "ShaderPSO 의존"으로 긋고 "이음매"까지 넓히지 않았고,
넓혔으면 게임 스크립트 프로젝트가 깨졌다. M5-A는 이 API 표면은 보존하되 늘
널이던 `m_cbMeta`와 임시 `MaterialParameters.h`를 제거하고 M7의
`ShaderMetaBindingLayout` 소유 복사본에 직접 연결했다.

★ **소스 파일화가 V5보다 먼저 와야 했다는 것을 놓쳤다.** 이 문서는 M1(컴파일러)
→ M4(애셋화) 순으로 잡았는데 `cc12ba4f`가 반대로 갔고 그쪽이 옳다. 문자열은
런타임 컴파일이 전제인데 Vulkan은 OS가 주는 컴파일러가 없어서, 문자열인 채로
V5를 하면 **Vulkan에서 성립하지 않는 계약을 중립화하게 된다.**

### 1.0.2 실제 소비·ModelImport 교차 상태 (2026-08-29 재감사)

M5의 저장·reflection 구조와 M7의 양 backend 대조 위에 M6-P0의 격리된 숫자 property
프로브, M6-P1a의 제품 GBuffer 숫자 property 소비, M6-P1b1의 texture generation 소유,
M6-P1b2a의 texture GUID/register 배선, M6-P1b2b1의 material keyword permutation
PSO와 M6-P1b2b2의 multi ShaderMeta generation PSO까지 섰다. `BuildDrawPool`은 최종 draw item에
`Material*`를 남기지 않고 ShaderMeta generation·복사된 binding layout·keyword·property bytes와
네 texture의 property 이름·논리 GUID·reflection register/space·`shared_ptr` owner를 immutable
snapshot으로 밀봉한다. GBuffer는 그 전체 내용을 batch key로 삼아 batch별 `b2`와 texture를
올린다. Material은 texture generation을 이름 기반 owner vector로 직접 소유하며 P2d-e에서
public raw view를 제거했다. GT frame packet은 primary와 Host required packet이 참조하는 복수 ShaderMeta generation/value를
소유하고, 제품 GBuffer는 material별 meta+keyword PSO를 batch 직전에 고른다. M6-P2a는 제품
Forward draw의 숫자 값과 네 texture generation도 별도 immutable packet으로 밀봉해 raw
texture 수명 경계를 닫았다. M6-P2b는 별도 `forwardShaderMetas` frame owner, Standard 48B
`b2`, reflection `t4..t7`, material keyword별 일반/Reference PSO pair를 제품 Forward에 연결하고,
기존 back-to-front 순서를 보존한 채 인접 호환 draw만 batch한다. M6-P2c는 실제
`ForwardWater`·`ForwardWind` Material의 non-nil `m_shaderMetaGuid`가 별도 ShaderMeta를 고르게 하고,
Standard 48B prefix 뒤 대표 custom float 4개를 붙인 64B `b2`와 기존 `t4..t7` 계약을 양
backend에서 관통했다. 당시 Scene/proxy 소유 non-cache Material도 대표 generation을 얻도록
Water/Wind seed를 frame에 싣던 bridge는 M6-P2d-d의 Host required-asset packet으로 교체됐다.
M6-P2d-a는 그 대표 재질의 실제 소비자인
`FoliageRenderProxy`를 타입별 owning draw source로 펼치고, 한 카메라에서 파생된
`m_isCulled` 대신 mesh world bounds를 제품 view별 절두체 판정에 연결했다.
M6-P2d-b는 `Material::m_flowInfo` 32B와 producer frame total/delta를 별도 immutable
flow snapshot으로 밀봉하고 Forward instance upload·Water/Wind 셰이더 소비까지 연결했다.
M6-P2d-c는 `Material`의 runtime texture owner와 GBuffer/Forward draw snapshot을 임의
ShaderMeta property 이름의 vector로 바꾸고 reflection register로 기존 4-slot pass 범위에
투영한다. M6-P2d-d는 Editor/Player Host가 활성 Scene의 Mesh/Foliage Material owner에서 pass별
ShaderMeta GUID를 수집해 임의 required-asset packet으로 밀봉하고 고정 Water/Wind seed를 제거했다.
M6-P2d-e는 제품 frame의 material-cache 전수 스캔 2곳과 draw pool의 legacy field 중복 쓰기를
제거하고, 외부 호출자 0인 raw `Texture*` setter 6개·Material raw texture alias 5개·읽는 곳 0인
`m_dirtyCBs`를 은퇴했다. `ApplyShaderParams`도 실행 코드 0건임을 다시 확인했다. 따라서 M6는
**P0·P1a·P1b1·P1b2a·P1b2b1·P1b2b2·P2a·P2b·P2c·P2d-a·P2d-b·P2d-c·P2d-d·P2d-e 완료,
제품 배선 완료**다.

여기서 제품 배선 완료는 **기존 `::Material`이 제공하던 렌더 계약의 완결**을 뜻한다.
타입 자체의 장기 존치나 `experiment::Material` 직접 소비 완료를 뜻하지 않는다.
새 저작 정본·runtime instance·resolver는 ModelImportPipelinePlan I5-M에서
`experiment::Material` 위에 세우고, 기존 `::Material`에는 전환에 필요한 수정 외의
새 책임을 추가하지 않는다. I5-M parity 뒤 I6가 legacy `::Model`/`::Material`,
Assimp와 변환 bridge를 함께 퇴역시킨다. 따라서 PHASE 3.5의 완료 상태는 유지하되
legacy 타입 퇴역 완료로 확대 해석하지 않는다.

동시에 `experiment::Material`은 목표 구조를 거의 갖췄지만 변환기의 기본 property
이름이 M5 정본과 달랐다. 2026-08-27 `StandardMaterialProperty.h`를 이름 정본으로
추가해 `DataSystem`의 legacy GUID 이행과 `SceneToModelDraft` 기본 매핑을 합쳤다.
glTF/FBX 게이트는 표준 숫자 property 6개와 underscore 별칭 0건을 검사한다.
재질별 `assetId`·`shaderAssetId`·texture ID가 없으면 cooked 게시를 거부하는 D5-a 계약과,
모델 sidecar의 저장된 material/embedded texture UUIDv4·재질별 ShaderMeta resolver·CEMF v1
  GUID manifest를 정의한 D5-b1과 별도 AssetCooker의 단일 `Prim_Cube` CEMC/CEMF 원자 게시
  D5-b2a는 완료됐다. D5-b2b1은 model 14개에서 material 52·embedded texture 96 identity를
  전수 UUIDv4로 재발급하고 CEMC 14 + CEMF 1/entry 66을 결정적으로 cook했다. D5-b2b2는 이를
  package-base snapshot에서 생성해 `AssetPacker`/pak에 게시했다. texture/ShaderMeta 등 나머지
  Derived producer D5-b2c와 experiment 재질의 실제
  draw 소비는 남아 있다.
모델 레이아웃 선행 레인은 V2(68B)에 이어 V3(mesh별 packed mask/stride)까지 완료됐다.

---

## 1. 지금 무엇이 있는가 — 측정

★ 아래는 **착수 시점(2026-08-11 오전) 측정이고 §0에서 절반이 갱신됐다.**
이 페이즈를 세운 근거로 남긴다.

### 1.1 셰이더 컴파일 시스템이 둘이고, 화면에 나오는 쪽은 애셋 파이프라인이 아니다

| 경로 | 소스 | 컴파일 | 퍼뮤테이션 | 화면 도달 |
|---|---|---|---|---|
| (a) 애셋 | `Dynamic_CPP/Assets/Shaders/` — `.hlsl` 114(vs 22 · ps 65 · cs 25 · gs 1) + `.hlsli` 11 + `.shader` DSL 12 | `HLSLCompiler`: `D3DCompileFromFile`, **SM5.0 고정 · 매크로 인자 NULL** (`HLSLCompiler.cpp:57-67`) | 없음 | **못 한다** |
| (b) 패스 내장 | `Enhanced*Pass.cpp` 안 **C++ 문자열 리터럴** HLSL | `D3DCompile` 직접 호출 93건 (24파일) | 손 매크로 배열 | 이것만 그린다 |

★ **`Material::GetShaderPSO()`의 렌더 경로 소비자가 0이다** (grep 재확인:
`Material.cpp`·`ShaderSystem.cpp` 자기 참조뿐). 애셋 경로는 데이터가 끝까지
흐르지만 — DSL 파싱 → `ShaderPSO` 조립 → cbuffer 리플렉션 → `Material`이
값 저장 — **GPU에 올리는 소비자가 없다.** `Material.cpp:447-460`의 주석도
"GPU에 올리는 쪽이 없는 상태"라고 자인한다.

실제 소비 경로는 이것뿐이다:

1. `MeshRenderProxy::m_Material` → `RenderPassData::PushRenderQueue`가
   **`m_renderingMode` 하나만 읽어** deferred/forward 분류 (`RenderPassData.cpp:112-133`)
2. `EnhancedSceneRenderer::copyQueue`가 **텍스처 4장 + PBR 스칼라 4개만** 뽑아
   `EnhancedDrawItem`으로 축약 (`EnhancedSceneRenderer.cpp:2018-2028`)
3. `EnhancedGBufferPass`/`EnhancedForwardPass`가 **자기 안에 박힌 고정 HLSL**로
   그린다 (`kGBufferShader`, 텍스처 슬롯 t0~t3 고정)

따라서 `.shader` 애셋 12종(물·바람·플래시 등 게임 커스텀 셰이더)은 구조상
반영될 수 없다 — 전부 고정 셰이더 폴백으로 그려진다. **커스텀 머테리얼
셰이더라는 기능 자체가 DX12 전환에서 계승되지 않았고, 이 페이즈가 그것을
새 구조로 부활시키는 자리다.**

### 1.2 퍼뮤테이션은 개념이 없다 — 필요한 곳마다 손으로

- DSL의 `Keywords = [...]`는 **파싱만 되고 읽는 곳이 0이다** (`ShaderDSL.cpp:99`
  대입뿐, 소비 grep 0).
- 진짜 필요한 곳은 패스마다 재구현한다: `SHADOW_SKINNING` 두 벌
  (`EnhancedShadowPass.cpp:97-107`) · `REFERENCE_PATH` `variants[]` 루프
  (`EnhancedForwardPass.cpp:855-880`) · SSAO·SSGI의 `D3D_SHADER_MACRO` 손 배열.
  variant가 늘수록 패스 코드의 `D3DCompile` 호출을 하나씩 늘리는 구조다.

### 1.3 렌더스테이트는 애셋 어디에도 없다

`PSO.h:6-22`가 블렌드/래스터/뎁스스텐실 상태 객체를 "호출자 0"으로 걷어냈고,
지금 유일한 정의처는 **패스 C++ 코드가 채우는 `DX12GraphicsPipelineDesc`**다
(D3D12 원시 타입, `DX12PSOManager.h:28-183`). Material이 가진 상태 어휘는
`MaterialRenderingMode{Opaque, Transparent}` 둘뿐 (`Material.h:15-20`) —
아티스트가 블렌드·컬·뎁스를 데이터로 바꿀 자리가 없다.

### 1.4 컴파일러가 목적지에 못 간다

- FXC(`D3DCompileFromFile`) SM5.0 고정. DXIL도 SPIR-V도 못 낸다.
- include 의존 추적이 없어 `.hlsli` 하나 바뀌면 **`.cso` 전체를 삭제**한다
  (`ShaderSystem.cpp:199-221`).
- `_DEBUG` PDB 추출은 `pdbPath == csoPath`라 PDB가 저장된 적이 없다
  (`HLSLCompiler.cpp:78,97`).
- Vulkan은 OS가 컴파일러를 주지 않아 SPIR-V 사전 컴파일이 필수이고, **Windows
  SDK의 dxc는 SPIR-V 출력이 꺼져 있다** — DXC 벤더링이 필요하다
  (RhiBoundaryPlan §7.2.2 실측). 이는 착수 시점 판단으로 남기되, 최종 배포
  방향은 §1.0.1의 Slang 번들 결정이 대체한다.

### 1.5 그 밖의 실측 — 재설계가 밟게 될 자리

- **M5 착수 전 직렬화 3계열 공존**: `.asset` YAML(`DataSystem::LoadMaterial` /
  `EditorAssetDatabase::SaveMaterial`) · 씬 임베디드 수동 파싱
  (`MeshRenderer::OnDeserialized`) · 모델 바이너리(`ModelLoader`의
  `SerializeMaterials`/`LoadMaterial`)였다. M5-B1에서 앞의 둘을 typed reflection +
  `DataSystem` codec/finalize 경계로 합쳤고, M5-B2에서 모델 바이너리도 같은 payload의
  versioned envelope로 옮겼다. 현재 새 필드의 저장 수정 지점은 `Material::reflect()`와
  `DataSystem` codec이며, 무버전 모델 material record는 read-only 호환 경로다.
- **텍스처 슬롯 5개 고정 필드** — 이름 문자열+포인터 쌍이 클래스에 박혀 있다
  (`Material.h:119-135`). 임의 슬롯 추가는 클래스 수정이다.
- **M5 착수 전에는 핫리로드 세대 핸들이 없었다**: 당시
  `DataSystem::RetireCachedAsset`은 legacy raw 소비자를 위해 Model·Material·Texture의
  이전 `shared_ptr` 세대를 종료까지 전역 retired 목록에 붙들었고,
  `FoliageType::m_material`도 Model에서 얻은 raw pointer였다. M5-C2/C3가 ShaderMeta·PSO
  generation을 세웠고 C4가 장기 Model/Mesh/Material 소비자를 소유 참조로 바꿨다.
  현재 전역 보존은 raw texture alias가 남은 Texture·UITexture·SpriteSheet 계열에만
  한정한다.
- **죽은 코드**: `VisualShaderAssets` 맵은 대입하는 코드가 0이고, 비주얼 셰이더는
  파서·자료구조만 있고 HLSL codegen이 없다. `SpriteRenderProxy::m_customPSO`는
  대입만 되고 읽는 그리기 코드가 0.
- RHI 계약(`IRHIDeviceResources`)에는 셰이더·PSO 개념이 없다 — 디바이스·프레임
  생명주기뿐. PSO 캐시는 DX12 전용 구체 타입으로 노출돼 있다.

---

## 2. 무엇을 하려는가

**목표**: 머테리얼이 셰이더를 고르고, 셰이더가 자기 패스·상태·키워드를
선언하고, 퍼뮤테이션이 요청식으로 컴파일되어 DXIL과 SPIR-V 어느 쪽으로든
나가는 **단일 파이프라인**을 세운다. 지금의 두 시스템((a) 죽은 애셋 경로,
(b) 재사용 불가 인라인 경로)을 이것 하나로 수렴시킨다.

**왜 지금(3.5)인가** — 셋:

1. **V5·V6의 인터페이스를 소비자가 정하게 한다.** 컴파일러 호출만
   옮겨 적으면 퍼뮤테이션·캐시·리플렉션 요구가
   나중에 인터페이스를 다시 찢는다.
2. **PHASE 12(빌드 파이프라인)의 셰이더 스텝이 이 파이프라인의 오프라인
   모드다.** 게임 빌드가 셰이더를 미리 굽는 단계는 여기서 정의된 퍼뮤테이션
   전수 컴파일 그 자체다.
3. **PHASE 10(파티클)·11(지형)이 이 위에 선다.** 그쪽에서 셰이더 경로를 또
   만들면 세 번째 시스템이 생긴다.

**하지 않을 것**:

- **비주얼 셰이더 codegen 없음.** 노드 그래프→HLSL 생성은 별건이고, 지금 있는
  스텁(파서만)은 M0에서 은퇴했다. 1차 이관의 소스는 현재 텍스트 HLSL이다.
- **파티클·지형·스프라이트 셰이더 없음** — PHASE 10·11의 몫. 이 페이즈의 배선
  대상은 메시 재질 경로(GBuffer·Forward)다.
- **HLSL 문법 현대화 강박 없음.** 현재 파일은 Slang HLSL 모드로 산출되는 한
  그대로 둔다. 새 모듈·generic·specialization은 컴파일러 전환 후 가치가
  입증된 축에만 별도 슬라이스로 도입한다.
- **M1B에서 `ParameterBlock`·자동 바인딩을 도입하지 않음.** 기존 명시적
  `register(...)`와 Vulkan 시프트를 유지해 컴파일러 동등성과 바인딩 재설계를
  한 슬라이스에 섞지 않는다.
- **머테리얼 에디터 UI 재설계 없음.** 인스펙터는 새 프로퍼티 모델을 읽는 최소
  배선만 한다. 본격 UI와 편집 경험은 별도 UI/에디터 재설계 계획에서 다룬다.

---

## 3. 설계

### 3.1 계층

```
Material (.asset, YAML)          프로퍼티 값 · 키워드 선택 · ShaderMeta 참조(GUID)
  ↓
ShaderMeta (.shadermeta, YAML)   프로퍼티 선언 · 키워드 축 · 패스 목록
  │                              └ 패스마다: 스테이지 엔트리 · 렌더스테이트 · 큐
  ↓
PermutationKey                   (meta, pass, 정규화된 키워드 집합)의 해시
  ↓
IRHIShaderCompiler (V5 승계)     HLSL + permutation inputs → Slang → DXIL | SPIR-V
  │                                   └ target layout → 중립 RHIShaderReflection
  ↓
파이프라인 캐시 (V6 승계)         (blob들 + RHIGraphicsPipelineDesc + RT 포맷) → PSO
```

### 3.2 ShaderMeta — 정규식 DSL의 후계

`.shader` 정규식 DSL은 은퇴하고 YAML로 간다. 근거: 엔진 직렬화 자산이 이미
YAML(`Meta::Serialize`)이라 파서를 하나 더 유지할 이유가 없고, 지금 DSL은
파싱 결과의 절반(`Keywords`·`tag`)이 어디에도 전달되지 않는 반쪽이다.

```yaml
# ForwardWater.shadermeta — 개념 예시(P2c 제품 자산의 정확한 복사본 아님)
schema: 1
name: ForwardWater
source: ForwardWater.hlsl          # 엔트리는 패스가 지정, 소스는 하나
properties:                        # Material 프로퍼티의 선언처 — cbuffer와 리플렉션으로 대조
  - { name: baseColor,  type: float4,    default: [1,1,1,1] }
  - { name: waveSpeed,  type: float,     default: 0.5 }
  - { name: baseMap,    type: texture2d }
keywords:                          # 퍼뮤테이션 축 — 선언한 것만 존재한다
  - { axis: SKINNING,   values: [off, on] }
  - { axis: ALPHA_TEST, values: [off, on] }
passes:
  - name: Forward
    vs: { entry: VSMain }
    ps: { entry: PSMain }
    state: { blend: alpha, cull: none, depthWrite: false, depthTest: lessEqual }
    queue: transparent
  - name: Shadow
    vs: { entry: ShadowVS }
    state: { blend: off, cull: front, depthWrite: true, depthTest: less }
    queue: shadow
```

P2c 제품 fixture는 기존 Forward 표준 texture 이름 `baseColorMap`·`normalMap`·`ormMap`·
`emissiveMap`과 `t4..t7`을 유지했다. P2d-c에서 Wind의 `t4` 이름을 `windMap`으로 바꾸고
Material/DataSystem/snapshot owner를 이름 기반 vector로 일반화해 위 `baseMap` 같은 generic
texture authoring 이름을 실제 제품 reflection/register 경로에서 허용한다. 현재 대표 pass의
물리 범위는 GBuffer `t0..t3`, Forward `t4..t7`, 모두 `space0`으로 유지한다.

- **프로퍼티 선언이 곧 Material의 스키마다.** 컴파일 후 리플렉션과 대조해
  선언·실제 cbuffer 불일치를 로드 시점에 잡는다(지금은 수동 동기화라 조용히
  틀린다 — `MaterialInfomation` 주석의 "HLSL cbuffer와 이름을 맞춰"가 그 표시).
- **`state` 블록의 어휘 = V6의 `RHIGraphicsPipelineDesc` 어휘.** 메타 파서가
  중립 desc를 직접 채우므로 어휘가 두 벌 생기지 않는다.
- 큐 분류는 패스의 `queue`에서 유도한다. `m_renderingMode`는 호환 필드로
  남기되 진실은 메타다.
- v1 property type은 `float|float2|float3|float4|int|bool|float4x4|texture2d`,
  state 어휘는 `fill(solid|wireframe)`·`cull(none|back|front)`·
  `blend(off|alpha|additive)`·`depthWrite`·`depthTest(off|less|lessEqual)`·
  `topology(triangle|line|point)`다. 알 수 없는 field는 오타로 보고 거부한다.
- source는 같은 meta 디렉터리 기준 상대 `.hlsl|.slang`만 허용하고 `..`·절대 경로를
  거부한다. 그래픽은 VS 필수, compute는 CS 단독 + `queue: compute`로 고정한다.

### 3.3 퍼뮤테이션 모델

- **키 = (metaGuid, passIndex, 키워드 비트셋).** 축은 메타가 선언한 것만
  존재하고, 집합은 정렬·정규화 뒤 해시한다.
- **M2A의 코드 기반 이행 키**는 메타가 아직 없는 엔진 패스를 위해 정렬된
  `(name, value)` 집합을 소유한다. 같은 집합은 삽입 순서와 무관하게 같은 128-bit
  key를 만들고, 중복 축·잘못된 식별자·빈 값은 요청 전에 거부한다. 이 키와
  충돌 대조용 정규화 entry 전부가 캐시 정체성에 들어간다.
- **에디터: 요청식 컴파일 + 캐시.** 처음 쓰는 조합만 컴파일하고 콘텐츠 해시
  캐시에 남긴다. **게임 빌드: 전수 사전 컴파일** — PHASE 12의 셰이더 스텝이
  이 모드를 부른다.
- **폭발 억제**: 축 선언 강제(자유 `#define` 주입 금지) + 메타당 퍼뮤테이션
  수를 로드 시점에 로깅 + 게임 빌드 전수 컴파일에 상한 게이트.
- **정본은 엔진의 `PermutationKey`다.** M2 첫 이관은 키워드 비트셋을 현재와
  동일한 전처리 define으로 내린다. Slang specialization은 캐시·코드크기·성능
  이득을 별도로 증명한 축에만 대체 표현으로 쓴다. specialization identity도
  같은 `PermutationKey`에 포함한다.
- 첫 이관 대상은 엔진의 손 퍼뮤테이션 4곳(1.2)이다. SKINNING이 축이 되면
  Shadow·GBuffer·WireFrame의 스키닝 이본이 데이터로 내려간다.

### 3.4 컴파일러 서비스 — V5의 실물

```cpp
enum class RHIShaderTarget : uint8_t { DXIL, SPIRV };

struct RHIShaderCompileRequest {
    std::filesystem::path source;      // 또는 메모리 소스(엔진 내장 셰이더 이행기)
    std::string entry;
    RHIShaderStage stage;
    RHIShaderTarget target;
    const RHIShaderPermutation* permutation;   // 정규화된 key + 소유 name/value
    bool debug;
};

struct RHIShaderBlob {
    std::vector<uint8_t> bytes;        // DXIL 또는 SPIR-V
};

class IRHIShaderCompiler {
public:
    virtual std::expected<RHIShaderBlob, RHIShaderError>
        Compile(const RHIShaderCompileRequest&) = 0;
    virtual std::expected<RHIShaderReflection, RHIShaderError>
        Reflect(const RHIShaderCompileRequest&) = 0;
};
```

- **목표 구현은 고정 버전 Slang 벤더링**(`ThirdParty/Slang/`)이다. 엔진은
  `slang-compiler.dll`을 직접 적재하고 compile session·module·entry point·target component를
  조립한다. Vulkan SDK·PATH·registry fallback은 Editor 편의 경로에도 두지 않아
  빌드 머신과 개발자 SDK 버전이 산출물을 바꾸지 못하게 한다.
- **캐시 키 = hash(소스 콘텐츠 + include closure 콘텐츠 + permutation key/entries + target +
  컴파일러 버전).** 파일명·타임스탬프가 아니라 콘텐츠다. 이것으로 `.hlsli`
  무차별 무효화가 은퇴하고, "두 번째 실행 컴파일 0건"이 퍼뮤테이션 세계에서도
  유지된다. include closure는 Slang이 실제로 열어 본 파일을 엔진 파일 시스템
  어댑터에서 수집한다. 별도 HLSL include 그래프 파서는 만들지 않는다.
- **리플렉션 정규화**: Slang program layout을 타깃별로 읽어 cbuffer
  레이아웃·리소스 종류·원래 register/space를 `RHIShaderReflection`으로
  정규화한다. Vulkan의 shift가 적용된 raw binding 번호와 DXIL 오프셋을 그대로
  비교하지 않고, 엔진의 논리 register/space 계약으로 되돌린 뒤 대조한다.
- **Slang reflection은 `.shadermeta`를 대체하지 않는다.** 셰이더에 없는
  기본값·라벨·큐·렌더 상태는 메타에 남고, reflection은 선언과 바인딩을
  검증하고 런타임 업로드 레이아웃을 생성한다. 이 통합은 M7에서 닫혔다.

### 3.5 파이프라인 기술 — V6의 실물

`RHIGraphicsPipelineDesc`(래스터·블렌드·뎁스·토폴로지·RT 포맷 — 전부 V1의
`RHIFormat`과 새 상태 열거로)를 세우고 `DX12PSOManager`를 그 구현으로 내린다.
PSO 요청 = (퍼뮤테이션 blob들, desc, RT 포맷). 메타의 `state` 블록과 엔진
패스의 코드 지정이 **같은 구조체를 채운다** — 재질 구동 패스는 데이터에서,
엔진 전용 패스(SSAO·포그 등)는 코드에서. 후자를 데이터로 강제하지 않는다 —
그 상태는 아티스트의 것이 아니라 알고리즘의 일부다.

### 3.6 Material 데이터 모델

- **ShaderMeta 참조(GUID) + 프로퍼티 값 맵 + 키워드 선택**이 전부다. 텍스처
  슬롯 5개 고정 필드는 프로퍼티 선언 기반으로 대체하고, 기존 필드·API는
  호환층으로 남겨 표준 텍스처 프로퍼티(baseColorMap·normalMap·ormMap·aoMap·
  emissiveMap)로 리매핑한다. `baseColor`는 이미 float4 값 이름이므로 texture GUID와
  공유하지 않는다.
- **직렬화 단일화**: `DataSystem` 경로 하나로. `ComponentFactory`의 중복 수동
  파서와 `ModelLoader`의 바이너리 필드는 그 경로 호출로 대체한다 — 필드 추가
  시 수정 지점이 한 곳이 된다.
- **핫리로드 핸들화**: 셰이더·PSO 식별을 map 엔트리 주소가 아니라 세대
  핸들로. 리로드 = 캐시 무효화 + 다음 사용 시 재요청. PHASE 2의 핸들 원칙과
  같다 — 주소 안정성 불변식(1.5)은 핸들이 서면 소멸한다.

---

## 4. 이행 — 슬라이스

원칙 둘. **슬라이스마다 소비자가 먼저 있다** — 추상을 세우는 슬라이스는 같은
슬라이스 안에서 기존 코드를 그 위로 옮겨 소비자를 확보한다(구 RHI의 사인 재발
방지). **판정은 수치다** — 공통 판정: 자가 검증 스윕 판정 착수 전과 동일 +
`pipeline.nodes` 구성/상태 판정 일치. 슬라이스별 기준은 각 항목에.

**M0 — 진실 확정과 죽은 표면 정리 (1일) — ✅ 완료 (`24e784ce`, 2026-08-11)**
계획보다 넓게 실행됐다: `ShaderSystem`·`ShaderPSO`·`VisualShaderPSO`·
`ShaderDSL`·`VisualShaderDSL`·`Shader.h`·`PSO`·`HLSLCompiler`·
`ShaderSelectionWindow` 소스 17파일 + 자산 136파일(`.hlsl` 113·`.hlsli` 11·
`.shader` 12), 약 14,800줄. 폐기 근거는 이 문서 §1.1과 같다(그림에 닿지
않는다) — 실측으로 확정한 소비자 0 목록이 커밋에 있다.
`.cso`·읽는 코드 0. `.meta` 고아 129는 손댈 것이 없다(§1.0).
Material 이음매는 남겼고, 그 이유는 내가 적은 것이 아니라 §0의 것이 맞다.

**M1 — 중립 컴파일러 서비스와 Slang 백엔드 (V5 승계)**

**M1A — 중립 서비스 기준선 (2.5일) — ✅ 완료 (2026-08-16).**
`cc12ba4f`의 `RHIShaderSource` 경계를 실제 서비스로 완성했다.
`IRHIShaderCompiler` 뒤의 DXC 구현 하나가 같은 요청에서 DXIL 또는 SPIR-V를 만들고,
패스·DX12/Vulkan 검사는 모두 `RHIShaderCompiler`만 호출한다. 구
`DX12ShaderCompiler`·`VulkanShaderCompiler` 네 파일과 `D3DCompile` 호출은 제거했다.
결과는 `RHIShaderBlob`, define은 `RHIShaderDefine`이라 컴파일 경계의 D3D 타입은 0이다.

콘텐츠 캐시 키는 compiler identity + output + profile + entry + defines + compile options +
루트 HLSL 및 재귀 include closure의 경로·내용을 포함한다. 메모리 캐시와
`%LOCALAPPDATA%/CreatorEngine/ShaderCache/v1` 디스크 캐시를 쓰며, 손상된 blob은
내용 해시로 거부한다. `dx12.selftest`는 메모리 표를 비운 재요청이 디스크 히트이고
컴파일 수가 늘지 않음을 단정한다. 실제 두 번째 프로세스는 **컴파일 0 · 디스크
히트 5**였다. `PostChainCommon.hlsli`만 바꾼 프로브에서는 캐시 파일이 정확히
의존 엔트리 수인 **6개만 증가**했고 `dx12.post` 픽셀 판정도 통과했다.

★ M1A의 DXC SM6 게이트는 HLSL **49개**(루트 38 + SelfTest 11), 실제 엔트리 67개를
런타임 define과 같은 값으로 컴파일해 **49/49 · 67/67 통과**했다. 파손이 20파일을
넘지 않았으므로 이중 FXC/DXC 기간은 두지 않는다. DXC 기본 fast math에서 IBL BRDF
LUT 극점이 A=0.413으로 변한 회귀는 판정값을 낮추지 않고 요청별 `strictMath`를
추가해 `IblBrdf` 한 곳에만 `-Gis`를 적용했다(A=1.000 복구). 일반 패스는 `-O3`다.

검증: VS18/v145 Debug x64 전체 솔루션·Player·Academy 링크, DX12 35종 스윕은
기준선과 같은 **통과 28 · 완료 4 · 실패 2 · 무판정 1**, `vk.selftest`는 검증 레이어
클린 통과. 비유니티 전수 컴파일은 새 `RHIShaderCompiler.cpp`까지 독립 컴파일된 뒤
기존 include 자급성 부채(`FrameCameraSnapshot.h` 등)에서 실패했다 — M1 신규 파일
오류는 아니다.

★ **M1A는 완료된 역사적 기준선이지 최종 컴파일러 선택이 아니다.**
공식 DXC 바이너리 벤더링을 따로 닫지 않고 M1B가 배포 소유권을 Slang
번들로 교체한다. M1B 전까지 현재 DXC 경로는 구현 현황으로 유지한다.
**M1B — Slang 백엔드 전환 (3일) — ✅ 구현·런타임 게이트 완료
(2026-08-24).**

- `IRHIShaderCompiler`·요청·패스 호출부를 유지하고 구현을 Slang 2026.14 API로
  교체했다. 제품 경로는 `.hlsl`에서 `DXIL | SPIR-V`를 내는 Slang 하나이며,
  엔진의 직접 `dxcapi`/`DxcCreateInstance`와 Vulkan SDK·PATH compiler fallback은 0이다.
  DXIL은 Slang이 downstream으로 호출하는 고정 DXC v1.9.2607을 사용한다.
- `ThirdParty/Slang`은 Slang 헤더·`slang-compiler.dll`·DXC의
  `dxcompiler.dll`/`dxil.dll`·양쪽 라이선스를 공식 archive digest와 함께 고정한다.
  authoring은 저장소 번들, 패키지는 실행 파일 옆 정확한 세 DLL만 허용한다.
  compiler identity는 build tag와 세 DLL의 전체 콘텐츠 hash다.
- column-major·`register(...)`·`__spirv__`·Vulkan
  `b/t/u/s = 0/100/200/300`·`-fvk-use-entrypoint-name`을 보존했다.
  Slang 2026.14 parser가 `VulkanBindShift`를 session/target 양쪽에 넣어 종류 코드가
  binding으로 굽히는 문제는 session의 shift 중복본만 제거해 해결했다. 산출
  decoration과 동일 버전 `slangc`를 대조했다. 최종 cache schema의 SPIR-V
  94개·binding decoration 354개에는 비정상 상위 binding(1000 이상)이 0개다.
  Vulkan 장치는 Slang SPIR-V가 선언하는 `shaderDrawParameters`를 선택·생성
  계약에 포함한다.
- `strictMath`는 `-fp-mode precise`, 일반 패스는 `-O3`, 모든 컴파일은
  `-warnings-as-errors all`이다. Slang이 지적한 Fog/Gizmo/SSGI/SSR 네 파일을
  수정했고 DXIL/SPIR-V 경고 예외는 0이다. DX12 `dx12.ibl`·`iblshade`와
  픽셀 스윕이 기존 기준선을 유지했다.
- 실제 Slang dependency 목록을 정렬·내용 hash해 기존 디스크 캐시 계약을
  유지했다. `FogCommon.hlsli` 변경 프로브는 의존한 엔트리만 **3개** 새로 만들었고,
  새 프로세스 `dx12.selftest`는 **컴파일 0 · 디스크 히트 5**였다.
- Debug x64 전체 솔루션은 경고/오류 0으로 링크됐다(Player·CreatorEditor 포함;
  과거 `Academy_4Q` 별도 프로젝트는 현재 솔루션에 없고 CreatorEditor로 수렴).
  DX12 35종은 기준선과 같은 **통과 28 · 완료 4 · 실패 2 · 무판정 1**,
  판정 줄 차이 0. `vk.selftest`와 DX12/Vulkan `render.livecheck`는 pipeline 19노드,
  두 view ready, validation 0, 미구현 0으로 통과했다.
- Vulkan SDK/DXC PATH를 제거한 `InputMode Workspace` 격리 stage에서 DX12·Vulkan
  패키지가 각각 HLSL source를 포함한 172 entries를 싣고 Player smoke를 통과했다. 세 compiler DLL도
  정확한 bundle hash로 stage됐다. **커밋된 스냅샷을 요구하는 `InputMode Tracked`
  clean-checkout 재현은 아직 실행하지 않았다** — 현재 변경을 커밋한 뒤 돌릴
  배포 재현 게이트이며, M1B 구현 완료를 B3 shader cook 완료로 확대하지 않는다.

**M2 — 퍼뮤테이션 프레임워크 (2.5일) — ✅ M2A·M2B 완료.**

**M2A — 코드 기반 퍼뮤테이션 정본 (1.5일) — ✅ 구현·런타임 게이트 완료
(2026-08-24).** `RHIShaderPermutation`이 name/value를 소유하고 이름순으로
정규화하며, 순서 독립 128-bit `RHIShaderPermutationKey`를 만든다. 컴파일 요청과
Slang `-D` 생성, 디스크 캐시가 이 객체만 소비한다. 키뿐 아니라 정규화 entry도
캐시에 넣어 hash 충돌이 곧 잘못된 재사용으로 이어지지 않게 했다. 중복·잘못된
축·빈/개행 값과 64축 초과는 요청 시 거부한다.

손 퍼뮤테이션은 4패스의 **6 define 그룹**이었다: Shadow `SHADOW_SKINNING`,
Forward 기본 상수 + `REFERENCE_PATH`, SSAO kernel + filter, SSGI trace. 전부
이관해 `RHIShaderDefine`·널 종료 배열·`CompileFile(..., nullptr, ...)` 계약은 0이다.
첫 이관은 Slang에도 같은 전처리 define를 내리며 specialization은 별도 성능
증명 뒤로 남긴다. 자가 검증은 삽입 순서 독립·값 구분·중복/잘못된 축 거부를
단정한다. Debug x64 RenderEngine·CreatorEditor·Player 빌드, `dx12.selftest`의
**컴파일 0 · 디스크 히트 5**, Shadow/Forward/SSAO/SSGI 표적 4종, DX12 35종
기준선 판정 차이 0, `vk.selftest`, Vulkan 240프레임 `render.livecheck`가 통과했다.

**M2B — 메타 기반 축·전수 열거·폭발 상한 (1일) — ✅ 구현·자가 검증 완료
(2026-08-24).** `ShaderPermutationDomain`이 M4의 `ShaderMeta`와 M2A의
`RHIShaderPermutation`을 직접 잇는다. 최종 lookup digest는
`metaGuid + passIndex + 축 이름순 선택(axis, value, ordinal)`으로 만들며,
hash 충돌을 잘못된 재사용으로 확대하지 않도록 GUID/pass/선택값 전체도 소유한다.
2~16값 축이므로 옛 계획의 literal keyword bitset은 **다중값 선택 vector**로
일반화했다. 메타의 축 작성 순서를 바꿔도 같은 선택은 같은 key/define을 만들고,
값 목록 순서를 바꿔 ordinal define가 달라지면 key도 달라진다.

`Resolve`는 Editor가 요청한 한 조합만 만들며 모든 축을 Slang 전처리 define
`AXIS=0..N-1`로 내린다. `EnumerateForBuild`는 같은 경계에서 pass-major·축 이름순
mixed-radix 전수 열거를 하고, 기본 **4,096 compile requests**를 넘으면 일부를
조용히 자르지 않고 실패한다. `DataSystem::LoadShaderMetaGUID`는 catalog GUID로
소유값을 읽고 variants/pass와 전체 request 수를 기록하며, 상한 초과는 Editor
로드 실패가 아니라 Build 경고로 남긴다. 별도 Shader 전역 registry/cache는
추가하지 않았다(M5 몫).

selftest fixture는 1 pass × `QUALITY={low,high}` = **2 requests**, ordinal define,
축 순서 독립, 값/pass 민감 key, cap=1 거부를 단정했다. Debug x64
RenderEngine·CreatorEditor·Player 빌드와 `dx12.selftest`가 exit 0·stderr 0,
PNG 생성, shader compile 0·disk hit 5로 통과했다. B3가 이 열거 결과를 소비해야
한다는 계약은 닫혔지만, 실제 DXIL/SPIR-V cook artifact·manifest·source-free Stage는
아직 B3 미구현이다. 이번 CPU identity/열거 슬라이스에서 DX12 35종·Vulkan·package
스윕은 다시 돌리지 않았다.

**M3 — 파이프라인 기술 중립화 (V6 승계, 3일) — ✅ 실행 코드 기준 완료.**
2026-08-24 재실측에서 패스의 생성 계약은 `RHIGraphicsPipelineDesc`·
`RHIComputePipelineDesc`·중립 샘플러/상태 어휘로 수렴했고, 패스의
`DX12GraphicsPipelineDesc`·`D3D12_` 직접 조립은 남지 않았다. `D3D12_` 문자열 1건은
`EnhancedSSGIPass.cpp:275`의 구 상태를 설명하는 역사 주석이며 실행 참조가 아니다.
이 슬라이스의 실행 코드 판정은 닫힌다. 재질 메타의 `state`가 같은 desc를
채우는 state 어휘·변환은 M4가 닫았다. 실제 Material/PSO 소비 배선은 M6의
몫이므로 M3·M4 완료를 그 배선 완료로 확대 해석하지 않는다.

**M4 — ShaderMeta 스키마 (3일 → 2일) — ✅ 구현·자가 검증 완료
(2026-08-24).**

✅ 이미 된 것(`cc12ba4f`): 패스 24곳 인라인 HLSL 45블록(3,792줄)이 파일로.
`Assets/Shaders/DefaultPassShader/`는 M4 시점 루트 38 + `SelfTest/` 11이었고,
M7 reflection fixture 추가 뒤 현재 `SelfTest/` 12다. 나는 "재질 구동 패스
둘만 꺼내고 나머지는 강제로 옮기지 않는다"고 적었는데 전부 옮겼고, 그쪽이
옳다 — 문자열이 막고 있던 것이 편집기·`#include`·컴파일 시점 셋이었고 그중
셋째가 Vulkan 전제와 직결된다(§0). 부수로 문자열 이어붙이기 셋이 진짜
`#include`가 됐다.
`ShaderMeta`/`ShaderMetaLoader`가 schema v1의 프로퍼티 선언·기본값·라벨,
키워드 축, 패스별 VS/PS/CS 엔트리, 중립 렌더 상태와 큐를 소유한다. 모든 map은
unknown field를 거부하고 이름/값 중복, 잘못된 stage/queue 조합, 타입 불일치,
nil GUID, 1MiB/개수 상한, source 절대·상위 경로와 source 부재를 로드 시점에
실패시킨다. state는 기존 RHI 열거를 저장하고 `RHIGraphicsPipelineDesc`에 직접
적용하므로 두 번째 상태 어휘가 없다.

별도 Shader 전역 registry는 만들지 않았다. 기존 `AssetMetaRegistry`가 해석한
sidecar GUID와 경로를 loader 입력으로 받고 `ShaderMeta`는 소유 값으로 반환한다.
M2B의 `DataSystem::LoadShaderMetaGUID`가 이 경계를 제품 load API로 올리고 조합 수를
기록하지만 owning value 계약은 그대로다.
핸들·세대 cache는 M5 몫이다. 에디터 asset database/presentation/content browser는
`.shadermeta`를 셰이더 자산으로 등록한다. `Assets/` 아래 selftest fixture는 기존
파일명 기반 UUID 규약을 쓰고, 전역 `*.meta` ignore에서 `.shadermeta.meta`만 좁게
추적 예외로 열어 clean checkout에도 GUID 정본이 남는다. catalog GUID→파일 load,
property 3·keyword 1·pass/state,
중복/unknown-field/path 탈출 거부를 `dx12.selftest`에서 검증했다. Debug x64
  RenderEngine·CreatorEditor·Player 빌드와 selftest(stderr 0, shader compile 0 ·
  disk hit 5)가 통과했다. 실제 HLSL 선언과 property/resource layout 대조는 뒤이은
  M7에서 닫혔으며 M4 단독 완료 증거로 소급하지 않는다.

**M5 — Material 재설계 (3일)**
프로퍼티 블록·키워드 선택·메타 참조·직렬화 단일화·핸들화(3.6, 3.7). 기존
`.asset` 로드 호환: 구 5필드 → 표준 프로퍼티 리매핑. 판정: 기존 씬·머테리얼
전부 로드 왕복(저장→로드→재저장 diff 0) + 회귀 세트 통과.

**M5-A — 소유 프로퍼티 블록과 기존 API 연결 — ✅ 구현·표적 자가 검증 완료
(2026-08-24).** `Material`의 저장 정본에 `m_shaderMetaGuid`, 이름 기반
`MaterialPropertyValue` vector, 다중값 keyword selection을 추가했다. 값 구조체와
vector는 C++20 `reflect()`만 사용해 typed YAML 경로로 왕복하며 새 매크로나 전역
registry는 없다. Slang reflection의 `ShaderMetaBindingLayout`은 저장하지 않고
`Material::RuntimeSchema`가 불변 소유 복사본으로 공유한다. 따라서 Material copy는
schema 수명에는 안전하고 property/CB 값은 서로 독립이다.

`ConfigureShaderProperties`가 ShaderMeta 기본값을 논리 값에 채우고 M7 offset으로
단일 CB byte view를 재생성한다. 기존 `TrySet/TryGet`과 C#의 `TrySetValue` 이음매는
이 layout을 실제 소비하며, property type·CB 이름·범위를 fail-closed로 검사한다.
texture GUID와 keyword axis/value API도 같은 정본을 갱신한다. 늘 널이던 raw
`m_cbMeta`와 소비자 0인 `MaterialParameters.h`는 제거했다.

Debug x64 RenderEngine·RenderTests·CreatorEditor·Player 빌드가 성공했다. 기존
`ShaderMetaFixture`를 사용한 `dx12.selftest`는 exit 0·stderr 0으로, GUID + property
3 + keyword 1, `MaterialProperties` 32B repack, 잘못된 type/name/value 거부,
저장→load→재저장 YAML diff 0, copy 독립을 단정했다. 공식 validation wrapper는
현재 로컬 vcpkg HEAD가 저장소 baseline과 달라 preflight에서 멈췄으므로, 동일한
`Invoke-Dx12Suite.ps1 -Only dx12.selftest` 실행 결과만 이 절편의 실행 증거다.
리플렉션 골든은 새 기본 키 3개만 기준선에 반영한 뒤 타입 77·직렬화 77·실패 0·
diff 0으로 통과했다.

★ **M5-A 잔여 부채 — packing 정본의 배치 오류 (2026-08-29 확인).** 이 절편이 만든
`ApplyDefault`·`ValidateLogicalValue`·`PackProperty`(+ `NumericElementCount`·`LogicalByteSize`·
`FindBinding`)는 `Material` 상태를 한 글자도 읽지 않는다 — **Material 클래스의 알고리즘이 아니라
ShaderMeta 계약의 알고리즘**이다. 그런데 `Material.cpp` 익명 namespace(internal linkage)에 두어
다른 TU 에서 이름조차 보이지 않는다. 결과로 (1) legacy 안에 같은 루프가 두 벌이 됐고
(`ConfigureShaderProperties` / `adc026b4`가 더한 `BuildShaderPropertyBlock`), (2) 두 번째 소비자인
`experiment::Material`이 이 정본에 붙을 방법이 없다.

이 정본은 I6에서 `Material.cpp`가 삭제된 뒤에도 살아남아야 하므로, 분리는 experiment 와 무관하게
필요하다. **선행 작업 `MaterialPropertyPacker` 분리**로 상환한다 — 자유 함수 6종을 ShaderMeta
계약 쪽으로 이동하고 legacy 두 호출부가 정본을 부른다(`MaterialInfo` 폴백은 legacy 호환 입력이므로
`Material.cpp`에 존치). 상세는 ModelImportPipelinePlan §I5-M "선행 작업".

**M5-B1 — standalone YAML codec + scene-owned runtime 복원 — ✅ 구현·표적 검증
완료 (2026-08-24).** `DataSystem::SerializeMaterialPayload` /
`DeserializeMaterialPayload`가 Editor 저장과 standalone `.asset` 로드의 typed YAML,
legacy `constant_buffers`, IOR·5 texture runtime 복원을 한 경계로 모았다. CB sequence는
이름순으로 기록해 unordered map 순서가 저장 diff에 새지 않으며 malformed/duplicate
entry는 거부한다. `LoadMaterial`은 파일 stem을 cache key와 material name의 정본으로
맞춘다.

씬 임베디드는 typed deserializer가 만든 renderer 소유 `shared_ptr<Material>`을 유지한
채 같은 `FinalizeMaterialRuntime`만 호출한다. 이전처럼 이름 기반 cache material에
scene snapshot을 다시 deserialize해 다른 renderer의 공유 상태를 바꾸지 않는다.
Debug x64 RenderEngine·SceneRuntime·CreatorEditor·Player가 성공했고, codec을 직접
통과하는 `dx12.selftest`는 `MaterialProperties` 32B legacy CB 복원과 DataSystem YAML
diff 0을 단정했다. reflection golden은 타입 77·직렬화 77·실패 0·diff 0이었다.
embedded material 8개가 있는 `FT_Primitives.creator`도 scene switch·11개 object 활성화·
렌더 캡처(942,079 bytes)까지 통과했다. PhysX GPU DLL 부재로 software fallback 경고는
있었으나 scene load/activate와 DX12 렌더 판정에는 실패가 없었다.

**M5-B2 — versioned model material payload + legacy texture GUID 이행 — ✅ 구현·
표적 검증 완료 (2026-08-24).** `ModelLoader`의 `MaterialInfomation` raw dump, enum/GUID
native layout, 5개 texture 문자열 수동 write를 제거했다. 새 writer는 M5-B1의
`DataSystem` YAML payload를 `CEMT` magic + little-endian version/encoding/length의 v1
record로 감싸며 payload는 4 MiB로 제한한다. loader는 첫 record를 probe해 v1은 같은
codec으로 복원하고, 기존 무버전 record는 bounds-checked read-only 호환 경로로 읽는다.
unknown version과 잘린 payload는 fail-closed다.

`FinalizeMaterialRuntime`은 legacy 5개 texture 이름을 catalog GUID로 해석해
`baseColorMap`·`normalMap`·`ormMap`·`aoMap`·`emissiveMap`에 채우고 이후 load는 GUID를
우선한다. 새 key는 숫자 `baseColor`와 타입 충돌하지 않는다. `ModelLoader`는 codec이
복원한 texture를 다시 `shared_ptr`로 보유해 `UnloadUnusedAssets` 뒤 raw pointer 수명도
기존 계약대로 유지한다.

Debug x64 RenderEngine·SceneRuntime·RenderTests·CreatorEditor·Player가 성공했다.
`dx12.selftest`는 YAML diff 0에 더해 binary v1 왕복, unknown/truncated 거부, legacy
texture GUID 5개 이행을 stderr 0으로 단정했다. 기존 무버전 primitive cache 8종은
`FT_Primitives.creator` scene switch·11개 object 활성화·DX12 렌더 캡처까지 통과했다.
`verify-asset-authoring-ownership.ps1`은 임시 GLB의 Editor import가 1,756-byte cache에
실제 `CEMT` marker를 썼고 재기동 cache load와 same-session generation reload가 모두
통과했음을 확인한 뒤 임시 자산을 정리했다. reflection golden도 타입 77·직렬화 77·
실패 0·diff 0이다.

**M5-C1 — 현재 수명·캐시 감사 — ✅ 소스 감사 완료 (2026-08-24).** 감사 시점의
`ShaderMeta`는 `DataSystem::LoadShaderMetaGUID`가 매번 소유값으로 읽었고 별도 cache나
generation은 없었다. 호출자는 자가 검증 둘뿐이었고, `Material`은
`ConfigureShaderProperties`에서 reflection layout과 keyword schema를 소유 복사하므로
메타 주소 자체에는 기대지 않았다. Slang binary cache는 source와 include content를
identity에 넣어 재요청만 들어오면 stale blob을 피한다.

`m_retiredAssetGenerations`의 실제 보존 대상은 Model·Material·Texture였으며 ShaderMeta는
들어 있지 않았다. watcher는 `Modified`를 버리고 authoring 변경 생산자는 Model·Texture만
`ContentReload`를 냈다. DX12의 `OnShaderReloaded`는 PSO `ComPtr` cache를 비우지만
resource-table handle slot은 해제하지 않았고 제품 호출자는 없었다. Vulkan pipeline
handle은 generation 0/app lifetime이었다. `FoliageType`의 raw `Mesh*`/`Material*`도
남아 있었다. 따라서 C를 C2 CPU ShaderMeta handle, C3 watcher·render-thread 재요청과
PSO handle 완결, C4 raw 소비자·retired generation 축소 순으로 나눴다.

**M5-C2 — DataSystem ShaderMeta generation handle — ✅ 구현·표적 검증 완료
(2026-08-24).** `ShaderMetaHandle {slot, generation}`과 DataSystem 소유 GUID→slot cache를
추가했다. 같은 GUID 반복 요청은 같은 immutable `shared_ptr<const ShaderMeta>` snapshot과
handle을 돌려주며, 게시된 `.shadermeta` `ContentReload`는 slot을 유지하고 generation을
전진시킨다. 이전 snapshot을 가진 호출자는 안전하게 마칠 수 있지만 이전 handle resolve는
즉시 실패한다. remove는 map에서 slot을 떼고 generation을 올린 뒤 재사용 목록으로 보낸다.
load·resolve·invalidate는 하나의 cache mutex 아래 직렬화해 현재 CPU 경계의 thread 계약을
명시했다.

`Material`은 적용한 handle을 runtime-only 값으로 기록하고 copy/move에서 보존한다.
디스크 정본과 reflection schema에는 여전히 GUID만 들어가므로 serialization ABI는 바뀌지
않는다. invalid handle로 schema를 적용하는 호출은 거부하고, 디스크/scene payload를 복원할
때는 이전 적용 handle과 runtime schema를 지워 새 세대 재구성을 강제한다.
`RuntimeAssetType::ShaderMeta`와 `.shadermeta` 분류를 `ApplyAssetChange`에 연결했으며, C3가
이 invalidation seam에 watcher와 render-thread next-use 재요청을 붙인다.

Debug x64 RenderEngine·RenderTests·CreatorEditor·Player 빌드와 `dx12.selftest`가 통과했다.
자가 검증은 동일 요청의 handle/snapshot 재사용, reload 뒤 stale handle 거부, 같은 slot의
generation 전진, 새 snapshot으로 Material 재구성을 단정했고 stderr는 0이었다. reflection
golden도 타입 77·직렬화 77·실패 0·diff 0이다. 다만 현재 작업트리의 별도 미완성
`Experiment/Model*.cpp` 두 파일은 자기 include 실패로 전체 빌드를 먼저 막았으므로, 이
두 파일만 MSBuild 검증 import에서 제외해 C2와 기존 제품 대상을 링크했다. 그 병행 변경까지
포함한 clean-tree 빌드 성공으로 확대 해석하지 않는다.

**M5-C3a — watcher `Modified` → GT CPU invalidation queue — ✅ 구현·표적 검증 완료
(2026-08-28).** Editor의 efsw callback이 버리던 `.shadermeta` `Modified`를
`DataSystem::QueueAssetChange`에 게시하고, 게임 스레드가 프레임 시작에만
`DrainQueuedAssetChanges`를 적용한다. 한 번의 저장에서 같은 경로·종류로 중복 발생한
알림은 마지막 한 건으로 합치므로 watcher 알림 조각 수만큼 generation이 뛰지 않는다.
큐에 넣기만 한 시점에는 현재 handle/snapshot이 그대로 유효하고, drain 뒤에만 C2 slot의
generation이 전진해 stale handle resolve가 실패한다. HLSL include dependency와 Model·Texture
수명 정책은 이 절편에 추측으로 넓히지 않았다.

VS18 MSBuild·v145·Debug x64에서 RenderEngine·RenderTests·CreatorEditor가 성공했다.
`dx12.selftest`는 중복 queue 2건→drain 1건, drain 전 같은 snapshot, drain 뒤 stale handle
거부와 같은 slot 새 generation 재구성을 실행 단정했고 exit 0·stderr 0이었다. 실제 외부
편집기의 저장 이벤트 timing은 아직 별도 live probe로 판정하지 않았으므로, 이 결과를
render-thread PSO reload 완료로 확대하지 않는다.

**M5-C3b1 — backend PSO cache handle generation — ✅ 구현·양 backend 표적 검증 완료
(2026-08-28).** `IRenderPipelineCache::InvalidatePipelines`를 render owner의 submission
경계 계약으로 추가했다. DX12는 cache entry를 resource table에서 release해 slot generation을
전진시키고, Vulkan은 pipeline slot/free-list에 generation을 두어 이전 handle resolve를
거부한다. 같은 desc를 next-use에 요청하면 두 backend 모두 새 generation handle을 게시한다.

무효화 시점에 GPU가 아직 참조할 수 있는 native pipeline은 즉시 파괴하지 않는다. C3b1
시점의 DX12 `ComPtr`와 Vulkan `VkPipeline`은 cache shutdown까지 retired 목록에 보존했다. 이는
use-after-free를 막는 안전한 과보존 절편이며 fence 완료값 기반의 bounded collection까지
완료됐다는 뜻은 아니다. DX12 회귀는 cache 3개 무효화, old handle 거부, 같은 desc의 새
generation 재생성을 단정했다. Vulkan 회귀는 command submit 직후 `WaitForGpu` 전에
무효화하여 old handle 거부·새 generation·retired 1개와 validation layer clean을 함께
단정했다. VS18 MSBuild·v145·Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player,
재링크 뒤 `dx12.psocache`·`dx12.selftest`·`vk.selftest`가 모두 통과했고 세 실행의
stderr는 0이었다.

이 절편은 제품 ShaderMeta watcher를 PSO cache에 일괄 연결하지 않았다. 현재 정적 pass
20여 곳은 PSO handle만 보유하고 재생성 desc/request record를 보유하지 않아, 전역 clear를
먼저 걸면 stale handle을 next-use에 복구하지 못한 채 draw가 조용히 빠진다. C3b2를
completion retirement(C3b2a)와 ShaderMeta/request record(C3b2b)로 나눠 진행한다.

**M5-C3b2a — completion-point PSO retirement — ✅ 구현·양 backend 표적 검증 완료
(2026-08-28).** `InvalidatePipelines`가 render owner가 마지막으로 제출한
`RHICompletionPoint`를 받으며, `CollectRetiredPipelines`가 완료값을 넘은 native PSO만
논블로킹 파괴한다. completion 0은 제출 완료를 증명하지 못한 quarantine으로 취급해
아무리 큰 completed 값으로도 회수하지 않고 device/cache shutdown drain에서만 파괴한다.
공통 `RHICompletionRetireQueue`를 재사용해 texture/mesh graveyard와 같은 완료 규약을 쓴다.

DX12 live runner는 매 프레임 asset-cache maintenance의 completed fence로 PSO queue도
회수하고, Vulkan은 `BeginFrame`에서 timeline 완료값으로 회수한다. DX12 `dx12.psocache`는
completion 6에서 3개 보존→7에서 3개 회수와 completion 0 quarantine 1개를 단정했다.
Vulkan `vk.selftest`는 실제 command submit 직후 무효화하고 `WaitForGpu` 뒤 1개 회수,
old handle 거부·새 generation·validation layer clean을 함께 단정했다. VS18 MSBuild·v145·
Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player가 성공했고 두 회귀의 stderr는
0이었다.

C3b2a는 제품 cache invalidation을 켜지 않았다. 현재 ShaderMeta의 production PSO 소비자는
0이고 정적 pass 20여 곳은 deep-owned bytecode/desc request record가 없기 때문이다.
C3b2b에서 먼저 owning request record와 targeted invalidation을 세우고, ShaderMeta generation을
frame packet으로 render owner에 전달해 한 수직 경로의 next-use 재생성을 닫는다.

**M5-C3b2b1 — deep-owned graphics request + targeted PSO invalidation — ✅ 구현·양 backend
표적 검증 완료 (2026-08-28).** `RHIGraphicsPipelineRequest`가 VS/PS bytecode, input element
배열, 각 input semantic 문자열을 함께 소유하고 move 뒤 `RHIGraphicsPipelineDesc`의 빌린
포인터를 다시 묶는다. `Replace`는 후보 PSO를 먼저 `GetOrCreate`한 뒤 성공한 경우에만 옛
handle 하나를 `IRenderPipelineCache::InvalidatePipeline`으로 retire한다. 후보와 옛 handle이
같으면 무효화하지 않으며, 옛 handle이 이미 global invalidation으로 stale이어도 유효한 후보로
요청을 복구한다. compile 실패가 현재 draw PSO를 먼저 끊는 순서는 허용하지 않는다.

같은 desc의 cache handle은 여러 draw/material이 공유할 수 있으므로 이 request를 material마다
복제하지 않는다. `InvalidatePipeline`은 같은 handle의 CPU holder를 모두 stale로 만들며
completion point는 GPU 수명만 보호한다. 따라서 C3b2b2의 RenderThread owner가 desc/key별
unique request record와 그 handle을 참조하는 packet 집합을 소유하고, 한 프레임 경계에서
전부 새 handle로 교체한다고 증명할 때만 targeted invalidation을 호출한다.

DX12 `dx12.psocache`는 bytecode 포인터의 deep copy, targeted stale 1개와 나머지 cache handle
보존, completion 6 보존→7 회수 1개를 단정했다. 이어 전체 invalidation 3개, 옛 desc의 같은
slot 새 generation, completion 8 보존→9 회수 3개와 completion 0 shutdown quarantine 1개도
그대로 통과했다. Vulkan `vk.selftest`는 실제 submit 뒤 새 cull variant를 먼저 생성하고 옛
handle만 retire한 뒤, bytecode/input/semantic deep copy, 옛 desc의 새 generation, `WaitForGpu`
뒤 targeted 1개 회수와 idle 경계 global 2개 회수를 단정했으며 validation layer가 clean이었다.
VS18 MSBuild·v145·Debug x64 RenderEngine·RenderTests·CreatorEditor·Player 빌드와
`dx12.psocache`·`dx12.selftest`·`vk.selftest`가 exit 0·stderr 0으로 통과했다. Player의 PhysX
PDB 누락과 `/DELAYLOAD:vulkan-1.dll` LNK4229는 기존 경고이며 새 컴파일/링크 오류는 없다.

이 절편은 **그래픽 요청의 안전한 소유·교체 primitive**까지만 닫았다. compute owning request는
실제 장기 보관 소비자가 생길 때 추가한다. C3b2b1 시점에는 ShaderMeta generation을 frame
packet에 싣거나 Material/pass 제품 owner가 `RHIGraphicsPipelineRequest`를 보유하지 않았고,
production invalidation caller도 0이었다. 이어지는 C3b2b2가 대표 GBuffer 경로에서 이 경계를
닫는다.

**M5-C3b2b2 — ShaderMeta frame packet → GBuffer unique render request — ✅ 구현·양 backend
제품/표적 검증 완료 (2026-08-28).** `GBuffer.shadermeta`를 제품 자산으로 추가하고, GT가
프레임 시작의 `DrainQueuedAssetChanges` 뒤 exact path로 GUID/handle과 immutable
`ShaderMeta` snapshot을 해석해 `EnhancedLiveFramePacket::gbufferShaderMeta`에 싣는다.
RenderThread는 `DataSystem`이나 파일을 다시 읽지 않고 packet snapshot만 소비한다.
`EnhancedGBufferPass` 하나가 `RHIGraphicsPipelineRequest`와 적용한 `ShaderMetaHandle`을
유일하게 소유하므로 공유 cache handle의 다른 CPU holder를 남기지 않는다.

같은 generation은 no-op이고 새 generation은 pass 이름 `GBuffer`, opaque VS+PS와 compile
target을 먼저 검증·컴파일한다. 성공한 후보 PSO를 먼저 만든 뒤 직전 제출의 completion point로
옛 handle 하나만 targeted retire하고, 그 다음에만 request와 적용 generation을 교체한다.
첫 제품 generation을 만들지 못하면 draw를 fail-closed로 막지만, 이후 잘못된 generation은
거부하고 현재 PSO/generation을 보존한다. DX12는 DXIL, Vulkan은 SPIR-V를 같은 packet 계약에서
선택하며 compiler output도 호출 범위에 한정한다.

VS18 MSBuild·v145·Debug x64에서 RenderEngine·RenderTests·CreatorEditor·Player가 성공했다.
최종 `vk.gbuffer`는 DX12/Vulkan 양쪽에서 ShaderMeta 1→2, old handle stale, new handle next draw,
잘못된 generation 3 거부/current generation 2 보존을 단정했다. center diffuse/normal/bitmask/depth,
coverage 2304/2304와 최대 채널 편차 0도 유지했고 Vulkan validation·미구현은 0건이었다.
실제 Editor watcher probe는 자산의 `depthTest`를 `less`→`lessEqual`로 저장했을 때
`handle 1:1 → 1:2`, apply 1→2, targeted replace 0→1, failure 0을 같은 프로세스에서 확인한 뒤
원본을 복구했으며 exit 0·stderr 0이었다. `dx12.psocache`·`dx12.selftest`·`vk.selftest`도
최종 코드에서 다시 exit 0·stderr 0으로 통과했다.

이 완료 범위는 **대표 pass의 source/entry/render-state generation 전환**이다. 제품 Material의
property/keyword/texture/CB를 item별 PSO와 draw에 전달하지 않았고, live watcher probe의 빈 씬은
draw 0이므로 Material 시각 배선 증거로 세지 않는다. HLSL include dependency 추적과
GBuffer/Forward 전체의 item별 Material 소비도 각각 후속 계약이다. 아래 M6-P0가 숫자 CB의
격리 관통을, P1a가 제품 GBuffer 숫자 property packet/batch를, P1b2a가 ShaderMeta texture
GUID/register 배선을 닫았지만 include dependency, multi-meta/permutation·Forward 경계는 남는다.

**M5-C4 — raw Mesh/Material 소비 제거 + retired generation 축소 — ✅ 구현·전체
게이트 완료 (2026-08-28).** `FoliageType`의 runtime `Mesh*`/`Material*`를
`shared_ptr`로 바꾸고 Model cache의 소유 로드와 `GetMeshShared`/`GetMaterialShared`를
연결했다. Foliage proxy/packet 복사는 이제 실제 자산 세대를 소유한다. 추가 수명 감사에서
Editor Undo/Redo의 `LoadModelToSceneObjCommand`가 장기 `Model*`를 보관하던 경로도 찾아
`shared_ptr<Model>`로 바꿨다. 새 `LoadCachedModelShared`가 소유 경계이고, 오탈자 legacy
`LoadCashedModel`은 즉시 호출 호환 wrapper로만 남아 외부 호출자는 0이다.

disk reflection은 `modelName`과 shadow 필드만 계속 저장하고 runtime Mesh/Material owner는
직렬화하지 않는다. selftest는 synthetic owner와 Foliage copy가 원본 owner reset 뒤에도
Mesh/Material을 살리고 마지막 owner 해제 뒤 소멸함을 단정했다. Meta YAML 왕복 뒤 runtime
owner가 비어 있음도 함께 검사했다. 실제 Material cache generation을 Foliage가 소유한 채
`ContentReload`하면 cache entry는 즉시 분리되지만 이전 Material은 Foliage가 있는 동안만
살고 마지막 consumer 해제 뒤 만료된다.

`DataSystem::RequiresLegacyRetiredGeneration`은 Model·Material에는 false,
Texture·UITexture·SpriteSheet에는 true를 반환한다. 따라서 Model/Material 이전 세대는 실제
외부 shared consumer만 보존하며 전역 retired 목록에 쌓이지 않는다. 반면
`TerrainLayer::diffuseTexture`와 UI/Sprite 계열 raw texture alias는 아직 실소비 중이므로
texture 계열의 `m_retiredTextureGenerations`는 의도적으로 유지했다. 이후 M6-P1b1이 제품
Material/GBuffer의 texture 세대 소유만 닫았으며, 그 전역 보존을 제거할 근거로 확대하지 않는다.

VS18 MSBuild·v145·Debug x64에서 RenderEngine·SceneRuntime·RenderTests·CreatorEditor·Player가
성공했다. 최종 `dx12.selftest`·`vk.selftest`·`dx12.psocache`·`vk.gbuffer`는 모두 exit 0·
stderr 0이었고 Vulkan validation·미구현은 0건이었다. DX12 35종 전수는 모든 stderr 0과
기준선 그대로 **통과 28 · 완료 4 · 실패 2 · 무판정 1**이었다. pipeline composition은
nodes 19와 Editor pass 상태를 단정했고, reflection golden은 타입 77·직렬화 77·실패 0·
diff 0, asset authoring ownership은 model envelope v2·material payload v1·runtime reload·
terrain/foliage/blackboard transaction·collision/tag/input/animator를 모두 통과했다.

**M5-D — 표준 PBR property 이름 정본 — ✅ 구현·표적 검증 완료 (2026-08-27).**
`StandardMaterialProperty.h`가 숫자 7개와 texture 5개의 논리 이름을 소유하고 중복을
컴파일 타임에 거부한다. M5-B2의 `DataSystem` texture GUID 이행과 ModelImport의
`SceneToModelDraft` 기본 매핑이 이 정본을 함께 소비한다. `_BaseColorFactor`·
`_MetallicRoughnessMap`처럼 M5에 존재하지 않던 기본 별칭은 더 이상 게시하지 않는다.
이 슬라이스는 이름만 단일화하며 property 타입·offset·register의 정본은 계속
ShaderMeta reflection이다. Debug x64 `RenderEngine`·`RenderTests`·`CreatorEditor` 빌드가
성공했다. 실물 `ImporterModelDecoder`를 탄 `experiment.gltf`(Gunner, 재질 2개)와
`experiment.fbx`(Ani_Mon, 재질 6개)는 표준 숫자 property 6개·underscore 별칭 0을
단정했고, 두 검사 모두 구조/보간/탄젠트/Material 계약 실패 0으로 통과했다.

**M5 — ✅ 완료 (2026-08-28).** A/B1/B2가 소유 property와 단일 codec을, C2/C3가
ShaderMeta·PSO generation/reload를, C4가 장기 Model/Mesh/Material raw 소비와 전역 retired
의존을, D가 표준 property 이름을 닫았다. Texture 계열 raw alias와 제한된 retired 보존은
별도 수명 작업으로 명시적으로 남겼다. item별 Material property/texture/CB/PSO draw 소비는
M5 완료 범위가 아니라 M6가 소유한다.

**M6 — 소비 배선 — 머테리얼이 고른 셰이더로 실제 드로우 (4일) — ✅ 완료
(2026-08-29).**
`copyQueue`의 축약 복사를 (PSO 핸들 + 바인딩 + 프로퍼티 CB) 전달로 확장하고,
GBuffer/Forward가 아이템별 PSO로 그린다(PSO 키 정렬로 배칭 유지). M0에서
폐기된 `.shader` 파일을 복구하지 않고, 물·바람 대표 재질을 새
`.shadermeta` + HLSL 계약으로 작성해 픽셀 판정한다.
★ **최대 위험 슬라이스다 — "배선만 이으면 그림이 더 나빠진다"**(Forward+
전례: 소비자 없는 출력을 이었더니 화면이 퇴보했다). 검증 씬과 셰이더
프로브(출력 단색 치환으로 원인 층 가르기)를 먼저 세우고 배선한다. 완료 시
Material의 값 사슬이 처음으로 GPU에 도달한다 — `ApplyShaderParams` 구 사슬은
여기서 은퇴.

**착수 게이트와 순서:** 선행 `M5-C3 → M5-C4`는 완료됐다. M6 안에서는 먼저 표준 PBR
`.shadermeta` + 단색 프로브로 `Material → layout → CB → PSO` 한 재질을 수직 관통하고(P0),
제품 GBuffer 숫자 property packet/batch를 닫은 뒤(P1a), texture generation owner(P1b1),
ShaderMeta texture GUID/register(P1b2a), material keyword permutation PSO(P1b2b1),
  multi ShaderMeta generation PSO(P1b2b2),
  Forward owner packet(P2a, 완료) → ShaderMeta/PSO(P2b) → 물·바람(P2c) → 전체 재질(P2d)로
  넓힌다. ModelImport의 V2·V3와 D5-a fail-closed cooked ID 게시 계약은 완료됐다.
  D5-b1의 sidecar/manifest 계약, D5-b2a 단일 model producer, D5-b2b1 model 전수 Cook과
  D5-b2b2 제품 pak 공급은 완료됐지만 D5-b2c 나머지 producer까지 닫히기 전에는
  I5 직접 소비로 합류하지 않는다.

**M6-P0 — Standard Material 숫자 b2 단색 프로브 — ✅ 구현·전체 게이트 완료
(2026-08-28).** 실제 catalog GUID의 `StandardMaterialProbe.shadermeta`와 HLSL을 추가하고,
표준 숫자 property 7개를 `Material`에 설정한 뒤 DXIL/SPIR-V reflection에서 같은
`MaterialProperties b2/space0/48B`와 field offset을 단정했다. `EnhancedGBufferPass`에는
선택적 material constant bytes를 받는 pixel `b2` root parameter를 추가했다. 프로브는
실제 `Material → ShaderMeta binding layout → 48B CB → 교체된 PSO → 5 MRT`를 양 backend에서
같은 픽셀로 관통한다. 중첩된 `.shadermeta`의 source가 shader root가 아니라 meta 파일
위치 기준임을 보존하도록 immutable snapshot에 runtime-only `originPath`도 넣었다.

VS18 MSBuild/v145 Debug x64에서 RenderTests·CreatorEditor·Player를 빌드했고,
`vk.gbuffer`는 DX12/Vulkan 모두 coverage 2304/2304·최대 채널 편차 0·Vulkan validation 0으로
통과했다. `dx12.selftest`·`vk.selftest`, pipeline composition 19 nodes, reflection 77종/diff 0,
asset authoring 회귀도 통과했다. DX12 35종은 기존 기준선과 같은 통과 28·완료 4·실패 2·
무판정 1이며 전 항목 stderr 0을 유지했다.

P0의 완료는 **격리된 한 재질의 숫자 CB 프로브**까지만 뜻했으며, 제품 배선 증거는 아래
P1a에서 별도로 닫았다. 그러므로 P0 자체를 M6 전체 완료로 확대 해석하지 않는다.

**M6-P1a — immutable per-material draw packet + 제품 GBuffer 숫자 property batch —
✅ 구현·전체 게이트 완료 (2026-08-28).** `BuildDrawPool` 동안만 `shared_ptr<const Material>`을
임시 소유하고, 활성 제품 GBuffer ShaderMeta가 적용된 뒤 generation handle·복사된
`ShaderMetaBindingLayout`·keyword 선택·property bytes·네 texture 참조를
`EnhancedMaterialDrawSnapshot`으로 밀봉한다. 최종 `EnhancedDrawItem`에는 `Material*`가 없으며,
`Material::BuildShaderPropertyBlock`은 논리 property를 const 경로로 reflection layout에 pack한다.

`EnhancedGBufferPass::ApplyShaderMeta`는 후보 VS/PS reflection에서 property layout을 만들고
`b2/space0` 계약과 PSO 교체가 모두 성공한 뒤에만 현재 generation/layout을 설치한다.
`PrepareFrame`은 다른 generation·다른 ShaderMeta·keyword 선택을 fail-closed로 거부하며 기존
PSO를 보존한다. material batch key는 ShaderMeta handle·keyword·property bytes·texture 전체를
비교하고, 같은 mesh/texture라도 property bytes가 다르면 batch를 나눠 각 batch의 48B `b2`를
업로드한다.

제품 `GBuffer.shadermeta`와 HLSL은 `baseColor`·`metallic`·`roughness`·`normalScale`·
`occlusionStrength`·`emissive`·`alphaCutoff`의 표준 숫자 7개를 소비한다. `vk.gbuffer`는 같은
mesh/texture에 property만 다른 두 draw를 넣어 draw/mesh/material/batch `2/1/2/2`, 양 backend
coverage `3072/3072`, 좌·우 픽셀 최대 채널 편차 0을 확인했다. 잘못된 material generation은
다음 prepare에서 거부되고 현재 PSO/meta는 보존됐다.

VS18 MSBuild/v145 Debug x64의 RenderEngine·SceneRuntime·RenderTests·CreatorEditor·Player 빌드,
`vk.gbuffer`, `dx12.gbuffer`·`dx12.skinning`·양 backend selftest, pipeline composition 19 nodes,
reflection 77종/diff 0, asset authoring 회귀가 통과했다. DX12 35종은 기존 기준선과 같은 통과
28·완료 4·실패 2·무판정 1이며 전 항목 stderr 0이다.

**P1a에서 의도적으로 남겼던 경계:** 당시 texture 네 칸은 raw alias였고 제품 GBuffer는
하나의 활성 ShaderMeta와 빈 keyword만 받았다. 아래 P1b1은 texture CPU generation 수명을,
P1b2a는 ShaderMeta texture GUID/register binding을, P1b2b1은 같은 active ShaderMeta 안의
material keyword permutation PSO를, P1b2b2는 material별 복수 ShaderMeta generation PSO를
닫았다. Forward 및 전체 재질 전환은 아직 완료가 아니다. 그러므로 M6 전체와 페이즈 완료 공수는
완료로 세지 않는다.

**M6-P1b1 — Material/draw packet texture generation ownership — ✅ 구현·전체 게이트 완료
(2026-08-28).** `Material`에 baseColor·normal/bump·ORM·AO·emissive 다섯 texture의
`shared_ptr` owner를 두었다. 당시 public raw view/setter는 P2d-e 전까지 호환 경계로 유지했고,
제품 sealing은 owner가 없는 raw 조합을 fail-closed로 거부했다. P2d-e에서 호출자 재측정 뒤
그 raw 표면을 제거했다. `DataSystem::FinalizeMaterialRuntime`은 저장 정본인 property GUID를 먼저 해석해
`LoadSharedMaterialTexture` generation을 설치하고, legacy name은 GUID가 없을 때의 fallback이다.
Assimp `ModelLoader`와 MeshRenderer Inspector의 texture 선택/삭제도 같은 shared setter를 탄다.

`EnhancedMaterialDrawSnapshot`은 GBuffer가 쓰는 baseColor·normal·ORM·emissive 네 owner를
복사해 GPU cache upload/record가 끝날 때까지 CPU texture generation을 직접 살린다. GBuffer
batch key와 binding은 그 owner의 raw view만 순간적으로 사용한다. synthetic 1×1 baseColor
texture를 두 Material이 공유하는 fixture에서 Material/local owner를 먼저 해제해도 두 draw
packet이 generation을 유지했고, packet 해제 뒤 `weak_ptr`가 만료됐다. 같은 owned texture와
다른 property의 두 draw는 양 backend draw/mesh/material/batch `2/1/2/2`, coverage
`3072/3072`, 첫/둘째 픽셀 최대 편차 `0`, Vulkan validation·texture failure `0`으로 통과했다.

VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player 빌드와
`vk.gbuffer`, `dx12.gbuffer`·`dx12.skinning`·양 backend selftest, pipeline composition 19 nodes,
reflection 77종/diff 0, asset authoring 회귀가 통과했다. DX12 35종은 M6-P1 기준선과 항목별
차이 0인 통과 28·완료 4·실패 2·무판정 1이며 전 항목 stderr 0이다.

이 완료는 `m_retiredTextureGenerations` 전체 제거가 아니다. Terrain/UI/Sprite 등 다른 raw
texture 소비자가 남아 전역 보존은 유지한다.

**M6-P1b2a — ShaderMeta texture GUID/register binding — ✅ 구현·전체 게이트 완료
(2026-08-28).** 제품 `GBuffer.shadermeta`를 숫자 7개와 `baseColorMap`·`normalMap`·`ormMap`·
`emissiveMap` texture 4개로 확장하고 HLSL resource 이름도 같은 정본으로 맞췄다. VS/PS
reflection은 이 네 property가 순서대로 `t0..t3/space0`임을 만들며, GBuffer 후보는 정확히 이
layout일 때만 PSO/current meta를 교체한다. draw snapshot은 각 texture의 property 이름·논리
GUID·register/space·generation owner를 밀봉하고, prepare 시 활성 reflection layout과 완전히
같은지 다시 검사한다.

`vk.gbuffer`는 `baseColorMap` 한 property에 서로 다른 non-nil GUID와 서로 다른 owned texture를
넣어 draw/mesh/material/batch `2/1/2/2`, coverage `3072/3072`, 양 backend 첫/둘째 픽셀 최대
편차 `0`, Vulkan validation·texture failure `0`을 통과했다. snapshot의 `baseColorMap` register를
`t0`에서 `t1`로 변조한 음성 대조는 prepare에서 거부됐고 현재 pipeline/meta는 보존됐다.
Material/local owner 해제 뒤 packet 유지와 packet 해제 뒤 반환도 그대로 통과했다.

VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player 빌드와
reflection 77종/diff 0, asset authoring, pipeline composition 19 nodes, Vulkan selftest가
통과했다. DX12 35종은 P1b1 기준선과 항목별 차이 0인 통과 28·완료 4·실패 2·무판정 1이며
전 항목 stderr 0이다. nil texture GUID의 legacy/default fallback은 허용하며, 실제
  `experiment::Material`의 실제 catalog/manifest asset/shader identity와 draw 소비는
  Serialization D5-b와 I5에서 별도로 닫는다.

**M6-P1b2b1 — material keyword permutation PSO — ✅ 구현·전체 게이트 완료
(2026-08-28).** 제품 `GBuffer.shadermeta`에 `SHADING_QUALITY=[full,reduced]` 축을 추가했다.
`full=0`은 기존 출력 정본이고 `reduced=1`은 normal-map 결과를 vertex normal 쪽으로 완화한다.
draw snapshot은 authored selection과 정규화된 `ShaderMetaPermutation::key`를 함께 밀봉한다.
GBuffer는 default variant를 candidate-first로 교체하고 추가 variant를 generation+permutation
key로 준비한 뒤, pass 전역 PSO가 아니라 material batch 직전에 해당 handle을 건다. reflection
layout은 각 define 조합에서 다시 해석해 b2/texture 계약이 같을 때만 variant를 게시한다.

`vk.gbuffer`는 첫 두 draw의 서로 다른 baseColor texture/GUID 계약을 보존하면서, 둘째와 셋째
draw는 texture·property를 완전히 공유하고 `full/reduced` selection만 다르게 했다. 양 backend
draw/mesh/material/batch `3/1/3/3`, coverage `3072/3072`, 첫/둘째/셋째 최대 채널 편차 `0`,
Vulkan validation·texture failure `0`으로 통과했고 normal 픽셀이 두 permutation에서 분리됐다.
변조 permutation key는 prepare에서 거부되어 현재 default/alternate PSO가 보존됐으며, 다음
ShaderMeta generation 적용 뒤 옛 alternate handle은 targeted retire되고 variant 수는 2→1이 됐다.

VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐다.
reflection 77종/diff 0, asset authoring, pipeline composition 19 nodes, Vulkan selftest가 통과했고,
DX12 35종은 P1b2a 기준선과 판정 차이 0인 통과 28·완료 4·실패 2·무판정 1, 전 항목 stderr
0이다.

**M6-P1b2b2 — multi ShaderMeta generation PSO — ✅ 구현·전체 게이트 완료
(2026-08-28).** GT는 기본 `GBuffer.shadermeta`와 `DataSystem::SnapshotMaterials()`가 참조하는
ShaderMeta GUID 집합을 현재 handle+`shared_ptr<const ShaderMeta>`로 frame packet에 밀봉한다.
RT sealing은 material GUID로 그 집합만 조회하며 DataSystem/file을 다시 읽지 않는다. GBuffer는
primary default request를 frame root-layout 정본으로 유지하고, 같은 pipeline-layout 계약인
secondary meta+permutation만 candidate-first로 variant map에 게시한다. primary 교체는 같은 catalog
slot의 옛 variant만, frame commit은 이번 packet에서 빠진 meta key만 retire한다. 서로 다른 meta key가
같은 PSO cache handle을 공유하면 마지막 holder가 사라질 때까지 invalidation을 미룬다.

`vk.gbuffer`는 기존 동일-meta `SHADING_QUALITY=full/reduced`와 texture/GUID 계약을 보존한 채 네 번째
draw에 별도 ShaderMeta handle/state를 사용했다. 양 backend draw/mesh/material/batch `4/1/4/4`, coverage
`3072/3072`, 네 픽셀 영역 최대 편차 `0`, Vulkan validation·texture failure `0`으로 통과했다. primary
reload가 secondary를 보존하고, secondary invalid candidate가 current를 보존하며, secondary generation
교체와 다음 frame 제외가 각각 정확한 key만 retire함을 확인했다. primary/secondary meta 원본 owner를
놓은 뒤 frame packet이 두 generation을 유지하고 packet 해제 뒤 반환하는 수명 게이트도 통과했다.

VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐다. reflection
77종/diff 0, pipeline composition 19 nodes, asset authoring, Vulkan selftest가 통과했다. DX12 35종 최종
스윕은 P1b2b1 기준선과 판정 줄 차이 0인 통과 28·완료 4·실패 2·무판정 1, 전 항목 stderr 0이다.

**M6-P2 — Forward + 전체 제품 재질 전환 — ✅ 완료 (2026-08-29).** 최대 위험 범위를 다음처럼
빌드 가능한 조각으로 고정한다.

1. **P2a Forward immutable value/texture-owner packet — ✅ 구현·표적 게이트 완료
   (2026-08-29).** `BuildDrawPool`이 잠시 소유한 transparent `Material`에서 baseColor,
   metallic, roughness, normal 사용 값과 baseColor/normal/ORM/emissive texture generation을
   `EnhancedForwardMaterialDrawSnapshot`으로 복사한다. 고정 Forward 계약의 논리 property,
   GUID, `t4..t7/space0`, `shared_ptr<Texture>` owner를 함께 밀봉한 뒤 Material owner를 놓는다.
   `EnhancedForwardPass`는 packet이 있으면 `EnhancedDrawItem`의 legacy factor/raw texture를
   읽지 않으며 property/register 순서가 다르면 mesh/texture upload 전에 fail-closed한다.
2. **P2b Forward ShaderMeta + material별 PSO/binding — ✅ 구현·표적/회귀 게이트 완료
   (2026-08-29).** `Forward.shadermeta`가 Standard 숫자 7개·texture 4개·
   `SHADING_QUALITY`와 transparent render state를 소유한다. GT frame packet은 GBuffer와 별도인
   Forward generation/value를 소유하고, `SealForwardMaterials`가 reflection-packed `b2/48B`,
   `t4..t7/space0`, keyword/permutation과 texture owner를 draw packet에 밀봉한다. pass는 각
   `(ShaderMeta generation, permutation, keyword selections)`마다 일반/Reference owning PSO pair를
   둘 다 후보 생성한 뒤 한 번에 게시한다. `CaptureFromView`의 back-to-front 배열은 재정렬하지
   않고 같은 mesh/material/pair가 바로 이어질 때만 instancing한다. reload candidate가 실패하면
   마지막 accepted ShaderMeta value owner로 material block을 계속 밀봉하고, half-pair 실패에서
   cache handle을 invalidate하지 않아 다른 owner를 stale시키지 않은 채 retry가 같은 entry를
   재사용한다. snapshot/legacy 선택은 `ShadeInstance.materialFlags`의 명시적 bit로 운반해 합법적인
   음수 `alphaCutoff`와 충돌하지 않으며, legacy per-instance 값도 유지한다.
3. **P2c 물·바람 대표 재질 — ✅ 구현·표적/회귀 게이트 완료 (2026-08-29).** 새
   `ForwardWater`·`ForwardWind` `.shadermeta + HLSL`과 Material asset을 추가했다. 실제
   `m_shaderMetaGuid`가 각 generation을 선택하며, Standard 숫자 7개의 48B prefix 뒤
   Water의 `waveSpeed`·`waveAmplitude`·`waveFrequency`·`waterTint` 또는 Wind의
   `windSpeed`·`windStrength`·`windFrequency`·`windTint` 네 float를 붙인 64B `b2`를
   reflection layout으로 pack한다. 표준 texture 이름과 `t4..t7/space0`, material별
   일반/Reference PSO pair, 기존 back-to-front 인접-only batch는 그대로 유지한다.

   canonical Water/Wind seed는 Scene/proxy가 소유하는 non-cache 대표 Material도 frame에서
   generation을 resolve하도록 catalog의 두 대표 Meta를 항상 싣던 임시 bridge였다. RT는 catalog나
   `DataSystem`을 다시 읽지 않았고, 이 고정 seed 목록은 P2d-d의 arbitrary required-asset packet으로
   일반화한 뒤 제거됐다. `windTint` 변경은 다음 frame packet에서 다시 밀봉돼 양 backend
   픽셀에 반영되고 invalid meta/generation/register는 마지막 accepted variant를 보존한 채
   fail-closed한다.
4. **P2d 전체 제품 재질/legacy 정리 — ✅ 완료 (2026-08-29).** 남은 material 소비자를 모두 새 packet/PSO
   계약으로 옮긴다.
   - **P2d-a Foliage 소비 — ✅ 완료 (2026-08-29).** `FoliageRenderProxy`가 type별 mesh/material
     `shared_ptr`, instance world matrix와 transformed AABB를 `DrawSource`로 밀봉한다. 제품
     `BuildDrawPool`이 이를 opaque/transparent draw로 펼치며, view별 `CaptureFromView`가 AABB를
     절두체로 판정한다. 단일 카메라에서 만든 `m_isCulled`는 multi-view 제품 판정에 쓰지 않는다.
   - **P2d-b dynamic flow — ✅ 완료 (2026-08-29).** `Material::m_flowInfo`의 wind vector/UV
     scroll과 `BuildLiveFramePacket`의 total/delta seconds를 32B
     `EnhancedForwardMaterialFlowSnapshot`으로 복사한다. 이는 authored ShaderMeta `b2`와 섞지
     않고 128B Forward `ShadeInstance`에 실리며, Water/Wind가 UV scroll·wind phase·frame time을
     소비한다. 값은 batch key에도 포함하고 NaN/Inf는 snapshot에서 fail-closed한다.
   - **P2d-c generic texture — ✅ 완료 (2026-08-29).** `Material`의 Standard별 고정 owner
     5개를 `(propertyName, shared_ptr<Texture>)` runtime vector로 통합하고 DataSystem이 임의
     texture GUID도 복원한다. GBuffer/Forward snapshot은 ShaderMeta authoring 순서의 가변
     owner vector를 보존하고 pass는 이름이 아니라 reflection register로 기존 4-slot 물리 범위에
     투영한다. Wind fixture의 `baseColorMap@t4`를 `windMap@t4`로 바꾸고 실제 1x1 owner/GUID를
     원본 해제 뒤 양 backend가 소비하며 packet 해제 뒤 반환되는 데까지 닫았다.
   - **P2d-d required assets — ✅ 완료 (2026-08-29).** Editor/Player Host가 활성 Scene의
     MeshRenderer/Foliage Material `shared_ptr`를 snapshot하고 rendering mode에 따라 GBuffer/Forward
     ShaderMeta GUID를 `EnhancedRequiredAssetPacket`에 선언한다. packet은 nil/중복을 제거하고
     `(domain, GUID)` 순으로 정규화되며 `BuildLiveFramePacket`이 generation/value owner로 resolve한다.
     cache 로드 전 Water/Wind 임의 GUID 2개가 양 backend frame에 들어오는 것을 판정했고 제품 코드의
     `ForwardWater.shadermeta`/`ForwardWind.shadermeta` 고정 seed 목록은 제거했다.
    - **P2d-e legacy 호출자 재측정/은퇴 — ✅ 완료 (2026-08-29).** 제품
      `BuildLiveFramePacket`의 `SnapshotMaterials()` 전수 스캔 2곳을 제거해 Host required packet만
      secondary ShaderMeta generation을 선언하게 했다. 제품 `BuildDrawPool`의 legacy material
      field 중복 쓰기도 0건으로 만들었다. Material의 public raw texture alias 5개와 외부 호출자 0인
      raw `Texture*` setter 6개를 제거하고 Editor/DataSystem/selftest를 owning getter로 옮겼다.
      `m_dirtyCBs`는 읽는 곳 0이라 제거했다. 실행 코드의 `ApplyShaderParams`는 0건이다.

      남긴 경계도 분리했다. `SnapshotMaterials()`의 실제 호출자 2곳은 Editor asset 목록/번들
      표시라 frame generation 소유와 무관하다. `TrySetValue`는 C# `Mesh_SetMaterialFloat/Int`가
      실제 호출하므로 유지한다. snapshot 없는 `EnhancedDrawItem` 값은 ShaderMeta/Material/cache를
      읽지 않는 격리 geometry selftest 입력으로만 남고 제품 draw pool writer는 0이다.

P2a의 `vk.forward`는 같은 mesh를 좌/우 두 transparent draw로 그리되 legacy 값은 같고
owning packet의 서로 다른 1x1 baseColor texture만 다르게 했다. 원 texture owner를 렌더 전에
놓아도 DX12/Vulkan 양쪽에서 두 영역이 분리되고, 변조 `t4` packet은 fail-closed하며, draw packet
해제 뒤 두 owner가 반환되어야 통과한다. 실측은 coverage `3072/3072`, 양 backend 좌/우 최대
채널 편차 `0`, 두 material 분리 `0.26724`, outside `0`, Vulkan validation·미구현 `0`이다.
`vk.forward`·`vk.gbuffer`·`vk.selftest`, DX12
`forward`·`forwardshade`·`forwardscale`가 stderr 0으로 통과/완료했다. VS18 MSBuild/v145 Debug
x64의 RenderEngine·RenderTests·CreatorEditor·Player도 빌드됐다. DX12 35종 전수 스윕은 P1b2b2
기준선과 판정 줄 차이 0인 통과 28·완료 4·실패 2·무판정 1이며 전 항목 stderr 0이다.
P2a는 수명/입력 선행 조각이며
그 자체로 Forward ShaderMeta/PSO나 M6 완료 증거가 아니었다. P2b의 새 `vk.forward`는 alpha
primary A와 additive secondary B를 겹치는 A/B/A 순서로 넣고, 6 draw가 전역 PSO 정렬 없이
인접 3 batch로 남는지 픽셀로 판정한다. DX12/Vulkan overlap RGB는
`0.25/0.50/0.50`으로 기대식과 일치했고 backend 최대 편차 `0`, coverage `1134/1134`,
outside `0`, ShaderMeta owner 2개·material packet 3개·variant 2개, invalid meta/generation/register
fail-closed, Vulkan validation·미구현 `0`이 통과했다. 두 번째 PSO 생성 실패 주입은 shared cache
invalidation `0`, 재시도 성공, 임시 variant frame retirement를 판정했고, 제품 `alphaCutoff=-1`과
legacy 비백색 tint도 각각 snapshot/instance 경로를 유지했다. `vk.gbuffer`·`vk.selftest`,
DX12 `forward`·`forwardshade`도 재통과했고 `forwardscale`는 완료했다. VS18 MSBuild/v145 Debug
x64의 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐다. 이 시점에는 물·바람과 전체
제품 재질/legacy 정리가 P2c/P2d에 남았으므로 P2b만으로 M6 완료를 선언하지 않았다.

P2c의 `vk.forward`는 primary/water/wind PSO A/B/A/C의 7 draw를 입력 순서 그대로 인접 4
batch로 유지하고 frame ShaderMeta owner 3개와 48/64B material packet 5개를 판정했다.
DX12/Vulkan overlap RGB는 각각 `0.125/0.25/0.5`, wind G는 `0→0.325`, coverage는
`1134/1134`로 같았다. backend 최대 편차와 기대식 편차는 `0`, custom은 `0.00005`, outside는
`0`, Vulkan validation·미구현은 `0`이다. VS18 MSBuild/v145 Debug x64의 RenderEngine·
RenderTests·CreatorEditor가 빌드됐고, `vk.gbuffer`·`vk.selftest`, DX12 `forward`·`forwardshade`가
재통과했으며 `forwardscale`는 완료했다. pipeline composition도 19 nodes를 유지했다. 이
완료는 일반 transparent Material 제품 경로의 대표 Water/Wind bridge까지다.

P2d-a의 `vk.forward`는 synthetic `FoliageRenderProxy`의 type 1개와 instance 3개를 입력으로
유효 type의 draw source 2개만 생성하고, 한 instance의 기존 `m_isCulled=true`도 view별 AABB
판정 입력으로 보존하는지 검사했다. 원 proxy/type Material owner 해제 뒤 draw source가 수명을
유지하고 source 해제 뒤 반환되는 것도 통과했다. 기존 Water/Wind 7 draw/4 batch/3 meta와
coverage `1134/1134`, wind G `0→0.325`, backend 편차·validation·미구현 `0`을 유지했다.
VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐고,
`vk.gbuffer`·`vk.selftest`, DX12 `forward`·`forwardshade`가 재통과했다.

P2d-b의 `vk.forward`는 producer frame delta `0.125→0.25`와 누적 total 증가를 먼저 판정하고,
`m_flowInfo` 32B+frame total/delta를 immutable draw snapshot과 128B Forward instance로 밀봉했다.
Wind의 authored property와 flow phase/time을 다음 packet에서 함께 바꾼 결과 DX12/Vulkan G는
각각 `0→0.2598`, overlap RGB는 `0.125/0.25/0.5`, coverage는 `1134/1134`였다. backend 최대
편차와 기대식 편차는 `0`, custom 편차는 `0.00023`, outside·Vulkan validation·미구현은 `0`이며
NaN time snapshot도 fail-closed했다. VS18 MSBuild/v145 Debug x64의 RenderEngine·RenderTests·
CreatorEditor·Player가 빌드됐고, `vk.gbuffer`·`vk.selftest`, DX12 `forward`·`forwardshade`가
재통과했다.

P2d-c의 `vk.forward`는 Wind ShaderMeta/HLSL의 첫 texture를 `windMap@t4`로 authoring하고,
generic 1x1 texture GUID/owner를 Material vector→immutable Forward packet으로 밀봉했다. 원
Material/owner 해제 뒤 DX12/Vulkan 모두 같은 픽셀을 유지하고 packet 해제 뒤 owner가 반환됐다.
기존 draw/batch/meta `7/4/3`, overlap RGB `0.125/0.25/0.5`, Wind G `0→0.2598`, coverage
`1134/1134`, backend·기대식 편차 `0`, custom `0.00023`, validation·미구현 `0`을 유지했다.
`vk.selftest`는 legacy texture 5개와 generic `baseMap`까지 owner vector 6개를 GUID로 복원하고
copy 공동 소유를 판정했다. `vk.gbuffer`, DX12 `forward`·`forwardshade`도 재통과했고 VS18
MSBuild/v145 Debug x64 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐다.

P2d-d의 `vk.forward`는 material cache 로드 전에 순서가 뒤섞인 Water/Wind GUID와 중복·nil 입력을
required packet에 넣고 canonical 2개로 정규화되는지, 두 generation owner가 frame에 실리는지
판정했다. 제품 코드의 고정 seed 이름은 0건이며 draw/batch/meta `7/4/3`, overlap RGB
`0.125/0.25/0.5`, Wind G `0→0.2598`, coverage `1134/1134`, backend·기대식 편차 `0`,
custom `0.00023`, validation·미구현 `0`을 유지했다. `vk.gbuffer`·`vk.selftest`, DX12
`forward`·`forwardshade`가 재통과했고 VS18 MSBuild/v145 Debug x64 RenderEngine·SceneRuntime·
RenderTests·CreatorEditor·Player가 빌드됐다.

P2d-e의 `vk.forward`는 Water/Wind Material이 cache에 들어온 뒤에도 빈 required packet에서
GBuffer/Forward ShaderMeta가 각각 primary 1개만 남는지 음성 판정했다. 명시 packet에서는 정확히
Forward primary+Water+Wind 3개이며 cache의 다른 재질은 frame 수명으로 새지 않는다. raw Material
texture alias/setter·`m_dirtyCBs`·`ApplyShaderParams`는 실행 코드 0건이고 제품 draw pool의 legacy
field writer도 0건이다. draw/batch/meta `7/4/3`, overlap RGB `0.125/0.25/0.5`, Wind G
`0→0.2598`, coverage `1134/1134`, backend·기대식 편차 `0`, custom `0.00023`, Vulkan
validation·미구현 `0`을 유지했다. `vk.gbuffer`·`vk.selftest`, DX12 `forward`·`forwardshade`가
같은 실행에서 통과했고 VS18 MSBuild/v145 Debug x64 RenderEngine·SceneRuntime·RenderTests·
CreatorEditor·Player가 빌드됐다.

### M6 이후 PHASE 4 인계 — Standard PBR native Slang 재작성

PBR 수식 개선과 HLSL→Slang 전환을 M6의 첫 소비 배선에 섞지 않는다. M6는 현재 출력과
수명 계약을 유지한 채 `Material → draw item → PSO/binding`을 실제 제품 경로에서 닫고,
PHASE 4가 그 위에서 **출력 동등 전환 → 의미 교정 → 품질 확장** 순으로 셰이더를 바꾼다.
단, 실제 Slang source/module/import를 읽는 컴파일러·에디터·패키징 기반은 소비자를 만들지
않는 격리된 `SRP-2` fixture로 M6와 병렬 선행할 수 있다. 자동 바인딩·`ParameterBlock`과
제품 PBR 진입점 전환은 M6 뒤다.

```text
PBR-S0 현재 DX12/Vulkan 기준선
    ├─ M5(완료) → M6 실제 Material 소비
    └─ PBR-S1 / SRP-2 native Slang source 기반(시각 변화 0)
                       ↓ 둘 모두 완료
PBR-S2 공용 PBR 모듈로 동등 이관 → S3 glTF 의미 교정 → S4 에너지·IBL
    → S5 그림자 → S6 display/post → S7 확장 lobe → S8 Shader Graph codegen
```

| 슬라이스 | 범위 | 완료 게이트 |
|---|---|---|
| PBR-S0 | DX12 현재 설정을 정본으로 고정하고 같은 scene/frame packet·카메라·해상도·tuning의 pre-tone 선형 HDR, final LDR, 표준 material grid와 pass별 timing을 DX12/Vulkan 별도 프로세스에서 캡처 | Vulkan 기본값 복원이 아니라 DX12 설정 재생. `SRP-G0`가 만든 이미지·차영상·RenderGraph stats artifact를 그대로 공유하고 별도 기준선 하네스를 만들지 않으며, pass fixture/live frame은 별도 판정 |
| PBR-S1 | `RHIShaderCompileRequest`의 HLSL/Slang 언어를 명시하고 cache identity에 포함. stable module search root·import dependency와 `.slang` Editor/Asset/packaging 분류를 추가 | native `.slang` 한 fixture가 DXIL/SPIR-V·reflection 동등 통과. 기존 HLSL 전수 개명과 시각 변경 0 |
| PBR-S2 | `MaterialInputs → StandardSurface` 평가와 GGX/IBL/light/shadow 공용 함수를 authored Slang module로 분리해 GBuffer·Deferred·Forward가 같은 구현을 소비 | 현행 출력을 의도적으로 보존한 DX12/Vulkan golden 통과. 중복 BRDF/재질 평가 제거 |
| PBR-S3 | `D2/D5-b → I5/V4` 뒤 glTF metallic-roughness 의미와 M5-D property를 실제 GPU 소비까지 닫음 | metallic factor는 texture 값에 곱하고 ORM AO·`normalScale`·`occlusionStrength`·emissive·`alphaCutoff`뿐 아니라 `doubleSided`·`emissiveStrength`와 texture UV set/transform/wrap이 import→Material→양 경로에 손실 없이 도달 |
| PBR-S4 | CreatorEngine의 height-correlated Smith·split-sum IBL을 유지하면서 multi-scatter energy compensation, specular AO, local reflection probe를 추가 | furnace/material-grid golden과 Deferred/Forward 허용 오차 통과 |
| PBR-S5 | shadow sampling module 단일화, normal-offset bias, cascade blend/far fade, 3/4 cascade 설정, point/spot atlas와 Low/Medium/High 품질 단계 | Low는 현행 PCF 비용을 보존하고 PCSS는 High에서만 선택. Vulkan timing 회귀 상한 포함 |
| PBR-S6 | `Linear HDR → Bloom → Exposure → Grading → Tone map → Display OETF → AA/UI` 계약과 `RGBA8Unorm` 출력 변환을 명시 | ACES 기준 golden의 backend 동등성 후 canonical AgX·auto exposure·bloom을 각각 독립 A/B |
| PBR-S7 | specular/IOR → clearcoat → transmission/volume → sheen → anisotropy/iridescence/dispersion 순으로 수직 확장 | 기본 MR은 Deferred 유지. GBuffer를 즉시 늘리지 않고 추가 lobe는 우선 Forward+; RT의 VNDF·exact Fresnel·Beer attenuation은 DXR reference slice로 분리 |
| PBR-S8 | 검증된 authored Slang module 계약을 Visual Shader Graph typed IR/codegen의 target으로 사용 | `.shadergraph` save→reload와 generated/authored Slang fixture가 같은 reflection·픽셀 게이트 통과 |

2026-08-27 정적 재감사 기준으로 먼저 닫을 의미 부채는 GBuffer/Forward의 metallic factor
가산, GBuffer에 기록되지만 Deferred 조명에서 소비되지 않는 ORM AO, 표준 property 중
실제 셰이더까지 도달하지 않는 normal/occlusion/emissive/alpha 값, PostChain의 명시적
display OETF 계약이다. 구현 착수 때는 각 항목을 소스와 golden으로 다시 확인한다.

**M7 — Slang reflection과 DXIL/SPIR-V 타깃 동등성 (2일) — ✅ 구현·양 백엔드
자가 검증 완료 (2026-08-24).**

`IRHIShaderCompiler::Reflect`/`RHIShaderCompiler::ReflectFile`이 컴파일 캐시와 분리된
같은 Slang linked program에서 타깃 program layout을 읽는다. `RHIShaderReflection`은
소유 값으로 stage, resource kind, 논리 register/space, 배열 수, cbuffer byte size,
field scalar·row/column·offset/size를 보존한다. `slang.h` 편의 메서드가 import library
의존을 만들지 않도록 필요한 reflection C export도 기존 `slang-compiler.dll` 동적
로딩 경계에서 명시적으로 해석한다. `spirv-reflect`나 새 전역 registry는 추가하지 않았다.

Vulkan의 t/u/s 100/200/300 shift는 추출 경계에서 HLSL 논리 register로 되돌린다.
상수 버퍼의 실제 GPU offset도 DXIL과 같게 하기 위해 SPIR-V 타깃에
`-fvk-use-dx-layout`을 고정했고, 옛 SPIR-V 디스크 캐시가 섞이지 않도록 compiler cache
계약을 v11로 올렸다. `ShaderMetaReflection::Resolve`는 M4 property 선언을 실제 stage
reflection과 대조해 하나의 material cbuffer와 texture binding을 소유
`ShaderMetaBindingLayout`으로 만든다. property type 불일치, stage 간 binding 불일치,
선언 부재와 둘 이상의 material cbuffer는 fail-closed다.

catalog GUID로 읽는 `ShaderMetaFixture.shadermeta`의 `QUALITY=high` 조합을 VS/PS 각각
DXIL·SPIR-V로 반사했다. `dx12.selftest`와 `vk.selftest` 모두 2 stages 동등,
`MaterialProperties b2/space0/32B`, `tint@0+16`, `roughness@16+4`, `albedoMap@t3`와
음성 대조를 단정했다. DX12 PNG와 Vulkan PNG가 생성됐고 Vulkan 검증 레이어는
클린이었다. Debug x64의 RenderEngine·RenderTests·CreatorEditor·Player가 빌드됐고,
240-frame warmup의 DX12 35종 전수 판정도 기준선과 같은 **통과 28 · 완료 4 ·
실패 2 · 무판정 1**이었다. M7 완료 자체는 M6 소비 증거가 아니며, 이후 M6-P1a가 실제
draw item의 숫자 property CB와 제품 GBuffer batch를 별도 판정했고 M6-P1b1이 texture
generation owner를, M6-P1b2a가 ShaderMeta GUID/register texture binding을, M6-P1b2b1이
material keyword permutation PSO를, M6-P1b2b2가 multi-meta generation PSO를 닫았다.
Forward의 owning value/texture packet은 M6-P2a가 닫았고 P2b가 별도 frame generation owner,
reflection `b2/t4..t7`, material별 일반/Reference PSO pair와 인접-only transparent batching을 닫았다.

합계 **23일**(M0 1 · M1A 2.5 · M1B 3 · M2A 1.5 · M2B 1 · M3 3 · M4 2 ·
M5 3 · M6 4 · M7 2). M0·M1A·M1B·M2A·M2B·M3·M4·M5·M6·M7의 실행 코드 기준
**23일 완료, 남은 추정 0일**이다. PBR-S0~S8은 PHASE 4의 후속 구현 후보라 이 23일 합계에
넣지 않으며 공수는 4-6에서 확정한다.
M2B·M3·M4·M5·M7 선행은 모두 닫혔다. native Slang source/module/import 기반만 격리된
`SRP-2` fixture로 병렬 선행할 수 있고, 제품 PBR 모듈 전환·specialization·
`ParameterBlock`은 M6 완료 후 별도 트랙이다.

★ **판정 문구 정정**: "자가 검증 33종"은 낡았다 — 기준선이 35종으로 갱신됐고
(`a253a22d`), 셰이더 작업 두 커밋은 **32종 통과**로 판정했다. 각 슬라이스는
착수 시점의 스윕 결과를 기준선으로 삼고 그 수를 노트에 적는다.

★ **판정 줄의 의미가 넓어진 것을 이어받는다**(`cc12ba4f`): 소스가 파일이 되면서
"셰이더 컴파일 통과"가 "파일을 찾았고 컴파일된다"가 됐다 — 배포에서 셰이더가
빠지면 여기서 잡힌다. M7의 SPIR-V 산출도 같은 성질을 갖는다.

---

## 5. 완료 기준

1. **컴파일 진입점 하나** — ✅ `RHIShaderCompiler` + 고정
   `ThirdParty/Slang` 하나다. `D3DCompile`·FXC·구 DX12/Vulkan 컴파일러·엔진의
   직접 DXC loader·Vulkan SDK/PATH fallback·런타임 컴파일러 선택 분기는 0이다.
2. **손 퍼뮤테이션 0건** — `RHIShaderDefine` 배열이 패스 코드에 없다
   (`D3D_SHADER_MACRO`는 이미 0).
3. **패스 17종 실행 코드에 `D3D12_` 직접 참조 0건** (V6 몫 —
   역사 설명 주석 1건은 제외) + 재질 구동 패스의 렌더스테이트가
   메타 데이터에 있다.
4. **머테리얼 커스텀 셰이더가 화면에 나온다.**
    - ✅ P2c 대표 Water/Wind Material GUID 선택, 양 backend 픽셀 판정과 다음-frame
      `windTint` 변경 반영.
    - ✅ P2d-a `FoliageRenderProxy` type/instance owning draw source와 view별 AABB culling 입력.
    - ✅ P2d-b `m_flowInfo`+frame total/delta immutable snapshot과 양 backend dynamic flow 픽셀.
    - ✅ P2d-c generic texture schema/owner vector와 `windMap@t4` 양 backend owner 수명·픽셀.
    - ✅ P2d-d arbitrary required-asset packet과 canonical Water/Wind seed 제거.
    - ✅ P2d-e 전체 legacy 호출자 재측정 및 구 사슬 은퇴 판정.
5. **같은 메타·같은 소스에서 Slang DXIL과 SPIR-V가 나온다** —
   `VkShaderModule` 로드, DX12 PSO 생성, 두 타깃의 중립 `RHIShaderReflection`
   바인딩·cbuffer 레이아웃 일치 단정, `.shadermeta` 선언 대조를 모두 통과.
6. **두 번째 실행 컴파일 0건** + `.hlsli` 수정 시 영향받은 셰이더만 재컴파일
   (전체 `.cso` 삭제 경로 은퇴).
7. **자가 검증 스윕 판정 착수 전과 동일** + `pipeline.nodes` 구성/상태 일치 +
   회귀 세트(Tools/regression) 통과.
8. **직렬화 경로 하나** — 머테리얼 필드 추가 시 수정 지점이 한 곳.

## 6. 리스크

- **DXC→Slang 런타임 동등성** — 52/52 프런트엔드·16/16 대표 산출은
  착수 근거였고, Slang이 진단한 4파일 경고·실제 양 backend 파이프라인·픽셀·
  package를 M1B에서 닫았다. 이후 버전 변경은 같은 전체 게이트를 다시 요구한다.
- **Slang ABI·버전 표류** — 설치된 Vulkan SDK 버전을 자동 소비하지 않고
  헤더·DLL·라이선스·DXIL 지원 구성을 하나의 고정 번들로 배치한다. 버전이
  바뀌면 캐시 identity와 DX12/Vulkan 전체 게이트를 다시 통과한다.
- **바인딩 재설계 혼입** — M1B/M7에서는 명시적 register·현재 Vulkan shift를
  보존한다. `ParameterBlock`·자동 바인딩을 같이 넣은 패치는 타깃 동등성을
  증명할 대조군을 없애므로 수용하지 않는다.
- **정밀 부동소수점 드리프트** — DXC `-Gis`와 Slang `precise`를 문구로
  동일시하지 않는다. IBL BRDF 극점·픽셀 판정이 모두 같아야 이관 완료다.
- **M6 배선** — 소비자 없는 출력을 이었을 때 그림이 나빠진 전례가 실측으로
  있다. 프로브·검증 씬 선행이 그 대응이고, 픽셀 판정 없이는 배선을 완료로
  치지 않는다.
- **퍼뮤테이션 폭발** — 축 선언 강제 + 수 로깅 + 빌드 상한 게이트(3.3).
- **핫리로드 불변식 해제 실수** — M5-C4가 Model/Mesh/Material 장기 소비를 소유 참조로
  바꾼 뒤에만 해당 전역 보존을 제거했다. Texture·UITexture·SpriteSheet는 raw alias가
  남아 있으므로 제한된 `m_retiredTextureGenerations` 보존을 계속 유지한다.

## 7. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| RhiBoundaryPlan §7.2 | **V5·V6를 이 페이즈가 승계한다** — M1=V5, M3=V6. V8(Vulkan 백엔드)은 M1A의 SPIR-V 산출을 이미 소비한다. 2026-08-16 DXC 기준선은 M1A의 완료 기록으로 남겨 두고, 2026-08-24 최종 컴파일러 계획은 M1B Slang 전환·M7 Slang reflection을 따른다. M3의 RHI-neutral desc 실행 코드 기준은 완료됐지만 메타 소비 배선은 M4·M6 몫으로 남는다 |
| BuildPipelinePlan (PHASE 12) | 게임 빌드의 셰이더 스텝 = 이 파이프라인의 전수 사전 컴파일 모드(3.3) |
| PHASE 10 파티클 · PHASE 11 지형 | 이 시스템 위에 선다 — 그쪽에서 셰이더 경로를 따로 만들지 않는다 |
| AssetResidencyPlan | 텍스처 상주는 그쪽 몫, 여기는 프로퍼티가 참조만 든다 |
| SceneGraphRedesignPlan | 직렬화 단일화(M5)가 트랙 P의 프리팹 왕복 회귀를 그대로 판정에 쓴다 |
| ModelImportPipelinePlan (PHASE 4) | I5-0에서 표준 PBR property 이름을 공유했고 모델 V2(68B)·V3(mesh별 packed layout), 머테리얼 M5와 M6의 기존 렌더 계약까지 완료됐다. Serialization D2/D5-a·D5-b1·D5-b2a·D5-b2b1·D5-b2b2로 model 전수 Cook과 AssetPacker/pak 게시까지 완료됐고, D5-b2c가 나머지 producer를 공급한 뒤 I5-M이 `experiment::Material` 정본·instance/resolver를 그 계약에 직접 연결한다. parity 뒤 I6가 legacy `::Model`/`::Material`과 Assimp를 함께 퇴역시킨다. V4의 레이아웃 퍼뮤테이션 축도 같은 ShaderMeta 키 체계를 쓴다 |
| ScriptableRenderPipelinePlan (PHASE 4) | M6 뒤 PBR-S0~S8을 소유한다. native Slang 기반은 SRP-2가 격리 선행할 수 있지만, 공용 Standard PBR 소비·자동 바인딩·Shader Graph codegen은 M6 실제 소비 계약을 우회하지 않는다 |
