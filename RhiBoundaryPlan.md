# RHI 경계 재설계 (PHASE 3-1 재정의)

2026-08-07 작성. DX11 구 렌더러 은퇴가 계기다. **목표는 멀티백엔드 RHI** —
DX12 와 Vulkan 이 **같은 패스 코드**로 그린다.

★ **2026-08-11 압축.** 3,833줄이 되면서 완료 기록과 살아 있는 계획이 섞여
순서 오판을 낳았다(V7 을 맨 뒤에 둔 채 5번에 착수한 사건 — §4.1). 완료
슬라이스의 상세 서사(착수 전 예상 · 완료 대조 · 정정 경위)는 **커밋
`d74fc2c3` 이전의 이 파일**과 각 커밋 메시지에 있다. 여기에는 ①현재 상태
②남은 작업 ③대장 ④돈 주고 배운 규칙만 남긴다.

★ 코드 주석이 인용하는 옛 절 번호(`§7.2.x` · `§8.x` · `§6.2` 등)는 그
보존판의 것이다 — `git show d74fc2c3:RhiBoundaryPlan.md` 로 읽는다.

---

## 0. 현재 상태 (2026-08-15)

### 0.1 지표 — 진척은 이 셋으로만 보고한다

| 지표 | 값 | 비고 |
|---|---|---|
| Vulkan 이 구현한 경계 인터페이스 | **7/7** | 구현 셋 + `RHIEncoder` + `IRenderDeviceServices` + `IRenderPipelineCache` + 중립 texture/mesh cache. GizmoIcon이 실제 PNG/root SRV를, 기본 지오메트리 슬라이스가 depth/MRT/compute buffer/fullscreen 경로를 실물로 만들었다(§0.4~0.5) |
| 두 백엔드가 공유하는 패스 | **10/17** | `EnhancedGridPass` · `EnhancedSkyBoxPass` · `EnhancedGizmoIconPass` · `EnhancedShadowPass` · `EnhancedGBufferPass` · `EnhancedDecalPass` · `EnhancedSSAOPass` · `EnhancedSSGIPass` · `EnhancedForwardPass` · `EnhancedDeferredPass` — 같은 공용 패스 코드가 두 백엔드에서 돈다 |
| 두 백엔드 픽셀 대조 검사 | **10** | `vk.grid` · `vk.skybox` · `vk.gizmoicon` · `vk.shadow` · `vk.gbuffer` · `vk.decal` · `vk.ssao` · `vk.ssgi` · `vk.forward` · `vk.deferred`. 확률 회전 패스는 의미·전체 분포를 대조하며, 전체 Vulkan validation·미구현 호출은 0 |

"DX12 심볼 몇 건 남았다"는 진척이 아니다 — 그 수가 0 이 돼도 두 번째
백엔드가 도는지는 답이 안 나온다. 기존 지표(전수 35종 판정 불변 등)는
**회귀 방어**로만 쓴다.

### 0.2 완료 — 5번 "A 완료": 그리드가 두 백엔드에서 돈다 ✔ (2026-08-12)

★★★ **"5번이 맴돈다"는 지적이 맞았고, 원인을 실측했다 (2026-08-12).**

증상의 수치: "좋아 5번 진행" 시점에 5는 **4항목**(5a·5b·5c·5d)이었는데
10커밋(5a·5b·G-2a·5c-1·2·3·5c-4a·b·c·d) 동안 **12항목**으로 자랐고, 그
사이 지표 ②·③은 0에 머물렀다. 매 보고의 "다음"이 항상 새 준비물을
가리켰고 말단(5d)을 가리킨 적이 없다.

원인: **완료 판정을 말단 검사가 아니라 부분 자가 냈다.** "남은 것" 계수가
전부 **서비스·인코더 호출 표** 하나로 쟀는데, 그 자에는 두 차원이 안 보인다
— ① 파이프라인 **생성 입력**(그리드는 `DX12ShaderCompiler`로 DXBC를 굽고
있었다 — 같은 자가 "그리드는 DX12 심볼 0"이라고 오측한 이유도 이것: include
와 정적 호출은 호출 표에 안 잡힌다) ② **검사 배관**(리드백 넷). 그래서
표가 보는 것을 끝내면 표 밖 차원이 드러나 새 항목이 되고, 목록이 줄지 않고
갈아탔다.

처방(그리고 그것이 이 완료다): 준비물을 더 자르지 않고 **5d를 그냥 밟았다.**
밟자마자 표 밖 결함이 셋 나왔고 전부 패스 밖에서 닫았다:

| 실측 결함 | 처방 |
|---|---|
| 셰이더가 DXBC로 구워진다 — 패스가 `DX12ShaderCompiler`를 직접 부른다 | `VulkanShaderCompiler`(런타임 dxc → SPIR-V, `VulkanBindingModel` 시프트 공유) + SPIR-V 모드면 DX12 이름에서 흘린다. 호출부 중립화는 M 트랙 |
| 배리어가 열린 렌더링 안에서 기록된다 — DX12엔 여닫이가 없어 그래프가 모른다 | **전이가 스스로 닫는다**(`TransitionResources`·`EndFrame`·복사가 각각). 여닫이를 중립 계약에 안 들인 판단은 유지 |
| 리드백 넷이 스텁 | 5d가 청구했으므로 연다(R6 미룰 때 적어 둔 조건 그대로). 행 간격은 Vulkan이 촘촘 — `rowPitch`가 계약이라 읽는 쪽 무변경 |

부수 실측: `discard`가 1.3 기능(`shaderDemoteToHelperInvocation`)을 요구한다
— 패스 대부분이 `clip`을 쓰므로 슬라이스 7 전체가 이것에 기댄다. dxc 는
`VULKAN_SDK` 환경이 세션에 없을 수 있어 머신 레지스트리 폴백(스크립트가
밟은 함정과 동일).

**결과: `vk.grid` 통과 — 패스 코드 무변경 · 픽셀 편차 0.0% · 검증 레이어
클린.** 같은 HLSL·같은 카메라에서 래스터까지 픽셀 단위로 일치했다.

### 0.3 완료 — 첫 텍스처 소비 공용 패스: SkyBox ✔ (2026-08-12)

`EnhancedSkyBoxPass`를 고치지 않고 `dx12.skybox`와 `vk.skybox`가 같은 합성
큐브맵을 그렸다. 세 카메라(+X·-Z·+Z)의 중심 RGB가 양쪽 모두 각각
`(1,0,0)` · `(0,1,1)` · `(1,0,1)`이고, 매 방향 전면 커버가
`65536/65536`이었다. Vulkan 검증 레이어 문제와 미구현 호출은 0건이다.

이 슬라이스가 닫은 경계는 `CreateBindings` → `SetBindings` → `Draw`다.

- `CreateBindings`는 셋 레이아웃을 모르는 시점이므로 포인터나
  `VkDescriptorSet`을 싣지 않는다. 프레임 요청 표에 `RHIBindingDesc`를 값으로
  복사하고 `RHIBindingTable::backend`에는 1-based 정수 슬롯만 둔다.
- `SetBindings(slot, table)`가 현재 파이프라인 레이아웃의 슬롯 번호표를 풀어
  디스크립터 종류·개수를 검증하고, CBV와 이미지 쓰기를 한 셋 상태로 합친다.
  셋은 기존 프레임 풀에서 할당되어 해당 슬롯의 GPU 완료 뒤에만 재사용된다.
- 큐브 `VkImageView`는 디스크립터 셋이 아니라 이미지 리소스 엔트리가 소유한다.
  따라서 프레임 풀이 리셋된 뒤에도 리소스가 살아 있는 동안 뷰 수명이 보장된다.
- `VulkanLayoutSlot`이 테이블 범위 개수를 함께 들어 다중 디스크립터 요청도
  연속 binding으로 펼치며, 종류나 개수가 어긋나면 부분 갱신 없이 거절한다.

회귀 대조: 같은 실행에서 `dx12.skybox` · `vk.grid` · `vk.skybox`를 연속 실행해
셋 모두 통과했다. `vk.grid`는 점등 `9840/9840`, 원점 R `0.225/0.225`, 편차
`0.0%`를 유지했다.

### 0.4 완료 — 실제 PNG 소비 공용 패스: GizmoIcon ✔ (2026-08-12)

`EnhancedGizmoIconPass`가 같은 공용 코드로 DX12와 Vulkan에서 실제
`CameraGizmo.png`를 그린다. 두 검사의 256×256 리드백은 중심 R `0.500`
(`128,128`) · 쿼드 안 투명 지점 R `0.000`(`128,152`) · 쿼드 밖 R `0.000` ·
점등 `1346`으로 일치했다. Vulkan 검증 레이어 문제와 미구현 호출은 0건이다.

이 슬라이스가 마지막 경계와 인스턴스 입력을 함께 닫았다.

- `RHITextureEntry`와 `IRenderTextureCache`를 `RHI/`의 중립 계약으로 올리고,
  DX12 엔트리는 호환 별칭으로 두었다. Vulkan 캐시는 자산 ID별 이미지와 뷰를
  소유하며 프레임 업로드 링에서 복사·전이한다.
- 실제 PNG가 WIC의 BGRA8로 들어오는 것을 첫 실행의 업로드 실패 계수가 잡았다.
  Vulkan 업로드가 이를 RHI RGBA8로 명시 변환한 뒤 업로드 `1` · 실패 `0`이 됐다.
- 그래프의 동적 렌더링이 열리기 전 `PrepareFrame`에서 텍스처를 올린다. 따라서
  이미지 복사/전이와 렌더 패스 기록 순서가 백엔드별 우회 없이 성립한다.
- `ShaderResourceBuffer(tN)` 레이아웃은 Vulkan storage-buffer descriptor로
  번역되고, `SetRootBuffer`가 현재 파이프라인의 슬롯 종류와 리소스 범위를
  검증해 아이콘 instance upload를 건다. 디스크립터 셋과 업로드 메모리는 기존
  프레임 슬롯 펜스 뒤에서만 재사용된다.

회귀 대조: `vk.gizmoicon`은 실제 Camera PNG 로드 · 그래프 실행 2/컬링 0 ·
아이콘 1/배치 1 · validation 0으로 통과했다. 이어 `dx12.gizmoicon`이 같은
픽셀값을 냈고, `dx12.gizmoscene`은 camera 1 · light 1을 scene source에서
주입해 실행 5/컬링 0으로 통과했다. 실제 scene route도 흰 1×1 fallback이 아니라
카메라 PNG를 소비한다.

### 0.5 완료 — 기본 지오메트리 4패스: Shadow → GBuffer → Forward → Deferred ✔ (2026-08-14)

힙/프레임 수명 변경이 진행 중인 트리에서 네 패스를 순서대로 실제 소비자로
세웠다. 패스별 우회 코드는 두지 않았고 `EnhancedRenderGraph`와 같은 공용 패스
본문을 DX12/Vulkan에서 실행했다.

- `vk.shadow`: depth-only PSO · 배열 slice DSV · indexed mesh. 첫 cascade의
  기록 `4194304/4194304`, min `0.61962/0.61962`, mean `0.79364/0.79364`.
- `vk.gbuffer`: indexed mesh + fallback 2D texture + dynamic sampler로 5 MRT와
  D32 depth를 생성. center/coverage/bitmask/depth 편차 0.
- `vk.forward`: compute PSO가 depth SRV와 structured UAV table을 소비하고,
  그래프가 두 persistent buffer의 UAV→SRV→CopySource 전이를 추적한다. 방향광
  하나가 모든 16타일에 `count=1`, shade center/coverage 편차 0.
