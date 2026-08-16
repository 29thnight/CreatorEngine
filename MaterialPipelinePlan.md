# 머테리얼 · 셰이더 파이프라인 재설계 (PHASE 3.5)

2026-08-11. Vulkan 골격(vk.selftest)이 선 직후 작성. RhiBoundaryPlan §7.2가 다음
슬라이스로 V5(셰이더 컴파일 93건)·V6(파이프라인 상태 기술 107건)를 지목하는데,
그 둘에 착수하기 전에 **그 인터페이스를 섬길 소비자 — 머테리얼·셰이더 애셋
파이프라인 — 의 모양을 먼저 정한다.** 구 RHI가 죽은 이유가 "소비자 없는
추상"이었고(RhiBoundaryPlan §1.1), V5를 지금 모양의 셰이더 시스템에 맞춰 자르면
같은 실수를 반복한다. 목표 사슬:

```
Material → 셰이더 메타데이터 → (Defines · Pass · RenderStates)
        → 셰이더 퍼뮤테이션 → HLSL 소스 → 플랫폼 컴파일러 → DXIL | SPIR-V
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

**가장 크게 틀린 것은 §1.1의 골자다 — "컴파일 시스템이 둘"은 이제 거짓이고,
하나다.** 애셋 경로(`ShaderSystem`·`ShaderPSO`·`ShaderDSL`·`VisualShader*`·
`HLSLCompiler`·`Shader.h`·`PSO`·`ShaderSelectionWindow` + `.hlsl` 113 ·
`.hlsli` 11 · `.shader` 12)가 통째로 사라졌다. 폐기 근거는 이 문서 §1.1이
적은 것과 같다(그림에 닿지 않는다). 즉 **결론은 맞았고 그 결론에 따른 실행이
계획보다 먼저 도착했다.**

아래 §1은 정정하되 원문의 판단은 남긴다 — 무엇을 보고 이 페이즈를 세웠는지가
근거이기 때문이다. 갱신된 현재 위치는 §1.0에 따로 적는다.

### 1.0 지금 실제 위치 (2026-08-11 재실측)

| 항목 | 값 | 판정 |
|---|---|---|
| 셰이더 컴파일 시스템 | **1개** | 애셋 경로 폐기로 수렴 |
| `D3DCompile` 호출 지점 | **1곳**(`DX12ShaderCompiler.cpp`) | M1 완료 기준 ①이 **이미 충족** |
| 패스의 `D3DCompile` | **0건** (착수 전 19) | 〃 |
| 컴파일 헬퍼 | **1개** (착수 전 22) | 오류에 파일·엔트리·타깃이 항상 붙는다 |
| HLSL 소스 | `Assets/Shaders/DefaultPassShader/` **39** + `SelfTest/` **7** | 파일이다(문자열 아님), `#include` 가능 |
| `D3DReflect` | **0건** | 리플렉션 몫은 폐기와 함께 소멸 |
| `ID3DBlob` (V5 잔량) | **66** | 패스가 blob을 들고 다니는 구간만 남음 |
| `D3D_SHADER_MACRO` | **10** · 4패스(Forward·Shadow·SSAO·SSGI) | **M2 범위는 그대로 유효** |
| `.cso` · 읽는 코드 | **0 · 0** | 정리됨 |
| `.meta` 고아 | 129 | **손댈 것 없음** — git 미추적(`.gitignore:465`)이고 `DataSystem` 초기화의 `ScanAndCleanupInvalidMeta`가 다음 실행에서 지운다(호출자 확인: `DataSystem.cpp:96,98`) |
| `Material::TrySetValue` 계열 | 살아 있음 | 게임 스크립트 8곳 + C# 인터롭이 호출. `m_cbMeta`가 늘 널이라 전부 false를 돌려준다 |

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
  (RhiBoundaryPlan §7.2.2 실측).

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

1. **V5·V6의 인터페이스를 소비자가 정하게 한다.** 지금 착수하면 V5는 "FXC
   호출을 DXC 호출로 옮겨 적기"가 되고, 퍼뮤테이션·캐시·리플렉션 요구가
   나중에 인터페이스를 다시 찢는다.
