# RG2 자원 버전·Modify — 2026-10-03 완료

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:74](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L74) — `enum class RGAccessMode { LegacyState, Read, Write, ReadWrite, AtomicMin64 };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L75) — `enum class RGSchedulingMode { DeclarationOrder, ExplicitSingleWriter, ExplicitVersioned };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:462](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L462) — `bool HasDeclaredWriter(RGHandle resource, const std::string& producer) const;`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:184](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L184) — `RHITextureHandle EnhancedRenderGraph::ExecuteContext::ResolveHandle(RGHandle handle) const`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:555](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L555) — `bool EnhancedRenderGraph::HasDeclaredWriter(RGHandle resource, const std::string& producer) const`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:567](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L567) — `RGHandle EnhancedRenderGraph::Write(RGHandle previous) { return AdvanceVersion(previous, false); }`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

RG3 이후 현행 소스/바이너리 기준선은 [RG3 검증 기록](RenderRg3LifetimeBarriers.md)을 따른다. 아래 내용은 RG2 완료 시점의 계약과 artifact이며 변경 전 증거로 보존한다.

## 구현 계약

`EnhancedRenderGraph`에 `ExplicitVersioned` 모드를 추가했다. 제품 기본값은
`DeclarationOrder`이며 제품 접근 이관과 전환은 RG5/6에서 수행한다.

- `RGHandle`은 기존 index에 version·texture/buffer kind·graph epoch를 추가했다.
  index는 논리 자원 ID다. epoch는 다른 graph 및 Reset 전 핸들을 거부한다.
- import는 외부 내용의 v0을 반환한다. transient CreateTexture는 아직 내용이 없는 선언 핸들을
  반환하며 최초 `Write`가 v0을 발행한다. 이 초기 핸들은 읽기나 Modify에 사용할 수 없다.
- `Write(previous)`와 `Modify(previous)`는 다음 버전 핸들을 발행한다. 실제 패스의 usage는
  출력 핸들과 요구 상태를 함께 쓰며 각각 `Write`, `ReadWrite` 접근을 선언해야 한다.
  Read는 특정 버전 핸들과 `Read` 접근을 선언한다. 이전 버전 읽기는 허용하고 덮어쓰기 전에 정렬한다.
- RAW는 해당 버전 producer→reader, WAW는 이전 producer→다음 writer,
  WAR는 이전 reader→다음 writer다. Modify는 이전 버전의 reader이자 새 버전의 producer다.
  pass 간 중복 edge는 정렬 indegree에서 합치고 진단에는 자원·버전·원인을 보존한다.
- 같은 입력에서 갈라지는 write, 누락/중복 producer, 잘못된 kind/epoch/미발행 버전,
  접근·상태 불일치와 같은 패스의 중복 자원 접근을 자원 할당 전에 거부한다.
  같은 물리 자원의 중복 import도 독립 버전 계보로 취급하지 않고 거부한다.
- 안정적 authored-index Kahn 정렬을 공유한다. 순환 오류는 실제 패스→자원 버전→패스 사슬이다.
  typed diagnostic snapshot에는 usage의 버전·kind·access·state와 RAW/WAR/WAW edge를 기록한다.

버전 API는 texture와 imported buffer에 공통이다. transient buffer 생성은 RG7이며,
현재 culling은 모든 물리 자원 writer를 보존하는 기존의 보수적 방식이다. 버전 producer 기준
정밀 culling·lifetime·barrier 재계산은 RG3에서 닫는다. hot-frame allocator 최적화의
성능 이득은 아직 주장하지 않는다.

## 실행 검증

최종 Debug/Release 빌드와 실행 검증을 모두 통과했다.

- 빌드: Build/rg2-Debug-closure-build.log, Build/rg2-Release-final-build.log.
- Build/Obj/RenderRG2/{Debug,Release}-regression-v1/result.json: 각 구성 독립 프로세스 2개×캡처 2개, graph/변이/계측 게이트 통과, 실패·소스 변경 0, 정상 종료 코드 0, ReplayExtensions=false.
- 각 구성 dx12-0/graph-fixtures.results.jsonl: RG2_DAG_OK shuffles=240 및 RG2_GPU_OK reversed-RAW-WAR-WAW old-version-pixels=0.
- native 순서 검사는 texture와 imported buffer에 각각 120 shuffle을 적용했다. 실제 GPU 검사는 소비·덮어쓰기를 먼저 선언한 뒤 원래 writer를 선언하고, 이전 색의 readback 뒤 다른 색의 texture를 같은 저장소에 복사한다. 이전 버전의 픽셀 불일치가 0이었다.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1의 comparison.json: 각각 16개 이미지 maxError=0, exceededPixels=0. 구성 간 환경·자산·tuning identity도 일치했다.
- current-baseline-phase-complete-v1.json: 현재 소스와 executable/runtime DLL 해시를 재검증한 기준선 완료 증거.
-
g2-completion-v1.json: 위 기준선과 native RG2 marker 및 변경 전후 대조를 함께 판정한 RG2 완료 기록.

RG1 artifact는 변경 전 기준선으로 보존한다. 다음은 RG3다.

실패한 `Debug-native-precheck`는 빌드 도중 추가한 거부 코드가 들어가기 전 바이너리를
실행한 기록이다. 수정된 바이너리의 `Debug-native-precheck-v2`에서는 240 shuffle과
실제 GPU 이전 버전 readback 픽셀 검사 모두 통과했다. 최종 해시 검증 증거와 구분해 보존한다.

IBL 1024/4096과 기존 이미지 상한을 유지한다. MAT-9 품질·성능 게이트는 열려 있고,
Lattice 재생은 선택 확장, Vulkan 비교는 PHASE 4.9다.

</details>