- `vk.deferred`: 앞 GBuffer의 5 MRT+depth를 9-slot SRV table로 소비하고
  fullscreen triangle을 그린다. center RGBA와 lit coverage, 바깥 RGB 편차 0.

이를 위해 descriptor table에 image UAV와 structured-buffer UAV를 구분하는 중립
종류를 추가했다. Vulkan은 robustness2 `nullDescriptor`를 실제 장치 기능으로
확인한 경우에만 그림자/IBL의 빈 descriptor를 허용한다. Forward 메시 업로드는
dynamic rendering 전에 `PrepareFrame`에서 끝내며, buffer transition/UAV barrier와
buffer readback은 핸들/프레임 수명 계약을 그대로 따른다. 함께 드러난 공용 상태
차이 두 건(Vulkan front-face winding, 대상 alpha 보존 Zero/One)도 DX12 계약과
맞췄고 `vk.shadow` 재회귀로 depth culling이 유지됨을 확인했다.

네 검사 모두 Vulkan validation 0 · 미구현 호출 0으로 통과했다.

### 0.6 완료 — Decal 공용 패스 + Vulkan editor live 연결 ✔ (2026-08-14)

실제 scene 순서의 첫 누락이던 `EnhancedDecalPass`를 GBuffer와 Deferred 사이에
넣었다. `vk.decal`은 같은 공용 패스 코드를 두 백엔드에서 실행하고 GBuffer
snapshot 3장, 읽기 전용 depth DSV/SRV, 채널별 independent MRT blend와 root
instance buffer를 함께 대조한다.

- 그래프 실행 4 · 컬링 0 · transient 9, decal/batch 1/1.
- center diffuse RGB `0.4390/0.4390, 0.3740/0.3740, 0.5610/0.5610`,
  normal Z `1.0000/1.0000`, ORM R `1.0000/1.0000`.
- 적용 coverage `576/576`, center/outside 최대 편차 `0/0`, mesh/texture 실패 0,
  Vulkan validation·미구현 호출 0.

첫 실행이 두 실제 경계 누락을 잡았다. Vulkan device에서 `independentBlend`를
켜지 않아 VUID 경고 6건이 났고, `CopyResource`가 계수만 올리는 stub이라 GBuffer
사본이 0으로 읽혔다. 전자는 물리 장치 기능 확인+device feature 활성화로, 후자는
`vkCmdCopyImage` 전 subresource 복사로 닫았다. Geometry 검사도 이제 서비스와
encoder의 미구현 계수를 모두 합쳐 판정한다.

이 슬라이스 당시 editor live는 `Shadow → GBuffer → Decal → Deferred → Forward+`
순서였고, 다음 SSAO 슬라이스에서 현재 순서로 확장했다. 실제
`scene.glb` 배치 검증에서 1920×1080 · draw/batch 22/22 · mesh resident/upload
25/25 · failure 0 · 완성 38프레임을 기록했고 종료 로그 오류는 0건이었다. 이때
드러난 DX12식 D32 resource/R32 SRV 어휘는 Vulkan의 기존 D32 depth view로
정규화해 shadow가 든 Deferred/Forward descriptor table도 validation 0으로 맞췄다.

### 0.7 완료 — SSAO 공용 패스 + 실제 scene GBuffer 소비 ✔ (2026-08-14)

`EnhancedSSAOPass`의 compute·bilateral filter를 공용 코드 그대로 Vulkan에서
실행한다. `vk.ssao`는 절차적으로 만든 R32 depth와 RGBA16 normal을 두 백엔드에
넣고 raw/filtered RG16 AO 두 장을 한 리드백으로 가져온다.

- Vulkan 그래프 실행 4 · 컬링 0 · transient 4, AO 128×128.
- flat AO `0.969/1.000`, 계단 안쪽 `0.373/0.404`, 핵심 대비는
  `0.596/0.596`으로 일치했다.
- 이웃 차이는 DX12 `0.02141→0.01035`, Vulkan `0.02141→0.00988`로 양쪽 모두
  필터 뒤 감소했다. 픽셀 회전은 `sin/cos` 초월함수 근사 차이 때문에 exact
  pixel 대신 flat/edge 대비와 전체 필터 분포로 판정한다.
- 미구현 호출 0 · Vulkan validation 0. 이어 `vk.shadow` · `vk.gbuffer` ·
  `vk.decal` · `vk.deferred` · `vk.forward`도 같은 실행에서 모두 재통과했다.

첫 실행은 SPIR-V typed storage image 포맷 불일치를 정확히 잡았다. HLSL의
`RWTexture2D<float2/float4>`가 SPIR-V에서 RG32/RGBA32로 추론됐는데 실제 뷰는
RG16/RGBA16이라 validation 3건과 undefined store가 났다. Vulkan 컴파일 때만
`vk::image_format("rg16f"/"rgba16f")`를 명시해 DX12 셰이더 의미는 유지하고
storage view 계약을 일치시켰다.

이 단계의 임시 live 순서는 `Shadow → GBuffer → Decal → SSAO → Deferred →
Forward+`였고 SSAO를 keep-alive로 살렸다. 다음 SSGI 슬라이스(§0.8)에서 정상
소비자가 생겨 임시 루트를 제거했다. 당시 `scene.glb` 검증은 draw/batch `22/22`,
mesh resident/upload `25/25`, failure 0, 정상 종료·로그 오류 0건이었다.

### 0.8 완료 — SSGI 공용 패스 + 뷰별 temporal live 연결 ✔ (2026-08-14)

`EnhancedSSGIPass`의 Hi-Z 6밉, trace, temporal resolve, bilateral filter,
full-resolution composite, persistent history copy를 같은 공용 코드로 두 백엔드에서
실행한다. `vk.ssgi`는 64×64 절차 씬을 6프레임 돌리고 반해상도 GI 두 장과
전해상도 direct/composite 두 장을 대조한다.

- Vulkan 그래프 실행 13 · 컬링 0 · transient 15, 미구현 0 · validation 0.
- temporal 누적 평균/최대는 양쪽 모두 `6.00/6.0`이었다.
- GI 평균은 `0.03894/0.03898`, 이웃 차이는 DX12 `0.03207→0.01383`, Vulkan
  `0.03215→0.01387`로 감소했다.
- 합성 간접광 평균/최대는 `0.02323/0.1445` · `0.02326/0.1445`, 변경 픽셀은
  `2691/2691`, 평균 GI/output 편차는 `0.00004/0.00002`다.

첫 실행 전에 공용 trace의 좌표 결함을 찾았다. Hi-Z는 반해상도인데 전해상도
normal도 같은 정수 좌표로 읽어 화면 좌상단 사분면만 참조했다. `normalSize`를
상수로 넘겨 UV 기준 좌표로 읽게 고쳤고, `dx12.ssgi` 8프레임 누적 `8.00`, 실행
`14/14`, 필터 감소를 재확인했다. SSGI의 RGBA16 storage output 네 곳에는 Vulkan
컴파일에서만 `rgba16f` typed image 포맷을 명시했다.

Vulkan editor live는 이제
`Shadow → GBuffer → Decal → SSAO → Deferred → SSGI → Forward+`다. SSGI는
카메라 뷰마다 인스턴스를 두어 히스토리 텍스처·이전 view-projection·핑퐁 인덱스가
다른 뷰와 섞이지 않는다. 뷰 슬롯 재배정 때는 `ResetHistory`로 temporal 상태만
버리고 PSO와 텍스처는 재사용한다. SSAO의 임시 keep-alive는 껐고 SSGI composite가
AO를 간접광에만 곱하는 실제 소비자가 됐다.

실제 `scene.glb` live 검증은 1920×1080 · 완성 85프레임 · draw/batch `22/22` ·
mesh resident/upload `25/25` · failure 0이었다. 상태의 패스 목록에
`SSGI.HiZ → Trace → Resolve → Filter → Composite → StoreHistory`가 모두 들어왔고
정상 종료 로그 오류는 0건이었다.

### 0.9 완료 — 공용 Render 물리 계층 + VS 필터 정리 ✔ (2026-08-14)

공용 그래프·패스·씬 러너를 `RHI/DX12` 밖으로 실제 이동했다. 파일 이름만
중립인 코드를 DX12 백엔드 폴더에 계속 두면 새 패스를 만들 때 다시 그 폴더를
기준으로 include하게 되므로, SSS 이식 전에 물리 경계를 먼저 맞췄다.

- 공용 파일 46개를 `Render/{Core,Graph,Scene,Passes}`로 이동했다. 17개 패스는
  `Geometry` · `Lighting` · `PostProcess` · `Editor` 네 카테고리로 나뉜다.
- DX12 전용 검사·벤치 23개는 `RHI/DX12/Tests`의 다섯 카테고리로, Vulkan
  패스 검사 3개는 `RHI/Vulkan/Tests`로 옮겼다. 총 물리 이동은 72개다.
- 아직 원시 DX12 리소스를 쓰는 `EnhancedIBLGenerator`와 호환 파사드
  `RenderFrameServices`는 공용 폴더로 위장하지 않고 `RHI/DX12`에 남겼다.
- `.vcxproj.filters`는 공용 Render와 각 backend의 Device · Commands · Pipeline ·
  Memory · Resources · Diagnostics · Platform · Tests를 구분한다. 프로젝트 항목
  223개는 실제 파일과 일치하고 중복 0 · 필터 미선언 0이다.
- include는 각 파일 기준 상대 경로로 정규화했다. `$(ProjectDir)`를 include root로
  추가하면 로컬 `MeshOptimizer.h`가 서드파티 `<meshoptimizer.h>`를 가리는 것을
  첫 빌드가 잡았고, root 추가를 제거해 기존 검색 우선순위를 보존했다.

VS18/v145 Debug x64 전체 빌드는 경고 0 · 오류 0이다. 이동 뒤 `vk.ssgi`는 실행
13 · transient 15 · validation/미구현 0과 기존 픽셀 수치를 유지했다. 실제
`scene.glb` Vulkan live도 1920×1080 · 완성 44프레임 · draw/batch `22/22` ·
mesh resident/upload `25/25` · failure 0 · 종료 로그 오류 0으로 통과했다.

### 0.10 완료 — ImGui renderer backend RHI 경계 ✔ (2026-08-14)

기존 `IImGuiHost`는 이름만 중립이었다. 구현 팩토리가
`RHI/DX12/ImGuiDx12Host.cpp`에 있고 `EditorImGuiTexture`와 live scene 표시가
`ImGuiDx12Shell` 싱글턴을 직접 호출했으므로 Vulkan 구현을 꽂을 절단면이 없었다.

- `ImGuiHost`가 ImGui 컨텍스트·Win32 플랫폼 backend·도킹/viewport 프레임을
  공통 소유한다. GPU 쪽은 `IImGuiRendererBackend`의 Initialize · Resize ·
  NewFrame · RenderAndPresent · texture transport 계약으로 내려갔다.
- `ImGuiDx12Shell`은 같은 계약의 DX12 구현으로 바뀌었고 기존 descriptor heap,
  DXGI shared handle, CPU RGBA bridge 동작을 보존한다.
- `ImGuiVulkanShell`은 `VulkanDeviceResources`의 swapchain · timeline semaphore ·
  command context를 재사용하고 동적 렌더링으로 `imgui_impl_vulkan` draw data를
  기록한다. 사용자 `Texture`는 `VulkanTextureCache`, live readback은 동일한
  `SubmitCpuRgbaFrame` 계약을 통해 Vulkan sampled image로 올라간다.