2. **PHASE 12(빌드 파이프라인)의 셰이더 스텝이 이 파이프라인의 오프라인
   모드다.** 게임 빌드가 셰이더를 미리 굽는 단계는 여기서 정의된 퍼뮤테이션
   전수 컴파일 그 자체다.
3. **PHASE 10(파티클)·11(지형)이 이 위에 선다.** 그쪽에서 셰이더 경로를 또
   만들면 세 번째 시스템이 생긴다.

**하지 않을 것**:

- **비주얼 셰이더 codegen 없음.** 노드 그래프→HLSL 생성은 별건이고, 지금 있는
  스텁(파서만)은 M0에서 은퇴한다. 텍스트 HLSL이 이 페이즈의 유일한 소스다.
- **파티클·지형·스프라이트 셰이더 없음** — PHASE 10·11의 몫. 이 페이즈의 배선
  대상은 메시 재질 경로(GBuffer·Forward)다.
- **HLSL 문법 현대화 강박 없음.** 소스 114개는 SM6로 컴파일되는 한 그대로 둔다.
  고치는 것은 컴파일이 깨지는 파일뿐이다(M1 게이트가 규모를 잰다).
- **머테리얼 에디터 UI 재설계 없음.** 인스펙터는 새 프로퍼티 모델을 읽는 최소
  배선만 한다. 본격 UI는 에디터 셸(PHASE 8) 이후.

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
IRHIShaderCompiler (V5 승계)     HLSL + defines → DXC → DXIL | SPIR-V + 중립 리플렉션
  ↓
파이프라인 캐시 (V6 승계)         (blob들 + RHIGraphicsPipelineDesc + RT 포맷) → PSO
```

### 3.2 ShaderMeta — 정규식 DSL의 후계

`.shader` 정규식 DSL은 은퇴하고 YAML로 간다. 근거: 엔진 직렬화 자산이 이미
YAML(`Meta::Serialize`)이라 파서를 하나 더 유지할 이유가 없고, 지금 DSL은
파싱 결과의 절반(`Keywords`·`tag`)이 어디에도 전달되지 않는 반쪽이다.

```yaml
# ForwardWater.shadermeta — 예시
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
    state: { cull: front, depthBias: shadow }
```

- **프로퍼티 선언이 곧 Material의 스키마다.** 컴파일 후 리플렉션과 대조해
  선언·실제 cbuffer 불일치를 로드 시점에 잡는다(지금은 수동 동기화라 조용히
  틀린다 — `MaterialInfomation` 주석의 "HLSL cbuffer와 이름을 맞춰"가 그 표시).
- **`state` 블록의 어휘 = V6의 `RHIGraphicsPipelineDesc` 어휘.** 메타 파서가
  중립 desc를 직접 채우므로 어휘가 두 벌 생기지 않는다.
- 큐 분류는 패스의 `queue`에서 유도한다. `m_renderingMode`는 호환 필드로
  남기되 진실은 메타다.

### 3.3 퍼뮤테이션 모델

- **키 = (metaGuid, passIndex, 키워드 비트셋).** 축은 메타가 선언한 것만
  존재하고, 집합은 정렬·정규화 뒤 해시한다.
- **에디터: 요청식 컴파일 + 캐시.** 처음 쓰는 조합만 컴파일하고 콘텐츠 해시
  캐시에 남긴다. **게임 빌드: 전수 사전 컴파일** — PHASE 12의 셰이더 스텝이
  이 모드를 부른다.
- **폭발 억제**: 축 선언 강제(자유 `#define` 주입 금지) + 메타당 퍼뮤테이션
  수를 로드 시점에 로깅 + 게임 빌드 전수 컴파일에 상한 게이트.
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
    std::span<const RHIShaderDefine> defines;   // 퍼뮤테이션이 채운다
    bool debug;
};

struct RHIShaderBlob {
    std::vector<uint8_t> bytes;        // DXIL 또는 SPIR-V
    RHIShaderReflection reflection;    // 백엔드 중립 — cbuffer 레이아웃·바인딩·입력 시그니처
};

