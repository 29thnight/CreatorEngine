# 머테리얼 · 셰이더 파이프라인 재설계 (PHASE 3.5)

2026-08-11. Vulkan 골격(vk.selftest)이 선 직후 작성. RhiBoundaryPlan §7.2가 다음
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
| `Material::TrySetValue` 계열 | 살아 있음 | 게임 스크립트 8곳 + C# 인터롭이 호출. `m_cbMeta`가 늘 널이라 전부 false를 돌려준다 |

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
넓혔으면 게임 스크립트 프로젝트가 깨졌다. `MaterialParameters.h`에 중립
타입이 새로 서서 옛 타입이 끌고 오던 DX11 의존도 끊겼다.

★ **소스 파일화가 V5보다 먼저 와야 했다는 것을 놓쳤다.** 이 문서는 M1(컴파일러)
→ M4(애셋화) 순으로 잡았는데 `cc12ba4f`가 반대로 갔고 그쪽이 옳다. 문자열은
런타임 컴파일이 전제인데 Vulkan은 OS가 주는 컴파일러가 없어서, 문자열인 채로
V5를 하면 **Vulkan에서 성립하지 않는 계약을 중립화하게 된다.**

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

- **직렬화 3계열 공존**: `.asset` YAML(`DataSystem.cpp:433-521`) · 씬 임베디드
  수동 파싱(`ComponentFactory.cpp:110-207`) · 모델 바이너리(`ModelLoader.cpp:375,488,715`).
  필드 하나 추가에 세 곳을 고친다.
- **텍스처 슬롯 5개 고정 필드** — 이름 문자열+포인터 쌍이 클래스에 박혀 있다
  (`Material.h:119-135`). 임의 슬롯 추가는 클래스 수정이다.
- **핫리로드가 map 엔트리 주소 안정성에 의존**: `ShaderPSO`가
  `unordered_map` 엔트리를 raw pointer로 들고 있어 개별 `erase`를 의도적으로
  막아 놨다 (`PSO.h:31-49`, `ShaderSystem.cpp:521-529`).
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
# ForwardWater.shadermeta — 예시
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
  호환층으로 남겨 표준 프로퍼티(baseColor·normal·orm·ao·emissive)로 리매핑한다.
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
`dx12.live status` 패스 이름 목록 문자 일치. 슬라이스별 기준은 각 항목에.

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

**M6 — 소비 배선 — 머테리얼이 고른 셰이더로 실제 드로우 (4일)**
`copyQueue`의 축약 복사를 (PSO 핸들 + 바인딩 + 프로퍼티 CB) 전달로 확장하고,
GBuffer/Forward가 아이템별 PSO로 그린다(PSO 키 정렬로 배칭 유지). M0에서
폐기된 `.shader` 파일을 복구하지 않고, 물·바람 대표 재질을 새
`.shadermeta` + HLSL 계약으로 작성해 픽셀 판정한다.
★ **최대 위험 슬라이스다 — "배선만 이으면 그림이 더 나빠진다"**(Forward+
전례: 소비자 없는 출력을 이었더니 화면이 퇴보했다). 검증 씬과 셰이더
프로브(출력 단색 치환으로 원인 층 가르기)를 먼저 세우고 배선한다. 완료 시
Material의 값 사슬이 처음으로 GPU에 도달한다 — `ApplyShaderParams` 구 사슬은
여기서 은퇴.

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
실패 2 · 무판정 1**이었다. 이 완료는 M6가 실제 draw item에 property
CB/texture/PSO를 배선했다는 뜻이 아니며, 그 소비 작업은 그대로 M6 몫이다.

합계 **23일**(M0 1 · M1A 2.5 · M1B 3 · M2A 1.5 · M2B 1 · M3 3 · M4 2 ·
M5 3 · M6 4 · M7 2). M0·M1A·M1B·M2A·M2B·M3·M4의 실행 코드 기준
완료에 M7을 더해 **16일은 완료**, 남은 추정은 **7일**이다. 순서 제약: 다음은
M5이고, M6은 M2B·M3·M4·M5·M7 전부 선행. Slang 모듈·specialization·
`ParameterBlock`은 M6 완료 후 별도 최적화 트랙이다.

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
4. **머테리얼 커스텀 셰이더가 화면에 나온다** — 검증 씬 픽셀 판정 통과,
   Material 프로퍼티 변경이 다음 프레임에 반영.
5. **같은 메타·같은 소스에서 Slang DXIL과 SPIR-V가 나온다** —
   `VkShaderModule` 로드, DX12 PSO 생성, 두 타깃의 중립 `RHIShaderReflection`
   바인딩·cbuffer 레이아웃 일치 단정, `.shadermeta` 선언 대조를 모두 통과.
6. **두 번째 실행 컴파일 0건** + `.hlsli` 수정 시 영향받은 셰이더만 재컴파일
   (전체 `.cso` 삭제 경로 은퇴).
7. **자가 검증 스윕 판정 착수 전과 동일** + `dx12.live status` 패스 목록 문자
   일치 + 회귀 세트(Tools/regression) 통과.
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
- **핫리로드 불변식 해제 실수** — 핸들화(M5)가 서기 전에는 기존 "erase 금지"
  규약을 유지한다. 중간 슬라이스가 개별 erase를 도입하면 조용한 UAF다.

## 7. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| RhiBoundaryPlan §7.2 | **V5·V6를 이 페이즈가 승계한다** — M1=V5, M3=V6. V8(Vulkan 백엔드)은 M1A의 SPIR-V 산출을 이미 소비한다. 2026-08-16 DXC 기준선은 M1A의 완료 기록으로 남겨 두고, 2026-08-24 최종 컴파일러 계획은 M1B Slang 전환·M7 Slang reflection을 따른다. M3의 RHI-neutral desc 실행 코드 기준은 완료됐지만 메타 소비 배선은 M4·M6 몫으로 남는다 |
| BuildPipelinePlan (PHASE 12) | 게임 빌드의 셰이더 스텝 = 이 파이프라인의 전수 사전 컴파일 모드(3.3) |
| PHASE 10 파티클 · PHASE 11 지형 | 이 시스템 위에 선다 — 그쪽에서 셰이더 경로를 따로 만들지 않는다 |
| AssetResidencyPlan | 텍스처 상주는 그쪽 몫, 여기는 프로퍼티가 참조만 든다 |
| SceneGraphRedesignPlan | 직렬화 단일화(M5)가 트랙 P의 프리팹 왕복 회귀를 그대로 판정에 쓴다 |