- 상위 `EditorImGuiTexture`와 `EnhancedSceneRendererLive`는 `IImGuiHost`만 보며
  D3D12 GPU descriptor handle과 Vulkan descriptor set의 차이를 모른다.
- `imguiBackendDx12=true/false`가 각각 DX12/Vulkan 부팅 선택이고,
  `renderBackendDx12`도 실제 초기 scene RHI 선택으로 복원했다. 두 백엔드는
  폰트·descriptor·platform viewport 수명 때문에 실행 중 hot swap하지 않는다.
- Vulkan ImGui는 DXGI shared handle을 열 수 없으므로 이를 선택하면 초기 scene
  RHI도 Vulkan으로 맞춘다. 반대 조합(Vulkan scene → DX12 ImGui)은 기존 CPU
  RGBA bridge가 지원한다.
- 공식 ImGui Vulkan binding의 `vulkan-1.dll` import는 delay-load다. DX12 선택
  환경에서는 Vulkan loader가 없어도 뜨며, Vulkan 선택 시 기존 동적 loader
  검사가 먼저 실패를 보고한다.

VS18/v145 Debug x64 전체 빌드는 오류 0이다. Vulkan 설정으로 에디터를 실제
부팅해 `ImGui=Vulkan` · `scene=enhanced-vulkan`을 로그에서 확인했고, 60프레임
대기 동안 `Shadow→GBuffer→Decal→SSAO→Deferred→Forward+` 첫 live frame이
완성됐다. ImGui/Vulkan validation 메시지와 세션 오류는 0건이며 정상 종료했다.
기본 DX12 설정도 별도 회귀 실행에서 `ImGui=DX12` · `scene=enhanced-dx12`,
CPU texture upload 실패 0, 세션 오류 0과 정상 종료를 확인했다.

### 0.11 완료 — ImGui texture completion 수명 ✔ (2026-08-14)

ImGui renderer backend 분리 뒤 남은 사용자 texture 수명 구멍을 닫았다. 두 셸이
`Texture*` 주소를 descriptor 캐시 키로 썼기 때문에 자산 파괴 뒤 같은 주소가
재사용되면 이전 그림을 돌려줄 수 있었고, DX12는 다음 프레임 업로드 목록에도
원시 포인터를 보관했다.

- 두 backend 모두 `Texture::m_assetId`를 등록 신원으로 쓴다. DX12의 원시 포인터
  대기 업로드는 없앴고 열린 ImGui 프레임에서 즉시 기록한다.
- `RegisterTexture` · `OpenSharedTexture` · `GetCpuFrameTextureId`가 현재 프레임의
  사용 표식이다. 120프레임 미사용이면 현재 제출 완료점으로 은퇴시키고,
  completed value가 그 값을 지난 뒤에만 DX12 SRV slot 또는 Vulkan descriptor
  set을 반환한다.
- 자산 descriptor와 실제 GPU image가 따로 먼저 죽지 않도록 DX12TextureCache와
  VulkanTextureCache도 같은 프레임 표식과 graveyard 완료점을 사용한다.
- 콘텐츠 브라우저의 확장자 아이콘 표는 backend별 `ImTextureID`를 영구 저장하지
  않고 `Texture*`만 들며, 그리는 프레임마다 `EditorImGuiTexture::From`으로
  해석한다. 따라서 보이는 정적 아이콘도 정상적으로 사용 표식을 갱신한다.

정책 경계값(119/120프레임)과 완료값 전/후는 공통 constexpr 판정으로 고정했다.

검증은 VS18/v145 Debug x64 전체 솔루션 빌드(경고 0, 오류 0)와 실제 editor
실행으로 마쳤다. DX12는 64프레임, Vulkan은 61프레임을 완료한 뒤 각각 CLI
종료까지 오류 0건으로 정상 종료했다. 실행 뒤 기본 설정은 DX12/DX12로 복원했다.

### 0.12 완료 — 공통 completion retire queue ✔ (2026-08-15)

캐시와 ImGui 셸마다 복제되어 있던 fence/timeline 묘지 여섯 곳을
`RHICompletionRetireQueue`로 통합했다. 공통 큐는 backend payload를 해석하지 않고
completion 판정, pending bytes/count와 quarantine만 관리한다. 실제 DX12 `ComPtr`,
Vulkan handle 및 descriptor 파괴는 큐를 소유한 backend가 수행한다.

- DX12/Vulkan mesh·texture cache와 두 ImGui 셸이 같은 완료 규칙을 사용한다.
- completion 0은 최대 completed value에서도 회수하지 않고 device teardown의
  `Drain`에서만 파괴한다.
- cache graveyard 통계는 별도 증감값이 아니라 공통 큐를 단일 진실 원천으로 읽는다.
- Vulkan live pipeline에서 누락됐던 texture cache의 BeginFrame/Retire/Sweep도
  실제 scene frame 경계에 연결했다.
- GPU completion과 무관한 DX12 shared-handle 격리소는 `ExternalInteropRetired`로
  분리했다. 외부 소비 종료를 fence가 증명하지 못하므로 renderer teardown까지 둔다.

VS18/v145 Debug x64 전체 빌드는 경고 0, 오류 0이다. `rhi.uploadsegments`에서
공통 큐 경계·quarantine, DX12 7/7, Vulkan 4/4와 validation clean을 확인했다.
DX12 editor는 177프레임에서 texture 1개 128.0 MiB를 실제 회수해 묘지/격리 0,
Vulkan editor는 130프레임 뒤 texture/mesh 묘지/격리 0으로 정상 종료했다.
양쪽 로그의 오류·critical·crash·validation은 모두 0이며 기본 설정은 DX12로 복원했다.

### 0.13 완료 — Slice D persistent buffer heap ✔ (2026-08-15)

양쪽 mesh cache의 vertex/index buffer를 개별 committed/dedicated allocation에서
큰 segment 내부의 placed/bound suballocation으로 전환했다. 백엔드 중립
`RHIPersistentHeapPolicy`가 compatibility key별 best-fit, alignment padding 보존,
주소 기반 인접 병합, empty segment trim과 generation handle을 관리한다.

- DX12는 `ID3D12Heap` + `CreatePlacedResource`, Vulkan은 `VkDeviceMemory` +
  `vkBindBufferMemory`를 쓴다. DX12 allocation info와 Vulkan requirements2/dedicated
  requirements는 각 adapter가 해석한다.
- 32 MiB 이상, driver dedicated 요구, pool 생성/바인드 실패는 DX12 committed
  resource 또는 Vulkan dedicated allocation으로 fallback한다.
- mesh가 은퇴할 때는 resource-table handle을 무효화하고 allocation 소유권을
  completion retire queue로 옮긴다. 완료 직전에는 block을 보존하고, 도달한
  뒤 native resource 파괴·block 병합·key당 standby 1장 trim을 순서대로 한다.
- 메모리 압박 시 live resource move/compact는 하지 않는다. 빈 segment trim과
  dedicated fallback을 쓰며, relocation은 새 resource/copy/version 게시/옛 completion
  대기가 포함된 명시적 트랜잭션으로만 후속 검토한다.

VS18/v145 Debug x64 RenderEngine과 전체 솔루션은 모두 경고 0, 오류 0으로
빌드됐다. `rhi.uploadsegments`의 공통/DX12/Vulkan persistent heap 검증과
Vulkan validation은 모두 통과했다. `scene.glb` 25개 mesh의 21,626,436 B를
DX12는 64 MiB segment 1장의 placed allocation 50개(23,658,496 B), Vulkan은
segment 1장의 bound allocation 50개(21,626,944 B)에 배치했다. 회수 후는
양쪽 모두 할당 0 B, empty standby 1장으로 복귀했고, Vulkan indexed draw는
94,308 index · green 105 · blue 3,991 · validation 0을 유지했다.

### 0.14 완료 — texture compatibility pool + device-local budget ✔ (2026-08-15)

양쪽 asset texture cache를 persistent heap에 연결했다. DX12는 buffer heap과
`ALLOW_ONLY_NON_RT_DS_TEXTURES` heap을 분리하고 sampled texture를
`CreatePlacedResource`로 만든다. Vulkan은 memory type만 공유 키로 쓰지 않고
buffer와 optimal image class를 분리해 `vkBindImageMemory`로 서브할당한다.

resource-table handle은 외부 소유로 등록하며, retire/Abort/Shutdown 모두 핸들을
먼저 무효화하고 native view/image/resource를 파괴한 뒤 block을 반환한다. 실제
성장 예산은 DX12 `QueryVideoMemoryInfo(DXGI_MEMORY_SEGMENT_GROUP_LOCAL)`와 Vulkan
`VK_EXT_memory_budget`에서 읽고, 확장이 없는 Vulkan만 heap size 추정치를 쓴다.
90% 진입/80% 해제 압력에서는 empty segment를 즉시 trim하고 새 64 MiB segment
대신 exact-size committed/dedicated fallback을 사용한다. live resource의 자동
move/compact는 하지 않는다.

실경로 검증은 `dx12.gizmoicon`과 `vk.gizmoicon`을 같은 프로세스에서 실행했다.
실제 `CameraGizmo.png`의 중심/투명/외부 R은 양쪽 `0.500/0.000/0.000`, 점등은
1,346으로 일치했다. DX12 texture pool은 segment 1장과 pooled/dedicated `2/0`
(흰 폴백+아이콘), Vulkan은 `1/0`이었고 양쪽 모두 allocated 0.1 MiB / 실제
device-local budget 11,228.0 MiB를 보고했다. Vulkan validation은 0건이다.
`rhi.uploadsegments`의 buffer/texture compatibility, DXGI/Vulkan budget,
pressure fallback 검사도 재통과했다. VS18/v145 Debug x64 전체 솔루션은 오류
0으로 빌드됐고 기존 `/DELAYLOAD:vulkan-1.dll` LNK4229 2건만 남았다.

### 0.15 완료 — device-scoped memory budget coordinator ✔ (2026-08-15)

allocator별로 native budget을 따로 조회하고 판단하던 경계를 device 단위로 올렸다.
`RHIDeviceMemoryBudgetCoordinator`는 DX12의 LOCAL domain 하나와 Vulkan의
device-local memory heap별 domain을 관리한다. DeviceResources가 초기화와 각
`BeginFrame`에서 native snapshot을 한 번 게시하고, buffer/texture persistent heap은
owner로 등록되어 같은 snapshot과 성장 한도를 공유한다.

- 새 segment는 생성 전에 growth ticket을 예약하고 성공 시 commit, 실패 시 cancel한다.
  따라서 같은 프레임에 여러 allocator가 성장해도 reserved/committed 합계로 판정한다.
- ticket을 우회하는 committed/dedicated fallback도 allocation/release를 기록해 다음
  snapshot 전까지 다른 allocator의 성장 판단에 포함한다.
- 실제 budget은 90% 진입·80% 해제 pressure hysteresis에 사용한다. Vulkan 확장이
  없어 heap size만 아는 estimated snapshot은 pressure를 만들지 않으며 성장 한도와
  telemetry에만 사용한다.
- 기존 allocator별 soft budget은 보수적인 local guard로 남기고, device coordinator가
  aggregate hard gate를 담당한다. 메모리 압박 시에도 live resource move/compact는
  하지 않고 empty trim과 dedicated fallback이라는 기존 정책을 유지한다.