class IRHIShaderCompiler {
public:
    virtual std::expected<RHIShaderBlob, RHIShaderError>
        Compile(const RHIShaderCompileRequest&) = 0;
};
```

- **구현은 DXC 벤더링**(`ThirdParty/` — a7d053ed의 규약대로). Windows SDK
  dxc는 SPIR-V가 꺼져 있으므로 시스템 것을 쓰지 않는다.
- **캐시 키 = hash(소스 콘텐츠 + include closure 콘텐츠 + defines + target +
  컴파일러 버전).** 파일명·타임스탬프가 아니라 콘텐츠다. 이것으로 `.hlsli`
  무차별 무효화가 은퇴하고, "두 번째 실행 컴파일 0건"이 퍼뮤테이션 세계에서도
  유지된다. include closure는 DXC의 include 핸들러가 컴파일 중에 실측으로
  수집한다 — 별도 그래프 파서를 만들지 않는다.
- **리플렉션 정규화**: DXIL은 `IDxcUtils` → `ID3D12ShaderReflection`, SPIR-V는
  spirv-reflect(벤더링). 산출은 `RHIShaderReflection` 하나로 정규화하고, 두
  타깃의 바인딩 정합은 M7에서 단정으로 검증한다.
- ★ **컴파일러 교체가 리플렉션 교체를 강제한다.** 지금 `ShaderPSO`의
  `D3DReflect`는 DXIL 컨테이너를 읽지 못한다. 그래서 컴파일러와 리플렉션이
  한 슬라이스(M1)다 — 나눠 옮길 수 있는 것이 아니다.

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

**M1 — 컴파일러 서비스와 DXC (V5 승계, 3일 → 2.5일) — 골격 완료, DXC가 남았다**
✅ 이미 선 것(`cc12ba4f`): `RHIShaderSource`(중립 읽기) / `DX12ShaderCompiler`
(백엔드 컴파일) 분리 — **V5가 갈아 끼울 경계가 여기다.** 컴파일 진입점 1곳,
패스의 `D3DCompile` 0건, 헬퍼 22 → 1(오류에 파일·엔트리·타깃이 항상 붙는다).
즉 **완료 기준 ①이 이미 충족됐다.**
남은 것: ① DXC 벤더링 — 지금 `DX12ShaderCompiler`는 여전히 `D3DCompile`(FXC)라
DXIL도 SPIR-V도 못 낸다 ② 콘텐츠 해시 캐시(3.4) ③ `IRHIShaderCompiler`로
인터페이스화(지금은 DX12 구체 타입) ④ `ID3DBlob` 66건 — 컴파일 결과가 DX12
타입인 채 패스를 타고 다닌다. 인계 지점(`DX12PSOManager`)은 이미
`const void* + size`라 중립이므로 패스가 blob을 드는 구간만 남았다.
★ **리플렉션 중립화는 범위에서 빠진다** — `D3DReflect` 0건, 그 몫은 자산
셰이더 폐기와 함께 사라졌다. 프로퍼티 검증(3.2)이 리플렉션을 다시 필요로
하면 그때 DXC 리플렉션으로 새로 세운다.
★ 게이트: HLSL **46개**(DefaultPassShader 39 + SelfTest 7) 전량 SM6 시험
컴파일로 fxc→dxc 파손 규모를 전수 측정. 20파일 초과면 M1을 쪼개 이중 타깃
기간을 둔다. 판정: 두 번째 실행 컴파일 0건 + `.hlsli` 한 파일 수정 시
영향받은 셰이더만 재컴파일.

**M2 — 퍼뮤테이션 프레임워크 (2.5일)**
PermutationKey + 요청식 컴파일(3.3). 소비자: 손 퍼뮤테이션 4곳(Shadow
SKINNING · Forward REFERENCE_PATH · SSAO · SSGI) 이관. 판정: 패스 코드에
`D3D_SHADER_MACRO` 배열 0건.

**M3 — 파이프라인 기술 중립화 (V6 승계, 3일)**
`RHIGraphicsPipelineDesc`·샘플러·상태 어휘(3.5). 패스 17종의
`DX12GraphicsPipelineDesc` 사용을 교체. 판정: 패스 17종에 `D3D12_` 직접 참조
0건(RhiBoundaryPlan §7.4 기준 2가 여기서 닫힌다).

**M4 — ShaderMeta 스키마 (3일 → 2일) — 애셋화는 끝났고 스키마만 남았다**
✅ 이미 된 것(`cc12ba4f`): 패스 24곳 인라인 HLSL 45블록(3,792줄)이 파일로.
`Assets/Shaders/DefaultPassShader/` 39 + `SelfTest/` 7. 나는 "재질 구동 패스
둘만 꺼내고 나머지는 강제로 옮기지 않는다"고 적었는데 전부 옮겼고, 그쪽이
옳다 — 문자열이 막고 있던 것이 편집기·`#include`·컴파일 시점 셋이었고 그중
셋째가 Vulkan 전제와 직결된다(§0). 부수로 문자열 이어붙이기 셋이 진짜
`#include`가 됐다.
남은 것: **`.shadermeta` YAML 스키마와 파서**(3.2) — 프로퍼티 선언·키워드
축·패스별 스테이지/렌더스테이트/큐. 지금 셰이더 파일에는 이 층이 없다(HLSL과
`kXxxShaderFile` 상수뿐). 배치 규약은 `cc12ba4f`가 정한 것을 따른다
(`Assets/` 아래 — `PakHelper`가 재귀 수집하므로 배포가 자동으로 따라온다).
M1 뒤라면 M2·M3과 병행 가능.