공통 계약 테스트는 owner 2개의 합산 ticket 제한, commit/cancel, pressure hysteresis,
estimate 제외를 통과했다. DX12/Vulkan native self-test도 공유 coordinator에 peer
allocator를 붙여 multi-owner 성장을 확인했다. `rhi.uploadsegments`는 scene.glb의
양쪽 persistent mesh 경로와 Vulkan validation clean을 포함해 오류 0으로 끝났고,
`dx12.gizmoicon` + `vk.gizmoicon` 실제 PNG 회귀 테스트도 pooled allocation과
동일 픽셀 결과를 유지했다. VS18/v145 Debug x64 전체 솔루션은 오류 0이며 기존
`/DELAYLOAD:vulkan-1.dll` LNK4229 2건만 남았다.

### 0.16 완료 — memory-pressure 기반 asset eviction ✔ (2026-08-15)

device coordinator의 pressure와 80% 해제선까지의 부족량을 실제 mesh/texture cache
퇴출에 연결했다. 공통 `RHIAssetEvictionPolicy`가 양 backend에 같은 선택 규칙을 준다.

- 정상 상태는 기존 120프레임 미사용 은퇴를 유지한다.
- 실제 budget pressure에서는 3프레임 이상 미사용한 Resident 자산을 LRU 순으로
  선택한다. 같은 last-use면 큰 자산을 먼저 골라 회수 target에 빨리 도달한다.
- target은 pressure domain의 effective usage를 80% 해제선으로 내리는 데 필요한 양과
  최소 64 MiB 중 큰 값이다. 한 프레임의 texture→mesh cache가 같은 pass를 공유하므로
  cache마다 target을 중복 회수하지 않는다.
- 이번 프레임과 최근 2프레임에 사용한 자산, Recording/Queued 업로드는 보호한다.
  퇴출 대상도 resource handle과 allocation을 completion retire queue로 옮기며 GPU가
  마지막 제출을 끝낸 뒤에만 native resource 파괴·free-list 반환·empty trim을 한다.
- 부분 free segment는 이후 성장을 흡수하지만 native budget을 즉시 줄이지는 않는다.
  move/compact 없이 실제 budget을 돌려주는 시점은 segment 전체가 비어 trim될 때다.

공통 계약 테스트는 normal 120f, pressure 3f LRU, recent/upload-pending 보호와 cache 간
target 공유를 검증했다. `rhi.uploadsegments`에서 실제 `scene.glb` 25개 mesh,
21,626,436 B가 DX12/Vulkan 모두 pressure 경로로 퇴출됐고 completion 직전 보존,
도달 뒤 병합/trim, Vulkan validation clean을 통과했다. 실제 PNG 양쪽 회귀 테스트도
동일 픽셀과 pooled allocation을 유지했다. live status에는 pressure pass/퇴출 바이트와
recent·upload-pending 보호 횟수를 추가했다.

### 0.17 완료 — Slice E-a descriptor versioned recycler ✔ (2026-08-15)

고정 frame slot에서 reset하던 transient descriptor 수명을 실제 command recording과
submission completion 단위로 바꿨다. 공통 `RHIDescriptorVersionPolicy`가
Available → Recording → Pending/Quarantined 상태, completion과 slot+generation을
관리하며 DX12와 Vulkan은 네이티브 저장소만 다르게 구현한다.

- DX12는 recording 하나에 shader-visible heap page 하나를 고정한다. 중간 제출 뒤
  즉시 새 page로 전환하고 encoder가 새 heap을 다시 bind한다. `RHIBindingTable`에
  page version token을 실어 이전 page의 stale GPU handle을 거부한다.
- Vulkan은 recording 하나에 descriptor pool version 하나를 점유하고 활성 pool에서
  descriptor set을 지연 할당한다. CPU binding request에도 epoch를 실어 이전 frame의
  요청 슬롯이 현재 레이아웃에서 resolve되는 것을 막는다.
- 제출 성공은 version을 completion에 매달고, Abort는 미제출 version을 즉시 반환한다.
  completion을 발급받지 못한 제출은 quarantine하며 teardown 전에는 재사용하지 않는다.
  모든 초기 version이 pending이면 인플라이트 저장소를 reset하지 않고 성장한다.

`dx12.descriptorheap` 6/6은 completion/Abort/quarantine/generation, 중간 제출 격리,
완료 뒤 재사용, overflow와 sampler dedupe를 통과했다. `vk.selftest`는 대기 없는
flush 3회 뒤 네 번째 pool version 성장과 실제 descriptor set draw, `WaitForGpu` 뒤
4개 전부 회수, validation 0을 확인했다. `dx12.parallel`도 upload 2,048건과
descriptor 1,024건 겹침 0, 순차/병렬 byte·pixel 차이 0을 유지했다.

### 0.18 완료 — G-2 RenderGraph 배리어 완전 중립화 ✔ (2026-08-15)

그래프의 계획과 기록에서 `D3D12_RESOURCE_BARRIER`와 `ToD3D12`를 제거했다.
공통 `RHIBarrierBatch`가 texture transition, buffer transition, texture UAV,
buffer UAV 네 span을 들고, `RHIEncoder::ResourceBarriers`가 현재 command target에
한 번에 기록한다.

- DX12 encoder는 네 부류를 한 `ResourceBarrier` 배열로 변환한다.
- Vulkan encoder는 image/buffer barrier를 한 `VkDependencyInfo`에 넣어
  `vkCmdPipelineBarrier2` 한 번으로 기록하고 image layout 장부도 함께 갱신한다.
- 순차 `Execute`와 DX12 병렬 `ExecuteParallel`은 같은 `RecordPassBarriers`를
  호출한다. 병렬 경로에 있던 DX12 배리어 재조립 코드는 사라졌다.
- `ImportBuffer`는 중립 생성자에서도 texture와 같은 상태 추적/UAV 순서 계약을
  쓰며, `GetPassBarrierCount`도 네 부류를 모두 센다.

Vulkan의 shader stage/access mask는 백엔드가 `RHIResourceState`에서 안전한 범위로
해석한다. 공용 패스 실측상 별도 DX12/Vulkan stage 어휘는 필요하지 않았으며,
더 좁은 mask는 정확성 계약과 분리된 성능 최적화다. 이 시점의 남은 DX12 그래프
접점은 command pool/profiler의 G-3와 원시 external texture import의 R6였다.

검증은 Debug x64 전체 솔루션 빌드 후 수행했다. `dx12.rendergraph` 7/7은 중립
buffer transition/UAV 유도 1/1/1과 texture 실제 실행 픽셀 불일치 0을,
`dx12.parallel` 4/4는 순차/병렬 byte 차이 0과 픽셀 불일치 0을 확인했다.
`vk.forward`·`vk.ssao`·`vk.ssgi`는 각각 buffer와 image transition/UAV 경로를
실제로 실행했고 모두 미구현 0 · Vulkan validation 0으로 통과했다.

### 0.19 완료 — G-3 RenderGraph 병렬 기록 중립화 ✔ (2026-08-15)

`EnhancedRenderGraph`에서 `DX12CommandListPool`, `DX12GpuProfiler`, 원시 command
list를 제거했다. 공통 `IRHIParallelCommandPool`은 Prepare → worker open →
pass별 encoder → close → ordered submit만 표현하고, `IRHIGpuProfiler`는
encoder에 pass begin/end timestamp를 기록하는 최소 계약만 둔다.

- `Prepare`가 기존 immediate upload/copy를 먼저 제출한다. 호출부가 수동
  `FlushCommandList`를 빼먹어 worker command가 upload보다 먼저 실행되는 경우를
  구조적으로 막는다.
- DX12 pool은 기존 allocator/list와 지속 worker thread를 유지하면서 공통 계약을
  구현하고, profiler는 중립 encoder를 DX12 timestamp query로 연결한다.
- Vulkan pool은 frame×worker마다 독립 `VkCommandPool`/primary command buffer를
  소유한다. worker별 pool만 기록하고 owner thread가 reset/end하며, 선언 순서의
  command buffer 배열을 한 `vkQueueSubmit2`로 제출해 timeline completion에 묶는다.
- 병렬 pass가 실제로 갱신하는 Vulkan binding request와 render-target 표를
  동시 접근 안전하게 만들었다. binding payload는 별도 할당해 표가 성장해도
  encoder가 받은 주소가 무효화되지 않는다.

`vk.parallel` 4/4는 순차와 worker 4/command buffer 4 병렬 기록을 같은 그래프로
실행해 byte 단위 픽셀 일치, 최종 색 일치, 미구현 0, Vulkan validation 0을 확인했다.
`dx12.parallel` 4/4도 upload 2,048건·descriptor 1,024건 겹침 0, 순차/병렬 byte
차이 0과 픽셀 불일치 0을 유지했다. `vk.gbuffer`·`vk.forward`·`vk.ssgi` 회귀도
validation 0으로 통과했다. 이제 그래프에 남은 DX12 접점은 R6의 원시 external
texture import 하나다.

### 0.2.1 완료 경위 — 5번의 단계들

| 단계 | 상태 | 무엇 |
|---|---|---|
| **5a** | ✔ (`d74fc2c3`) | 중립 값 타입을 `RHI/` 로 — `RHIResourceTypes.h` 신설 · `RHIEncoder.h` 이동 · `RHIReadback::buffer` 핸들화 |
| **5b** | ✔ | `RHIRenderTargetBinding` 중립화 — 실측: 패스 10곳이 `IsValid()` 만 읽으므로 **불투명 값 + 개수**로 족하다(A-5b 와 같은 정정 — "뷰 목록" 모델은 Vulkan 백엔드 *안쪽*의 것) |
| **G-2a** | ✔ | 그래프가 `IRenderDeviceServices&` 를 든다 — **새 어휘가 필요 없었다**(아래 ★★). 병렬 실행도 G-3에서 중립 pool/profiler 계약으로 내려갔다 |
| **5c-1** | ✔ | `DescribeTexture(handle) → RHITextureInfo` — 패스가 포인터로 풀어 `GetDesc()` 를 읽던 셋(SSGI 2 · PostChain 1) 소멸 |
| **5c-2** | ✔ | IBL 생성기가 `DX12DeviceResources*` 를 든다 — 인터페이스 경유 DX12 호출 9 → 0 |
| **5c-3** | ✔ | `IRenderDeviceServices` 에서 **DX12 반환형 12개 하강** — 남은 15가 전부 중립이다. `ImportBuffer` 로 Forward+ 2건도 그래프 안쪽으로 |
| **5c-4a** | ✔ | `VulkanResourceTable` — 핸들 → {`VkImage`,`VkDeviceMemory`,`VkImageView`}. 칸이 크기·포맷·레이아웃도 든다(Vulkan 은 되물을 방법이 없다) |
| **5c-4b** | ✔ | `VulkanEncoder : RHIEncoder` — **24 중 8 실물 · 16 계수**. 베낀 열거 둘 소멸. `vk.selftest` 통과(검증 레이어 클린) |
| **5c-4c** | ✔ | `VulkanDeviceResources : IRenderDeviceServices` — **15 중 7 실물 · 8 계수**. 선행 이동: 인터페이스를 `RHI/IRenderDeviceServices.h` 로(같은 모양 **세 번째** — 아래 ★). 인코더 렌더 타깃 셋이 실물로. `vk.selftest [5/6]` 신설 — **계약으로만 부르고 미구현 계수 0 을 판정에 넣는다** |
| **5c-4d** | ✔ | 업로드 링 · 디스크립터 풀 · **상수 버퍼가 픽셀로 확인됐다**. `vk.selftest [5/6]` 이 중립 경로로 그리고 초록 255 를 읽는다 — 디스크립터가 안 걸리면 검은 삼각형이므로 픽셀 하나가 판정이다. 테이블 둘은 **소비자가 없어서** 슬라이스 7 로 |
| **5d** | ✔ | `vk.grid` — 패스 무변경 · 픽셀 편차 0.0% · 검증 레이어 클린. 실측 결함 셋(§0.2 표)은 전부 패스 밖에서 닫혔다 |
| 마무리 | ✔ | 골격 비계 소멸 — `VulkanTrianglePass`·`VulkanFrameContext` 삭제(922줄), 인코더의 백엔드 전용 공개 표면 제거(렌더 타깃 셋은 **내려갔다** — 중립 오버라이드의 구현 본체), `vk.selftest` 6단계 → 4단계(전부 계약 경유 — 원시 Vulkan 은 핸들 없는 백버퍼 전이 하나), 호출자 0 이 된 원시 게터 넷 삭제. **비계가 재던 것을 본채가 잰다**: b0 도달은 [3/4]과 vk.grid 픽셀이, t·s 규약은 슬라이스 7 의 첫 텍스처 패스가 같은 방식으로 잰다 — 그때까지 t·s 시프트는 검증 소비자가 없다(공백을 세어 둔다) |