**M5 — Material 재설계 (3일)**
프로퍼티 블록·키워드 선택·메타 참조·직렬화 단일화·핸들화(3.6, 3.7). 기존
`.asset` 로드 호환: 구 5필드 → 표준 프로퍼티 리매핑. 판정: 기존 씬·머테리얼
전부 로드 왕복(저장→로드→재저장 diff 0) + 회귀 세트 통과.

**M6 — 소비 배선 — 머테리얼이 고른 셰이더로 실제 드로우 (4일)**
`copyQueue`의 축약 복사를 (PSO 핸들 + 바인딩 + 프로퍼티 CB) 전달로 확장하고,
GBuffer/Forward가 아이템별 PSO로 그린다(PSO 키 정렬로 배칭 유지). 기존
`.shader` 12종 중 대표 2종(물·바람)을 새 메타로 재작성해 픽셀 판정.
★ **최대 위험 슬라이스다 — "배선만 이으면 그림이 더 나빠진다"**(Forward+
전례: 소비자 없는 출력을 이었더니 화면이 퇴보했다). 검증 씬과 셰이더
프로브(출력 단색 치환으로 원인 층 가르기)를 먼저 세우고 배선한다. 완료 시
Material의 값 사슬이 처음으로 GPU에 도달한다 — `ApplyShaderParams` 구 사슬은
여기서 은퇴.

**M7 — SPIR-V 타깃 (2일)**
같은 요청에 `target=SPIRV`. spirv-reflect 정합 검증 — DXIL 리플렉션과 바인딩
일치를 단정으로. 소비자: vk.selftest 확장 — 재질 퍼뮤테이션 하나를 SPIR-V로
컴파일하고 `VkShaderModule` 로드까지(그리기는 V8의 몫). M1 뒤 언제든 가능.

합계 **21.5일 → 20일**(M0 완료 · M1 2.5 · M4 2로 재조정). 순서 제약: M1 → M2
→ M3 순차(캐시 → 퍼뮤테이션 → PSO), M4는 M1 뒤 병행 가능, M5는 M4 뒤, M6은
M2~M5 전부 선행, M7은 M1 뒤 자유.

★ **판정 문구 정정**: "자가 검증 33종"은 낡았다 — 기준선이 35종으로 갱신됐고
(`a253a22d`), 셰이더 작업 두 커밋은 **32종 통과**로 판정했다. 각 슬라이스는
착수 시점의 스윕 결과를 기준선으로 삼고 그 수를 노트에 적는다.

★ **판정 줄의 의미가 넓어진 것을 이어받는다**(`cc12ba4f`): 소스가 파일이 되면서
"셰이더 컴파일 통과"가 "파일을 찾았고 컴파일된다"가 됐다 — 배포에서 셰이더가
빠지면 여기서 잡힌다. M7의 SPIR-V 산출도 같은 성질을 갖는다.

---

## 5. 완료 기준

1. **컴파일 진입점 하나** — ✅ **충족**(`cc12ba4f`): 호출 1곳
   (`DX12ShaderCompiler.cpp`), 패스 0건, 헬퍼 22 → 1.
   단 **FXC 은퇴는 미완** — 그 한 곳이 아직 `D3DCompile`이다(M1 잔여).
2. **손 퍼뮤테이션 0건** — `D3D_SHADER_MACRO` 배열이 패스 코드에 없다.
3. **패스 17종에 `D3D12_` 직접 참조 0건** (V6 몫 — RhiBoundaryPlan §7.4-2와
   같은 기준) + 재질 구동 패스의 렌더스테이트가 메타 데이터에 있다.
4. **머테리얼 커스텀 셰이더가 화면에 나온다** — 검증 씬 픽셀 판정 통과,
   Material 프로퍼티 변경이 다음 프레임에 반영.
5. **같은 메타·같은 소스에서 DXIL과 SPIR-V가 나온다** — vk.selftest의 SPIR-V
   모듈 로드 통과 + 두 타깃 리플렉션의 바인딩 일치 단정 통과.
6. **두 번째 실행 컴파일 0건** + `.hlsli` 수정 시 영향받은 셰이더만 재컴파일
   (전체 `.cso` 삭제 경로 은퇴).
7. **자가 검증 스윕 판정 착수 전과 동일** + `dx12.live status` 패스 목록 문자
   일치 + 회귀 세트(Tools/regression) 통과.
8. **직렬화 경로 하나** — 머테리얼 필드 추가 시 수정 지점이 한 곳.

## 6. 리스크

- **fxc→dxc 전수 파손 규모 미지** — M1 게이트가 잰다. SM5 시절 문법(레거시
  샘플러 문법 등)이 얼마나 남았는지에 따라 M1이 쪼개질 수 있다.
- **M6 배선** — 소비자 없는 출력을 이었을 때 그림이 나빠진 전례가 실측으로
  있다. 프로브·검증 씬 선행이 그 대응이고, 픽셀 판정 없이는 배선을 완료로
  치지 않는다.
- **퍼뮤테이션 폭발** — 축 선언 강제 + 수 로깅 + 빌드 상한 게이트(3.3).
- **핫리로드 불변식 해제 실수** — 핸들화(M5)가 서기 전에는 기존 "erase 금지"
  규약을 유지한다. 중간 슬라이스가 개별 erase를 도입하면 조용한 UAF다.
- **DXC·spirv-reflect 벤더링** — `ThirdParty/` 규약(a7d053ed)대로. DXC는 MS
  공식 바이너리 배포가 있어 소스 빌드는 하지 않는다.

## 7. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| RhiBoundaryPlan §7.2 | **V5·V6를 이 페이즈가 승계한다** — M1=V5, M3=V6. V8(Vulkan 백엔드)은 M7의 SPIR-V 산출을 소비한다. ★ 경계 정정(2026-08-11): **V5 전반(소스 파일화 · 컴파일 일원화 · 옛 시스템 폐기)은 3-1에서 이미 끝났다.** M1이 승계하는 것은 잔여다 — `ID3DBlob` 66건 + DXC 도입 + 캐시 + 인터페이스화. 두 문서가 같은 사실을 각자의 관점(3-1은 백엔드 중립화, 3.5는 머테리얼 파이프라인)에서 적으므로, **수치의 정본은 RhiBoundaryPlan §7.2.3**이다 |
| BuildPipelinePlan (PHASE 12) | 게임 빌드의 셰이더 스텝 = 이 파이프라인의 전수 사전 컴파일 모드(3.3) |
| PHASE 10 파티클 · PHASE 11 지형 | 이 시스템 위에 선다 — 그쪽에서 셰이더 경로를 따로 만들지 않는다 |
| AssetResidencyPlan | 텍스처 상주는 그쪽 몫, 여기는 프로퍼티가 참조만 든다 |
| SceneGraphRedesignPlan | 직렬화 단일화(M5)가 트랙 P의 프리팹 왕복 회귀를 그대로 판정에 쓴다 |