그리드를 고른 근거: 패스 17종 중 가장 얇고(경계 호출 19건 · **DX12 심볼 0**),
`dx12.grid` 가 이미 리드백 픽셀 판정을 낸다 — 대조할 자가 서 있다.

★★ **그리드까지 남은 것을 셌다 (5c-4c 실측).** 슬라이스의 경계를 감으로
정하지 않으려고 **소비자가 실제로 부르는 것**을 세었고, 그 수가 작다:

| 부르는 쪽 | 무엇 | 상태 |
|---|---|---|
| 중립 그래프 | `CreateTexture` `ReleaseTexture` `TransitionResources` `GetImmediateEncoder` | ✔ 5c-4c |
| 그리드 패스 (서비스) | `CreateRenderTargets` | ✔ 5c-4c |
| 〃 | `UploadConstants` | ✔ 5c-4d |
| 그리드 패스 (인코더 8) | 뷰포트·파이프라인·토폴로지·`Draw` | ✔ 5c-4b |
| 〃 | 렌더 타깃 걸기·색 클리어·깊이 클리어 | ✔ 5c-4c |
| 〃 | `SetConstantBuffer` | ✔ 5c-4d |

**전부 섰다.** 5d 에 남은 것은 "패스를 한 줄도 안 고치고 돌려 본다"뿐이다 —
세어 두지 않았으면 5c-4d 의 분량을 "업로드 링과 디스크립터 풀을 짓는다"로
크게 잡았을 자리이고, 실제로 필요했던 것은 그 둘 + **슬롯 번호표** 하나였다.

★ **같은 모양이 세 번째다 (5c-4c).** `IRenderDeviceServices` 는 5c-3 이 끝난
시점에 이미 15개 전부 중립이었는데, 선언이 `d3d12.h` 를 무는 헤더 안에 있어
Vulkan 이 상속을 못 했다. A-1b(`IRenderPipelineCache`) · 5a(값 타입 10종 +
`RHIEncoder`) 와 **진단이 글자까지 같다.**

A-1b 가 그 진단을 정확히 적어 놓고 자기가 만지던 헤더 하나만 옮겼고, 그래서
5a 가 같은 일을 다시 했고, 여기서 또 했다. §4.1 의 "원인을 적을 때는 그
원인이 설명하는 **범위**도 함께 적는다"가 세 번째로 값을 치른 자리라,
이번에는 범위를 코드에 적어 두었다:

> `RHI/DX12/` 에 남아도 되는 선언은 **서명에 DX12 타입이 실제로 있는 것**뿐이다.
> GizmoIcon 슬라이스가 `IRenderTextureCache`까지 `RHI/`로 올려 이 경계 조건을
> 만족하는 선언은 0이 됐다. `RenderFrameServices.h`는 이제 DX12 구현 헤더를
> 묶는 호환 파사드일 뿐이며, 삭제는 경계 선결이 아니라 후속 정리다.

`RenderFrameServices.h` 341 → **104줄**. 소비처 6곳은 한 줄도 안 바뀌었다.

★ **5c-4c 가 실측한 백엔드 비대칭 여섯** (전부 계약은 안 바꾸고 흡수했다):

| 무엇 | DX12 | Vulkan | 처분 |
|---|---|---|---|
| `Common` 상태 | 어느 쪽이든 `COMMON` | `before` 면 `UNDEFINED` · `after` 면 `GENERAL` | 백엔드가 `isSource` 로 가른다 |
| 큐브 | **뷰**의 성질 | **이미지 생성 플래그** | 배열 6배수면 호환 플래그. 정답은 IBL(슬라이스 7)이 답한다 |
| 버퍼 용도 | 뷰가 정한다 | 생성 시 요구 | 전부 켠다 — 느리지만 안 틀린다 |
| 버퍼 초기 상태 | 생성 인자 | 개념 없음(레이아웃이 없다) | 무시. `clearColor` 와 같은 부류 |
| 깊이 읽기 전용 | DSV **플래그** | 렌더링 시작 **레이아웃** | 묶음이 들고 인코더가 고른다 |
| 렌더 타깃 뷰 | 프레임 힙 쓰기 = 정상 비용 | `VkImageView` = **객체 생성** | 기본 뷰를 빌리고 부분 뷰만 만든다 |

★ **타깃 포맷은 안 들었다.** 넣을 뻔했고 안 넣은 것이 맞다 —
`VkRenderingAttachmentInfo` 에 포맷 칸이 없고, 동적 렌더링에서 타깃 포맷은
**파이프라인**이 든다(DX12 의 PSO `RTVFormats` 와 같다). 두 API 가 **일치하는**
자리를 백엔드 구조체에 중복하면 파이프라인이 구운 값과 어긋날 자리가 생긴다.

★ **5c-4d 가 실측한 것 — 계약의 `slot` 은 DX12 어휘였다.**
`SetConstantBuffer(bindPoint, slot, slice)` 의 `slot` 은
`SetGraphicsRootConstantBufferView` 의 첫 인자, 즉 **루트 파라미터 번호**다.
Vulkan 에 그 번호가 없다 — 디스크립터는 binding 번호로 걸리고 그 값은
`VulkanBindingModel` 의 구간 + 셰이더 레지스터다. 그래서 레이아웃마다
**"몇 번째 칸이 어느 binding 인가" 번호표**가 선다(`VulkanLayoutSlot`).

계약을 고칠 일은 아니다: `slot` 을 "레이아웃에서 몇 번째로 선언한 것인가"로
읽으면 두 백엔드가 모두 답할 수 있고, DX12 는 그 답이 항등이라 표가 필요
없을 뿐이다. **한쪽 API 의 모양을 물려받은 이름이 다른 쪽에서 번역을
요구하는 자리**이고, 지금까지의 비대칭 여섯과 같은 부류로 흡수했다.

★ **거는 시점이 갈린다 — 계약은 그대로, 소비 시점만 늦춘다.** DX12 는
`Set*` 하나가 곧 루트 파라미터 하나라 즉시 기록되지만, Vulkan 은
`VulkanBindingModel` 이 정한 대로 **셋이 하나**다. 슬롯마다 따로 걸면 뒤엣것이
앞엣것을 덮는다. 그래서 인코더가 쌓아 두었다가 **드로우·디스패치 직전에 한 번
묶어 건다**(`FlushDescriptors`). `SetVertexBuffer` 의 보폭이 "같은 인자가 두
백엔드에서 다른 시점에 소비된다"였던 것의 두 번째다.

★ **정렬은 넓힌다, 좁히지 않는다.** `AllocateUpload` 가 정렬을 인자로 받는
것은 DX12 의 상수 256·복사원 512 가 **고정 상수**이기 때문인데, Vulkan 의
`minUniformBufferOffsetAlignment` 는 **디바이스 속성**이라 기계마다 갈린다.
링이 호출부의 값과 디바이스 요구의 최댓값을 쓴다 — 그대로 쓰면 이 기계에서는
지나가고 정렬이 더 큰 기계에서 처음 터진다.

★ **진입점 이름도 갈렸다(검증 레이어가 짚었다).** DX12 는 진입점을 컴파일할
때 고르고 그 뒤 블롭은 이름을 모른다. SPIR-V 는 `OpEntryPoint` 로 이름을 들고
다니고 파이프라인 생성이 그 이름을 **다시** 요구한다. `RHIGraphicsPipelineDesc`
에 진입점 칸이 없는 것이 DX12 모델을 물려받은 결과인데, 칸을 더하는 대신
**굽는 쪽이 이름을 규약으로 맞춘다**(`-fspv-entrypoint-name`) — 소비자가
하나뿐인 채로 계약을 넓히지 않는다.

★ **검사가 없으면 이 슬라이스는 죽은 코드다.** `vk.selftest [1~4/6]` 은 전부
Vulkan 구체 타입을 손에 들고 돌아서 "Vulkan 이 돈다"를 잴 뿐 "**같은 계약으로**
돈다"를 재지 않는다. 그래서 `[5/6]` 은 `IRenderDeviceServices&` 와 `RHIEncoder&`
로만 부르고(구체 타입은 프레임 여닫이와 `EndRenderTargets` 둘뿐 — 둘 다 계약
밖인 이유가 적혀 있다), **미구현 계수 0** 을 판정에 넣는다. 그 대조가 곧바로
값을 했다: 전이가 옮긴 레이아웃(`DEPTH_STENCIL_ATTACHMENT_OPTIMAL`)과 렌더링이
선언한 레이아웃(`DEPTH_ATTACHMENT_OPTIMAL`)이 달라 검증 레이어가 잡았다 —
**어휘를 두 벌 쓰면 안 된다**가 실측으로 확인됐다.

5c-4d 가 그 검사를 **픽셀까지** 밀었다. `[5/6]` 이 `Cbv(0)` 하나짜리 파이프라인
(그리드의 레이아웃과 글자 그대로 같다)으로 삼각형을 그리고 초록 255 를 읽는다
— **디스크립터가 안 걸리면 상수가 0 이라 검은 삼각형**이므로 픽셀 하나가
"업로드 링 → 슬라이스 → 디스크립터 셋 → 번호 번역 → 셰이더" 전체를 판정한다.
V8-a 가 상수 경로를 잰 자와 같은 자다.

이 단계를 짓는 동안 실패가 셋 났고 **셋 다 검사가 잡았다**: 진입점 이름
(검증 레이어) · 깊이 포맷 불일치(〃) · 비유니티 include 18건. 마지막 것은
유니티 빌드가 통과시켰다 — 중립화 커밋마다 비유니티를 도는 이유다.

★ 실패할 때 **검증 레이어가 한 말을 함께 낸다**(5c-4d 에서 더했다). 없으면
`VkResult -13`(UNKNOWN) 같은 답만 남는데 그 값은 원인을 하나도 말해 주지
않는다 — 레이어는 이미 정확히 알고 있었고 그 말을 버리고 있었다.

★ **5c 실측과 설계 결정 (2026-08-11).** `RHIEncoder` 순수 가상 **24** 중
`VulkanEncoder` 가 가진 것 **8**, 그리고 **그리드가 쓰는 8이 정확히 그 8이다**
— 없는 것이 아니라 **서명만 달랐고**, 그 차이 셋 중 둘을 5a·5b 가 이미
없앴다(`RHIBindPoint` 가 `RHI/` 로 · 렌더 타깃이 불투명 값으로). 남은 하나는
`SetConstantBuffer` 가 `VkDescriptorSet` 대신 `RHIBufferSlice` 를 받는 것.

상속하려면 나머지 **16**(리드백 복사 4 · 디스패치 · 인덱스 드로우 ·
정점/인덱스 · 바인딩/샘플러 · UAV 배리어 · 복사 3 …)을 채워야 하는데,
지금 그것을 부르는 패스는 Vulkan 을 타지 않으므로 **도달 불가**다. §1.1 이
"상속하면 열넷을 '못 한다'로 채워야 하고 그러면 계약이 *부를 수는 있지만
죽는다* 가 된다"고 경고한 자리이자, T4 가 "도달할 수 없는 경로는 죽었는지
살았는지 알 수 없다"고 적은 자리다.

★★ **G-2a 를 5c 앞으로 당긴다 (2026-08-11 실측).** 그리드는 `Declare` 가
`EnhancedRenderGraph&` 를 받으므로 **그래프를 타야만 돈다.** 그런데 그래프가
`DX12DeviceResources&` 를 든다 — 즉 그래프가 중립이 되기 전에는 5d 가
성립하지 않는다. **V7 때와 같은 부류의 순서 오판**이고, 이번에는 계획서가
이미 답을 적어 두었다:

> §8.3 ② — 내용을 보면 셋 다 이미 인터페이스가 있는 일이다. **그래프만 그
> 인터페이스를 안 쓰고 손으로 한다.** 이대로면 Vulkan 은 렌더 그래프 없이
> 패스를 돌려야 하고, 그것은 성립하지 않는다.

**실측하니 새 어휘가 하나도 필요 없다.** 그래프가 서비스에게 부르는 것은
여섯이고 **다섯이 이미 인터페이스에 있다**(`Resolve` · `ReleaseTexture` ·
`CreateTexture` · `RegisterExternalTexture` · `GetCommandList`). 배리어도
마찬가지다 — 전이는 `TransitionResources`(V3, 중립 `RHITransition` 을 받고
**안에서 묶는다**), UAV 배리어는 `encoder.UavBarrier`(A-6, 핸들 span).
인코더는 `GetImmediateEncoder`(A-3). **A-3·A-6·V3 이 만들어 둔 것이 여기서
한꺼번에 청구된다.**

그래서 G-2 를 둘로 가른다 — 그리고 이 갈라짐이 "소비자가 서기 전에 어휘를
정하지 않는다"(§4.1)를 지킨다:

| | 무엇 | 언제 |
|---|---|---|
| **G-2a** | 그래프가 중립 인터페이스를 든다. **기존 어휘만 쓴다** | **지금** — 5c 의 전제 |
| **G-2b** | 배리어 어휘 확장(스테이지·접근 마스크)이 정말 필요한가 | 5d 가 Vulkan 소비자를 세운 뒤 |

**2026-08-15 결론:** Vulkan 공용 패스가 실제 소비자로 선 뒤에도 상위 stage/access
어휘 확장은 필요하지 않았다. `RHIBarrierBatch`와 backend state 변환으로 G-2를
닫았고, 세분화는 필요할 때 계측으로 판단할 최적화로 분리했다(§0.18).

★ 이 지점은 G-2 당시 `ExecuteParallel`이 워커 리스트마다 `DX12Encoder`를 만들고
`GetCommandQueue()`를 쓰던 상태를 기록한 것이다. G-3에서
`IRHIParallelCommandPool`·`IRHIGpuProfiler`를 도입해 해소했다. 이제 중립 생성자로
만든 그래프도 backend pool을 받아 병렬 기록하며, DX12 생성자는 원시 external
texture import(R6) 하나 때문에만 남는다(§0.19).

★★★ **5c 의 진짜 장벽은 인코더가 아니라 `IRenderDeviceServices` 다 (실측).**
순수 가상 **26 중 12 가 DX12 타입을 반환한다** — `GetUploadRing()` 은
`DX12UploadRing&` 이고, Vulkan 은 그런 것이 없으므로 **돌려줄 참조가 없다.**
즉 지금 형태로는 Vulkan 이 이 인터페이스를 구현할 길이 원천적으로 없다.

그런데 **그 12를 인터페이스 경유로 부르는 프로덕션 자리는 20뿐이다**(패스가
`EnhancedFrameContext::resources` 로 받는 자리 — 캐시 둘은 `DX12DeviceResources*`
를 직접 들어서 해당 없음). 소유자별로 가르면:

| 어디 | 건수 | 처분 |
|---|---|---|
| IBL 생성기 | 9 | **5c-2** — 그래프 밖에서 원시 리스트를 쓰는 컴포넌트다. DX12 전용임을 타입으로 말하게 한다(슬라이스 7 까지) |
| SSGI 2 · PostChain 1 | 3 | **5c-1 ✔** — `DescribeTexture` 로 닫혔다 |
| Forward+ 타일 버퍼 | 2 | **G-2 ✔** — `ImportBuffer`와 `RHIBufferTransition`으로 닫혔다. 백엔드가 DX12/Vulkan buffer barrier로 각각 변환한다 |
| 그래프의 원시 `ImportTexture` | — | R6(자가 검증 32) |

★ Forward+ 둘은 Vulkan 소비자가 선 뒤 `ImportBuffer`로 닫았다. 그래프의
`Resource`는 texture/buffer handle을 구분하고, 계획은 중립 transition을 만들며,
네이티브 image/buffer 구조체의 차이는 각 encoder가 흡수한다(§0.18).

**결정: 미구현을 조용한 실패가 아니라 계수로 만든다.** 스텁이
`m_unimplemented` 를 올리고 이름을 남기며, `vk.*` 검사가 **그 수가 0 인가**를
판정에 넣는다. 그러면 부르는 순간 검사가 잡고, 슬라이스 7 이 패스를 하나씩
옮길 때 **무엇이 막는지가 패스별로 자동으로 드러난다** — 5c 가 "컴파일 오류
목록이 남은 작업의 실측"이라고 예상했던 것의 더 정확한 형태다(컴파일은
서명만 보지만 이쪽은 **실제로 부르는 것**만 센다).

### 0.3 남은 순서 (5번 뒤)

| # | 슬라이스 | 비고 |
|---|---|---|
| **G-2 ✔** | 그래프 배리어 모델 | `RHIBarrierBatch`와 backend encoder 변환으로 완료. 순차/병렬 기록이 같은 중립 경로를 쓴다(§0.18) |
| **G-3 ✔** | 병렬 기록 | `IRHIParallelCommandPool`·`IRHIGpuProfiler`로 완료. Vulkan은 frame×worker 독립 command pool을 쓴다(§0.19) |
| **7** | 나머지 패스 7종 + **IBL 생성기** | `SSS` · `SSR` · `VolumetricFog` · `PostChain` · `GizmoLine` · `WireFrame` · `UI`. IBL 이 남은 최대 덩어리(원시 리스트에 직접 건다 — `Resolve` 5 · `RegisterExternalTexture` 4 · `GetDescriptorRing` 3). 패스마다 `vk.*` 대조 |
| **8** | 러너·배선 | `EnhancedSceneRendererLive` 중립화 + `render.backend vulkan` + 로더 없음 폴백(dx12). 에디터가 Vulkan 으로 뜬다 |
| **R6** | 자가 검증의 원시 리소스 → 가짜 백엔드 | 리드백 서비스 4종(`ID3D12GraphicsCommandList*` + `ID3D12Resource*` 인자)이 여기 딸린다 — 스왑체인 백버퍼에 핸들이 없어서 지금은 못 내린다 |

**5번을 기다리지 않는 소소한 것 둘** (5c/5d 에서 필요해지면 그때 만든다 —
지금 만들면 소비자가 DX12 하나뿐인 채로 모양을 정한다):
`DescribeTexture(handle)→RHITextureInfo` (SSGI 2 · PostChain 1 이
`Resolve(h)->GetDesc()` 로 포맷을 되묻는다) · `ImportBuffer(RHIBufferHandle)`
(Forward+ 타일 버퍼 2 이 핸들을 포인터로 풀어 `ImportTexture` 에 우회).

### 0.5 프로덕션 DX12 접촉면 잔량 (2026-08-11 실측)

자가 검증 블록(`EnhancedSceneRenderer::Run*` 이후)과 백엔드 내부를 가른 값:

| 소유자 | 접촉 | 처분 |
|---|---|---|
| **ImGui 셸** (`ImGuiDx12Shell` / `ImGuiVulkanShell`) | 11 | ✔ `IImGuiRendererBackend` 아래 DX12/Vulkan 구현으로 분리 |
| **IBL 생성기** | ~14 | 슬라이스 7 |
| **러너** (`EnhancedSceneRendererLive`) | ~8 | 슬라이스 8 |
| 패스의 `Resolve` 잔여 | 5 | `DescribeTexture` · `ImportBuffer` 로 닫힌다(§0.3) |
| 그래프 | 원시 texture import 1계열 | R6 |

경계 헤더: `RHIEncoder.h` **0** · `RHIResourceTypes.h` **0** ·
`IRenderDeviceServices.h` **0** (5c-4c 신설) · `IRenderTextureCache.h` **0**
(GizmoIcon 슬라이스에서 중립화) · `RenderFrameServices.h` 341 → **104줄**
(DX12 구현 include 호환 파사드; 공용 패스 의존 0) ·
`EnhancedRenderGraph.h` 10.

---

## 1. 측정과 동기 (2026-08-07)

### 1.1 기존 RHI 의 사인 — 이 계획서 전체의 반면교사

구 RHI 층(633줄)은 DX11 즉시 컨텍스트 모델로 만들어졌고, DX11 렌더러
은퇴로 소비자가 0 이 됐다(`Device()`/`Immediate()` 호출 0곳 · 핸들 열 종이
전부 `using = void*`). **소비자 없이 세운 추상은 소비자가 생길 때 맞지
않는다** — DX12(커맨드 리스트 · 명시 배리어 · PSO)가 들어갈 자리가 없었고,
R5 에서 일곱 파일을 지웠다(심볼 사용 0 확인 후).

이 사인이 반복해서 인용되는 이유: **소비자가 하나뿐일 때 계약의 모양을
정하면 안 된다.** V8 을 앞당긴 것, A-5 를 V8-b 뒤로 미룬 것, G-2 를 5번
뒤에 둔 것이 전부 이 규칙이다.

### 1.2 문제의 방향 — DX12 가 새는 게 아니라 상위가 갇혀 있다

`d3d12.h` 를 무는 파일 중 `RHI/DX12/` 밖은 둘뿐이었다. 문제는 반대다:
패스 인터페이스 · 그래프 · 파이프라인 기술 — "무엇을 어떤 순서로
그리는가" — 가 전부 `RHI/DX12/` **안에** 있고 `ID3D12*` 를 직접 썼다.

### 1.3 결합 실측이 슬라이스를 정했다

패스 17종 12,523줄 · DX12 직접 접촉 1,081건. 최대 덩어리는 드로우가
아니라 **뷰 생성 + 디스크립터 바인딩**(뷰 66 · 테이블 33 · 힙 20)이었고,
동시에 가장 조용히 틀리는 자리였다(포맷 불일치 · 링 오버런 — 컴파일되고
검증 레이어도 대개 지나간다). 그래서 R2(바인딩 이관)가 최대 이득이었다.

### 1.4 동기 셋

① 반복 실수 자리를 구조로 없앤다(위 1.3). ② 패스를 테스트 가능하게 —
인코더가 인터페이스면 기록만 받아 적는 가짜 백엔드(R6)가 성립한다.
③ 상위 개념을 백엔드 폴더 밖으로. 2026-08-10 Vulkan 확정으로 **④ 두 API
가 같은 어휘로 들어오는 것**이 기준으로 승격됐다.

---

## 2. 설계 — 지금 서 있는 형태

### 2.1 계층

```
RenderEngine/RHI/                ← 중립 경계 (실물)
  RHIFormat.h  RHIHandle.h  RHIResourceState.h
  RHIPipelineLayout.h  RHIPipelineState.h
  RHIResourceTypes.h             ← 값 타입 10종 (5a 신설 — 바인딩 desc · 테이블 ·
                                    슬라이스 · 타깃 · 리드백 · 메시 바인딩)
  RHIEncoder.h                   ← 커맨드 기록 계약 (5a 에 올라옴 · DX12 심볼 0)
  IRHIDeviceResources.h  IRenderPipelineCache.h  IRenderTextureCache.h

RenderEngine/Render/             ← backend 공용 상위 렌더 계층 (0.9 완료)
  Core/                             live pipeline desc · light packing
  Graph/                            render graph · pass base
  Scene/                            scene renderer · DX12/Vulkan live runner
  Passes/Geometry/                  GBuffer · Decal · Shadow · Deferred · Forward
  Passes/Lighting/                  SkyBox · SSAO · SSGI · SSS · SSR · Fog
  Passes/PostProcess/               PostChain
  Passes/Editor/                    Grid · Gizmo · WireFrame · UI

RenderEngine/RHI/DX12/           ← DX12 backend 구현
  DX12*.{h,cpp}                    디바이스 · 인코더 · 캐시 · 힙 · 표
  EnhancedIBLGenerator.{h,cpp}     아직 DX12 전용인 생성기
  RenderFrameServices.h            DX12 구현 include 호환 파사드
  Tests/{Geometry,...}/             DX12 전용 검사·벤치

RenderEngine/RHI/Vulkan/         ← Vulkan backend 구현 + Tests/
```

### 2.2 경계 인터페이스 7종과 구현 현황

| 인터페이스 | DX12 | Vulkan |
|---|---|---|
| `IRHIDeviceResources` — 디바이스 수명·프레임 경계·프레젠트 | ✔ | ✔ (골격) |
| `IRenderPipelineCache` · `IRenderRootSignatureCache` | ✔ | ✔ (`VulkanPipelineCache`) |
| `RHIEncoder` — 기록 | ✔ `DX12Encoder` | ✔ (24 중 15 실물; GizmoIcon root SRV 포함) |
| `IRenderDeviceServices` — 프레임 서비스(링·바인딩·리드백) | ✔ | ✔ (15 중 13 실물) |
| `IRenderMeshCache` · `IRenderTextureCache` | ✔ | ✔ (`VulkanTextureCache` 실제 PNG 검증) |

핸들은 슬롯+세대 불투명 정수(`DX12ResourceTable`), 파이프라인은
{파이프라인, 레이아웃} 짝으로만 표에 산다(잘못된 조합이 표현 불가능).
바인딩·샘플러 테이블과 렌더 타깃 바인딩은 **백엔드가 뜻을 주는 불투명
값** — DX12 는 힙 핸들/인덱스, Vulkan 은 디스크립터 셋/이미지 뷰 목록.

### 2.3 그래프

알고리즘(컬링·배리어 계획·트랜지언트 풀)은 검증 자산이라 손대지 않는다.
G-1 로 `Compile()`/`Execute()`/`ExecuteParallel(pool,…)` 이 백엔드 인자를
버렸고, 트랜지언트는 `CreateTexture(RHITextureDesc)` 로 만든다. G-2에서
texture/buffer transition과 UAV 순서를 `RHIBarrierBatch`로 묶어 backend encoder가
기록하게 했다. G-3에서 병렬 pool·profiler도 중립 계약으로 내렸다. 남은 DX12는
원시 `ImportTexture`(R6) 하나다.

상태 전이 계획은 그래프의 몫이고 패스 내부의 dispatch 간 순서는 `UavBarrier`가
맡는다. 그래프 밖 한 번짜리 전이는 서비스의 `TransitionResources/Buffers`가
같은 중립 상태 계약을 쓴다.

---

## 3. 완료 대장

상세(착수 전 예상 · 완료 대조 · 발견 경위)는 커밋 메시지와 `d74fc2c3`
이전 판에 있다. 여기서는 한 줄씩 — **"남긴 것"이 있는 슬라이스는 §4 규칙에
승격돼 있다.**

### R축 — 경계 세우기 (08-07 ~ 08-09)

| 슬라이스 | 무엇 | 잔향 |
|---|---|---|
| R1 | `EnhancedFrameContext` 의 구현 타입 5종 → 인터페이스 | 최상위 절단선. "구현 클래스를 아는 상태"가 게터로 되살아난 것을 A-5·A-4 가 마저 걷었다 |
| R2a | SRV/UAV → `CreateBindings` (패스 13종) | `SrvDepth` · `OrNull` — 널 슬롯 구멍 두 곳이 실은 버그였다 |
| R2b | RTV/DSV → `CreateRenderTargets` 계열 (14종 · 힙 21→1) | 검증 목록이 도움말보다 낡아 있었다(26/35 — §4.3). 스냅샷 게시자 부재 발견·수리(`ccca6964` 이후 죽은 층) |
| R2c-a | 패스 소유 리소스 생성 → desc | 프로덕션 `GetDevice` 0 (당시 기준) |
| R2c-b | 리드백 7종 인터페이스 | Map 81 · 피치 계산 63 · 복사 53 → 0. `XxxHalfToFloat` 17종 소멸. R6 의 선결 |
| R3 | `RHIEncoder`/`DX12Encoder` · 패스 17종 이관 | 가상 dispatch 0.9ns 실측 → "드로우 루프 비가상 특별취급" 폐기 |
| R4-1·2 | 렌더 타깃 바인딩·힙 바인딩을 인코더로 · 커맨드 리스트 별칭 0 | setter 미호출로 32종 동시 실패 → **필수 의존은 생성자 인자로**(§4.6) |
| R5 | 구 RHI 7파일 삭제 | 소비자 0 확인 후. §1.1 의 장례 |

### D·T축 — 디바이스 소유권과 자산 잔량 (08-07 ~ 08-08)

| | 무엇 | 상태 |
|---|---|---|
| D1~D3 | `IRHIDeviceResources` 추출 · 스왑체인 DX11→DX12 · 셸 기본 켬 | ✔ |
| D4 | `Utility_Framework/DeviceResources` 은퇴 | 부류 A~E ✔ · **본체 제거 남음** — ImGui DX11 은퇴가 선행. 이 계획과 독립 |
| T2~T6 | `Texture` 의 DX11 표면 제거 (가드 질문 교체 · 죽은 경로 · 캐시 폴백 · 지형 GPU · 멤버) | ✔ 전부. 지형은 "편집 기능이 걸려 있다"는 걱정이 실측(GPU 자원 독자 0) 앞에서 사라졌다 |

### V축 — Vulkan 전제 재편 (08-10 ~ 08-11)

재편의 근거: 표면 742건을 다시 세니 "필수" 분류가 90건이고 "선택"으로
밀어 둔 것이 443건이었다 — 백엔드가 둘이 되는 순간 필수/선택이 뒤집혔다.
**포맷 145건이 어느 슬라이스에도 없었다**는 것이 최대 수확.

| 슬라이스 | 무엇 | 잔향 |
|---|---|---|
| V1 | `RHIFormat` (145) | 모든 뒤 슬라이스의 선행 |
| V2 a·b·c | 리소스 핸들 + `DX12ResourceTable` (슬롯+세대) | "desc 31곳"을 131곳으로 재센 반성 → §4.2 |
| V3 | `RHIResourceState` 를 RHI 로 · `TransitionResources` | A-2 의 미룸 조건을 풀어 줬다 |
| V4 | 루트 시그니처 → `RHIPipelineLayout` (227) | 샘플러 어휘를 실사용 최소집합으로 — V8-b 가 그 덕을 봤다 |
| V5·V6 | 셰이더 소스 파일화 · 파이프라인 기술 중립 | V5 잔여는 `MaterialPipelinePlan`(M1~M3) 승계 |
| V8-a | Vulkan 골격 + 삼각형이 패스 경로를 탄다 | 동적 렌더링 · 타임라인 세마포어 · 동기화 2 · 수동 로더(vulkan-1.lib 링크 안 함 = 폴백 근거) |
| V8-b | 삼각형에 텍스처·샘플러 | 레지스터 시프트 규약(`VulkanBindingModel.h` — 빌드 스크립트가 **읽어 간다**, 두 벌 금지). 픽셀 판정은 최대값·비율로(§4.5) |

### A·G축 — 경계 마감 (08-11)

| 슬라이스 | 무엇 | 잔향 |
|---|---|---|
| A-1 | 파이프라인 짝을 핸들로 (369) | 표는 {PSO, 루트시그} 짝으로만 산다. `Resolve(RHIPipelineHandle)` 이 인터페이스에 오른 것은 IBL 이 원시 리스트를 써서 — 슬라이스 7 에서 내려간다 |
| A-2 | 생성 desc 의 `initialState` 중립 (5) | 미룸 조건 회수의 모범(§4.6) |
| A-5a | 업로드 링 → `RHIBufferSlice` (43) | 성능 측정법 확립(§4.4). `SubRange` 가 성능 계약 — 드로우당 `Allocate`(~175ns) 금지 |
| A-5b | 바인딩·샘플러 테이블 → 불투명 64비트 (11) | V8-b 의 "모델 교체" 진단은 **레이아웃** 이야기였다 — 걸 때 넘기는 것은 양쪽 다 한 값. 자를 세우고서야 보였다 |
| A-6 | 인코더의 배리어·복사·사각형 (4) | `RHIRect` 는 실사용 호출부의 어휘로. `UavBarrier` 는 소비처 0 이어도 중립화(존재 근거가 산다) |
| A-3·A-4 | 즉시 인코더 · `CreateSamplers` · `RHIMeshBinding` | **`RHIEncoder.h` DX12 0.** 죽은 `GetDevice` 선언 5(심볼 세기의 한계 — §4.2). `GetGPUVirtualAddress` 사건 → §4.4·§4.5 |
| G-1 | 그래프 인자 중립화 (호출부 69) | 전부 동어반복 인자였다 — 받는 쪽이 이미 아는 값. `ExecuteContext::Resolve` 를 소멸 조건 충족으로 삭제 |
| G-2 | 그래프 배리어 완전 중립화 | `RHIBarrierBatch`를 DX12/Vulkan encoder가 native barrier 한 번으로 변환. 순차/병렬 기록 경로 통합 |
| G-3 | 그래프 병렬 기록 중립화 | 공통 pool/profiler 계약, Vulkan worker별 command pool, ordered submit와 completion 연결 |
| 5a | 중립 값 타입을 `RHI/` 로 | §0.2. A-1b 가 같은 진단을 하고 하나만 고쳤던 것의 일반화(§4.1) |

---

## 4. 규칙 — 이 저장소가 돈 주고 배운 것

새 슬라이스는 착수 전에 이 절을 훑는다. 각 규칙 끝의 괄호는 그 수업료를
낸 사건이다.

### 4.1 순서와 값 매기기

- **소비자가 서기 전에 계약의 모양을 정하지 않는다.** 소비자 하나로 정한
  모양은 맨 뒤에서야 틀렸다고 드러난다. (구 RHI 사망 · V8 앞당김 · G-2 를
  5번 뒤에 둠)
- **작업의 크기로 값을 매기지 않는다 — 목표로 잰다.** "이동이라 값이
  적다"로 V7 을 맨 뒤에 뒀는데, 실측하면 V7 없이는 공유(5번)가 성립하지
  않았다. (5a)
- **"남은 것"은 말단 경로를 밟아 전 차원으로 센다 — 부분 자로 세면 목록이
  맴돈다.** 서비스·인코더 호출 표로 "남은 것 둘"을 셌는데, 그 자에는 생성
  입력(셰이더)과 검사 배관(리드백)이 안 보였다. 표가 보는 것을 끝내면 표
  밖 차원이 새 항목이 되어, 4항목이 12항목이 되고 10커밋 동안 지표가 0에
  머물렀다. 완료 판정은 말단 검사가 낸다 — 준비물이 남았는지 의심되면
  더 세지 말고 말단을 밟는다. (5 의 맴돎 · 사용자가 두 번 지적했다)
- **호출 표는 include 와 정적 호출을 못 본다.** "그리드는 DX12 심볼 0"이
  오측이었다 — 13행에 `#include "DX12ShaderCompiler.h"` 가 있었다. 접촉면을
  셀 때는 서명·include·정적 호출을 다 센다. (5d)
- **원인을 적을 때는 그 원인이 설명하는 범위도 함께 적는다.** A-1b 가
  "중립인데 DX12 헤더에 갇혀 상속 불가"를 정확히 진단하고 **막힌 헤더
  하나만** 옮겼다. "이 부류가 더 있는가"를 물었으면 V7 이 그때 앞으로
  왔다. 맴돎의 기전이 이것이다. (5a)
- **지표를 재산출한 뒤에도 옛 자로 일하기 쉽다.** 지표를 바꿔 놓고 8커밋을
  전부 옛 지표(심볼 걷기)로 보냈다 — 순서표를 갱신했으면 다음 슬라이스를
  집기 전에 **표를 다시 본다**. (A-6·G-1 을 5번보다 먼저 한 사건)

### 4.2 세는 자

- **접촉면을 셀 때는 "누가 이것을 채우는가"를 함께 묻는다.** 헤더 심볼
  4건의 인자를 채우는 자리가 패스 38곳이었다. (V2-b "desc 31→131" · A-5)
- **"채운 것을 쓰는가"도 묻는다.** `GetDevice` 프로덕션 18건 중 5건이
  죽은 선언이었다 — 심볼 수는 의존 수가 아니다. (A-3)
- **소유자별로 가른다 — "큰 덩어리 셋"으로 뭉뚱그리면 패스 몫이 IBL·러너
  몫에 숨는다.** (A-4 잔여 오분류)
- **프로덕션과 자가 검증을 가른다.** 가르는 자는
  `EnhancedSceneRenderer::Run*` 정의 시작 줄. 안 가르면 17건, 가르면 6건
  — 표가 통째로 틀린다. (08-11 우선순위 재산출)
- **검증 목록은 소스에서 뽑는다.** 도움말은 손으로 유지돼 코드보다
  낡는다(26/35). 검증 목록이 대상보다 낡으면 "통과"가 아무것도 뜻하지
  않는다. (R2b)

### 4.3 검증 하네스

- **전수 스윕**: `Tools/dx12-validation/Invoke-Dx12Suite.ps1` (pwsh 7 전용
  — 5.1 은 한글 인코딩으로 파싱이 무너진다). 검사당 프로세스 하나(모달
  어서션이 뒤를 막는다) · 워밍업 240프레임 · 판정은
  `^\[CLI\] <name> (통과|실패|완료)` 앵커로만.
- **기준선: 28 통과 · 2 실패(설계) · 4 완료 · 1 무판정.** 실패 둘은
  `dx12.scene`(씬에 메시 0 — 리소스 부재)과 `dx12.bench11`(Debug 설계
  거부). 워밍업 0 으로 재면 27 이 나오는데 회귀가 아니라 **다른 자**다
  (`dx12.gizmoscene` 이 에디터 씬의 살아 있는 카메라를 쓴다).
- **성능을 고치는 커밋일수록 전수를 돌린다.** 픽셀도 속도도 안 바뀌는
  부류(검증 레이어 경고)가 있고, 그것이 한 번에 28종을 깬 실적이 있다.
  (A-4 `GetGPUVirtualAddress`)
- **비유니티 점검을 이동·중립화 커밋마다.** 중립화는 언제나 전이 include
  를 끊는다 — 유니티 빌드는 같은 덩어리의 이웃이 물어 줘서 가린다. 90건이
  유니티에서 0 으로 보였다. (5a · V5 잔여 · A-1 잔여)
- **음성 대조**: 디스크립터를 안 걸어도 삼각형은 그려진다 — 끄고 재 봐야
  경로가 산 것을 안다. (V8-b `m_bindTexture`)

### 4.4 성능 측정

- **자가 실물을 재고 있는지부터 확인한다.** `dx12.encoderbench` 의 경로
  셋은 전부 모형이라(원시 뷰를 손에 든다) 실물 인코더를 지나지 않았다 —
  경로 ④(실물)를 더하자 드로우당 +67~81ns 가 즉시 드러났다. (A-4)
- **앞 슬라이스의 자를 그대로 집지 않는다.** `dx12.forwardscale` 은
  드로우가 하나다(광원 스케일링) — 패스당을 재는 자로 드로우당을 잴 수
  없다. (A-4)
- **Release 전용 · 첫 실행은 버린다(워밍업 없이는 2.9배) · 경로 순서를
  회전 · 판정은 절대값이 아니라 대조군**(변경과 무관한 참조 경로가 같은
  값인가). (A-5a)
- 확립된 수: 가상 dispatch ~0.9ns/호출 · 링 `Allocate` ~175ns(원자 연산 —
  드로우 루프 금지, 조각당 한 번) · 실물 인코더 경로 드로우당 38~43ns
  (다섯 호출 · 더 깎으려면 계약을 되돌려야 하므로 여기서 멈춘다 — 드로우
  수천 씬이 생기면 재측정).

### 4.5 조용히 틀리는 부류

- **값이 맞게 돌아와도 불러도 되는 것은 아니다.** 텍스처에
  `GetGPUVirtualAddress` 는 0 을 주지만 검증 레이어 경고를 남기고, 이
  저장소는 WARNING 을 실패로 세므로 검사 28종이 깨졌다. (A-4)
- **COM 호출을 드로우 루프에 두지 않는다.** 주소·설명처럼 리소스 수명
  동안 안정한 값은 표가 등록 시점에 들면 된다(`ResolveGpuAddress`). (A-4)
- **클리어 힌트는 조건부다**: 타깃이 아닌 리소스에 주면 `E_INVALIDARG`,
  타깃인데 안 주면 경고 — 조건을 `CreateTexture` 한 곳에 뒀다. (G-1)
- **`unique_ptr<불완전 타입>` 멤버는 소멸자만으로 모자란다** — 생성 중
  예외 되돌리기가 소멸자를 인스턴스화하므로 **생성자도** `.cpp` 로. (A-3)
- **픽셀 판정은 고정 좌표 절대값으로 하지 않는다.** 체커보드 어두운 칸에
  걸리면 거짓 실패 — 영역 최대값과 인접 비율로 잰다. (V8-b)
- **소비자 없는 출력 = 미완성 패스.** 배선만 이으면 그림이 더 나빠진다.
  (Forward+ 라이브 배선)

### 4.6 미루기와 회수

- **미룰 때는 소멸/재개 조건을 그 자리에 적는다.** 그러면 회수가 "다시
  판단"이 아니라 "충족 확인"이 된다. 실적: A-2(V3 가 조건을 풀었다) ·
  `ExecuteContext::Resolve`(호출자 0 조건 충족으로 삭제) · R2c-b 의 버퍼
  리드백(실사용을 보고 모양 확정).
- **호출자 0 인 표면은 세어 두거나 지운다.** 적어 두지 않으면 다음 사람이
  "쓰이는 것"으로 읽는다. (`Release(RHIPipelineHandle)` — Vulkan 이 오면
  필요해지므로 세어 두고 유지)
- **필수 의존은 setter 가 아니라 생성자 인자로.** 부르는 것을 잊으면
  조용히 잘못 그리는 대신 컴파일이 안 되게. (R4-1 의 32종 동시 실패)

### 4.7 Vulkan 대응

- **한쪽에만 코어인 것은 계약에서 뺀다**(예: `QueryVideoMemory::usedMB`).
  **한쪽에만 최적화인 것은 넣되 무시 가능하게**(예: 클리어 힌트 — Vulkan
  은 렌더패스 시작에 받는다).
- **양쪽이 요구하는 구분은 인터페이스에 남긴다**(바인드 포인트 — Vulkan
  도 `VK_PIPELINE_BIND_POINT_*`).
- **불투명 값의 뜻은 백엔드가 준다**: 테이블 = GPU 핸들 ↔ 디스크립터 셋,
  타깃 = 힙 인덱스 ↔ 이미지 뷰 목록. 계약은 값 하나 + 유효성이면 족하다.
- **레지스터 시프트 규약은 한 벌**: `VulkanBindingModel.h` (b+0 · t+100 ·
  u+200 · s+300) — 빌드 스크립트가 정규식으로 읽어 간다.
- **두 백엔드가 정당하게 다를 수 있는 자리** — 대조에서 여기 밖이 어긋나면
  결함이다 (착수 전에 적어 둬야 "원래 다르다"가 결과 맞춤이 되지 않는다):

| 자리 | 왜 |
|---|---|
| 뷰포트 Y | Vulkan 클립 Y 반전 — 음수 높이로 맞춘다 |
| 깊이 클립 | `VK_EXT_depth_clip_control` 부재 시 동작이 갈린다 |
| 부동소수 마지막 비트 | 드라이버 차이 — `dx12.compare` 의 허용 대역 |
| 밉 필터링·비등방 | 구현 정의 — 대조 셰이더에서 밉 고정 |

---

## 5. 완료 기준

1. `RenderEngine/Render/` 아래 어떤 파일도 `d3d12.h`·`vulkan.h` 를 include
   하지 않는다.
2. 패스 17종 전체(`Initialize` 포함)에 `ID3D12`·`D3D12_`·`DXGI_` 직접 참조
   0건.
3. `dx12.live status` 의 패스 이름 목록이 착수 전과 문자 그대로 같다.
4. 자가 검증 35종 판정이 기준선(§4.3)과 같다.
5. `dx12.live status` 의 CPU ms 가 유의미하게 늘지 않는다.
6. **Vulkan 백엔드가 같은 패스 코드로 그린다** — 5d 의 `vk.grid` 가 첫
   증명이고, 이것이 없으면 1~5 는 전부 "그럴듯한 추상"이다.
7. 경계 인터페이스 7종을 Vulkan 이 전부 구현한다(현황 §2.2).
8. 패스 17종이 두 백엔드에서 같은 픽셀을 낸다 — `vk.*` 가 `dx12.*` 와
   짝을 이룬다.

1~5 는 회귀 방어, 6~8 이 진척이다(§0.1 의 지표 셋과 같은 것).

---

## 6. 이 계획이 넘겨받지 않는 것

- **셰이더 컴파일·퍼뮤테이션** (V5 잔여) → `MaterialPipelinePlan` M1~M3.
  바이트코드 인계 지점은 이미 중립(`const void* + size`)이라 두 축이
  부딪히지 않는다. 유일한 접점 `D3D_SHADER_MACRO` 10건은 M2 의 몫.
- **자가 검증 하네스의 DX12 잔량** (패스 파일 안 51건 등) — DX12 백엔드의
  자기 검사다. Vulkan 은 자기 검사를 따로 갖는다(`vk.selftest` · `vk.grid`).
- **D4 잔여** (`Utility_Framework/DeviceResources` 본체) — ImGui DX11
  은퇴가 선행. 독립 축.
