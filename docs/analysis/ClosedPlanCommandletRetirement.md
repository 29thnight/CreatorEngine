# 닫힌·완료 계획 Commandlet 은퇴 — 2026-09-12

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-cli-service.ps1:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-service.ps1#L23) — `$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-cli-service.ps1:184](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-service.ps1#L184) — `$sw = [Diagnostics.Stopwatch]::StartNew()`
- [Tools/regression/verify-cli-service.ps1:295](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-service.ps1#L295) — `$me  = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value`
- [Editor/EngineEntry/ConsoleCommandSystem.cpp:193](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/ConsoleCommandSystem.cpp#L193) — `stream.handle = ::GetStdHandle(stream.id);`
- [Editor/EngineEntry/ConsoleCommandSystem.cpp:195](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/ConsoleCommandSystem.cpp#L195) — `const DWORD type = ::GetFileType(stream.handle);`
- [Editor/EngineEntry/ConsoleCommandSystem.cpp:203](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/ConsoleCommandSystem.cpp#L203) — `if (nullptr == ::GetConsoleWindow())`
- [Tools/regression/verify-mbc-cutover-freeze.ps1:27](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mbc-cutover-freeze.ps1#L27) — `$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-mbc-cutover-freeze.ps1:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mbc-cutover-freeze.ps1#L38) — `$relative = [IO.Path]::GetRelativePath($repoRoot, $fullPath)`
- [Tools/regression/verify-mbc-cutover-freeze.ps1:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-mbc-cutover-freeze.ps1#L46) — `$body = [IO.File]::ReadAllText($path)`
- [Tools/regression/verify-cli-registry-golden.ps1:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-registry-golden.ps1#L34) — `$Work = [IO.Path]::GetFullPath($Work)`
- [Tools/regression/verify-cli-registry-golden.ps1:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-registry-golden.ps1#L35) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-cli-registry-golden.ps1:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-registry-golden.ps1#L79) — `foreach ($line in [IO.File]::ReadAllLines($Path)) {`
- [Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp:95](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp#L95) — `cls.hInstance = ::GetModuleHandleW(nullptr);`
- [Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp:118](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp#L118) — `bool RunVulkanSelfTest(const std::string& outputPngPath, const std::string& modelPath, const std::string& texturePath, std::string& outLog)`
- [Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp:122](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/RHI/Vulkan/VulkanSelfTest.cpp#L122) — `if (!RenderTest::RunShaderReflectionSelfTest(texturePath, outLog)) return false;`
- [Tools/regression/verify-editor-command-surface.ps1:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-command-surface.ps1#L11) — `if ([string]::IsNullOrWhiteSpace($MaterialScene) -ne [string]::IsNullOrWhiteSpace($MaterialTarget)) { throw 'Provide both MaterialScene and MaterialTarget for the optional real-material Undo leg.' }`
- [Tools/regression/verify-editor-command-surface.ps1:88](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-command-surface.ps1#L88) — `Assert ($proc.WaitForExit(180000)) 'Commandlet scenario must terminate without quit'`
- [Tools/regression/verify-editor-command-surface.ps1:113](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-command-surface.ps1#L113) — `Assert ($results[-1].command -eq 'quit' -and $results[-1].status -eq 'succeeded') `
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/Commandlets/EditorCommandlets.cpp#L26) — `CommandData data = CommandData::Object();`
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp:27](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/Commandlets/EditorCommandlets.cpp#L27) — `CommandData names = CommandData::Array();`
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/Commandlets/EditorCommandlets.cpp#L28) — `for (const auto& entry : entries) names.Append(CommandData::String(entry.name));`
- [Tools/regression/verify-player-cooked-scene-baseline.ps1:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-player-cooked-scene-baseline.ps1#L63) — `$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-player-cooked-scene-baseline.ps1:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-player-cooked-scene-baseline.ps1#L65) — `if ([string]::IsNullOrWhiteSpace($Stage)) {`
- [Tools/regression/verify-player-cooked-scene-baseline.ps1:74](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-player-cooked-scene-baseline.ps1#L74) — `$stagePath = [IO.Path]::GetFullPath($Stage)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

[2026-09-06 1차 정리](CommandletRetirementAudit.md) · [명령 도표](CommandSurfaceTable.md) · [처분표](CommandSurfaceDisposition.tsv) · [보관함](../plans/archive/README.md)

계획이 닫히거나 끝나면 그 계획을 증명하려고 세운 Commandlet 도 함께 끝난다는 판단으로
68개를 은퇴시켰다. 제품 명령은 **한 개도 건드리지 않았다**.

## 1. 기준

둘을 함께 충족할 때만 지웠다.

1. 그 Commandlet 을 낳은 계획이 [보관함](../plans/archive/README.md)에 들어갔거나(19종) PHASE 17
   `SerializationPlan.md` 처럼 본문이 스스로 폐쇄를 선언했다.
2. 활성 계획 문서가 그 이름을 더 이상 부르지 않는다.

두 번째 조건 때문에 같은 보관함 계획 소속이어도 **20개는 남았다**. Mathematics 이주의 픽셀
게이트와 PBR 배선 W9 가 아직 부르는 것들이다: `dx12.forward` · `dx12.forwardshade` ·
`dx12.gbuffer` · `dx12.gizmoscene` · `dx12.post` · `dx12.scene` · `dx12.selftest` ·
`dx12.shadowquality` · `dx12.skinning` · `dx12.ui` · `assets.scenemodel` · `experiment.cooked` ·
`render.livecheck` · `ui.navprobe` · `vk.decal` · `vk.deferred` · `vk.forward` · `vk.gbuffer` ·
`vk.grid` · `vk.shadow`.

seed 표를 거치지 않고 `EditorCommandlets.cpp` 의 손표에 박혀 있는 셋(`experiment.matmigrate` ·
`experiment.matresolve` · `experiment.matscript`)도 남겼다. 소속 계획은 닫혔지만 활성 PBR
계획의 게이트 `verify-pbr-wiring-baseline.ps1` 이 실제로 부른다 — 문서 언급보다 강한 증거다.

## 2. 결과 수치

| 항목 | 이전 | 이후 |
|---|--:|--:|
| descriptor seed | 223 | 155 |
| 등록형 Commandlet | 113 | 45 |
| 손표 Commandlet | 3 | 3 |
| `--commandlet list` discovery | 116 | 48 |
| Editor 제품 명령 | 그대로 | 그대로 |

명령 계층에서 5,596줄이 빠졌다. 핸들러 본문 54개(도메인 TU)와 11개(`ConsoleCommandSystem.cpp`),
그리고 그 핸들러만 쓰던 helper 6개(`PrintSerializationStage` · `PrintSerializationBoot` ·
`SummarizeBenchSamples` · `BenchPercentiles` · `MeasureAnimtickAxisTyped` · `SeedAuthoredMaterial` ·
`FoldPoseDigest`)를 fixpoint 까지 훑어 지웠다.

## 3. 함께 지운 하네스

- 회귀 스크립트 27개. 각각이 은퇴한 Commandlet 하나(또는 한 계열)만 부르던 것들이다.
- 전용 fixture 10개: `reflect_golden.txt`/`.yaml`, `asset_identity_vectors.json`,
  `Generate-AssetIdentityVectors.py`, `mbc11_perf_archive.json`,
  그리고 `scripts/` 의 시나리오 5개.
- `run-all.ps1` 에서 25칸(217줄). 그중 4칸은 Release 조건부 `if/else` 였다.

`mbc0_corpus_baseline.json` 과 `Export-MbcCorpusBaseline.ps1` 은 남겼다 — 살아 있는
`verify-model-corpus-v8*.ps1` 이 아직 그 기준선을 읽는다.

## 4. 수술한 게이트 4개

| 게이트 | 고친 것 |
|---|---|
| `verify-editor-command-surface.ps1` | `--commandlet` 종료 코드 표에서 은퇴한 넷 제거, discovery 단정을 살아 있는 도메인(`experiment.cooked`·`dx12.gbuffer`)으로, 시나리오/배치가 부르던 은퇴 fixture 교체 |
| `verify-cli-service.ps1` | "하네스가 제품 명령으로 새지 않는다" 음성 목록에서 사라진 이름을 살아 있는 이름으로 교체 |
| `verify-mbc-cutover-freeze.ps1` | 은퇴한 probe 를 예외로 적던 주석 정정 |
| `verify-player-cooked-scene-baseline.ps1` | 은퇴한 Editor 벤치를 현재형으로 적던 주석 정정 |

★ `scene.sparseresolver` · `scene.transformpull` · `scene.transformwritestats` 세 **제품** 명령은
남는다. 셋은 인자가 길면 `commandlet.required` 로 거절하며 `*.check` Commandlet 을 가리키고
있었는데, 그 Commandlet 이 사라졌으므로 **없는 명령을 가리키는 오류 메시지**가 됐다. 셋 다
자기 인자를 직접 판정하는 `arguments.invalid` 로 바꿨다.

## 5. 검증과 한계

- Debug·Release `Editor\CreatorEditor` 빌드 exit 0. 기존 LNK4229 경고는 그대로다.
- `commands.selftest` = 명령 105 · 이름 112 · seed 155 · **문제 0**.
- `commands.list` 스냅샷을 `cli_registry.golden.tsv` 와 대조했다. 차이는 **다른 세션이
  아직 커밋하지 않은 `editor.*` 6개뿐**이다 — 이 변경으로 사라진 제품 명령은 0이다.
  그 6개 때문에 골든은 이미 붉었고, 이 변경은 그것을 고치지도 악화시키지도 않았다.
  골든 갱신은 그 세션 몫이므로 건드리지 않았다.
- `--commandlet list` 로 살아 있는 48개를 실물 확인했다.
- ratchet 셋(discovery·exit spine·consumer)은 상한이라 명령이 줄면 초록을 유지한다.
- **run-all 전체는 돌리지 않았다.** HEAD 부터 초록 기준선이 없어 이 변경 전후를 가를
  기준이 없다. 남은 칸이 초록이라고 주장하지 않는다.

### 5.1 뒤에 실측한 것 (2026-09-13)

위에서 "여러 칸이 붉다" 며 든 예 둘을 실제로 재 봤다. **둘 다 이 변경이 남긴 것이었고,
둘 다 고쳤다.**

- `verify-editor-command-surface.ps1` — `experiment.matresolve` 는 은퇴하지 않았다(그 줄은
  지금도 Exit=0 으로 통과한다). 실제로 붉던 자리는 **하나**였고 이 문서 §4 표가 적은 바로 그
  수술이 원인이다: 일반 배치에서 은퇴한 `scene.hierarchymutation probe` 를 뺐는데 그 아래
  개수 단정 둘(`results.Count -eq 5` · `$results[0..3]`)을 따라 고치지 않았다. 이름 셋 +
  `quit` 이라 결과가 넷인데 다섯을 요구하고 있었다. 수를 배치 목록에서 유도하게 바꿨다
  (`$commandletNames`) — 손으로 박아 두면 이름이 빠질 때마다 같은 자리에서 다시 어긋난다.
  마지막 줄이 `quit` 인지도 함께 단정한다(색인 구간이 조용히 짧아지는 것을 막는다).
  **결과: 293 checks 통과.**
- `verify-cli-registry-golden.ps1` — 이 문서가 "`editor.*` 6개 때문에 이미 붉고 갱신은 그
  세션 몫" 이라고 적어 둔 그 칸이다. 실측은 **추가 14 · 삭제 0**(`editor.*` 아홉 +
  `object.icon` · `object.lock` · `scene.navigate` · `script.create` · `script.creation`).
  사라진 제품 명령이 없으므로 골든이 뒤처진 것뿐이라 `-Update` 로 다시 기록했다
  (113 commands · 120 names). **결과: 초록.**

## 6. 남은 일 — C++ 자가검사 구현 41개

Commandlet 을 지워도 그것이 부르던 `Editor/RenderTests/**` 의 구현은 남는다. 이번에 호출자가
0이 된 진입점이 41개다(`RunSSAOTest` · `RunVulkanSelfTest` · `RunAssetIdentitySelfTest` 계열).
이번 범위에 넣지 않은 이유는 하나다 — **살아남은 검사가 같은 파일의 공용 호스트를 쓴다.**
`VulkanSelfTest.cpp` 는 은퇴한 `RunVulkanSelfTest` 와 살아 있는 vk 검사들의 장치 준비를 함께
들고 있고, 파일을 지우려면 `RenderTests.vcxproj`·`.filters`와 전용 HLSL 까지 함께 가야 한다.

별건으로 HEAD 시점에 **이미** 호출자가 0이던 진입점이 17개 더 있다(`RunExperimentWeldSelfTest` ·
`RunShaderReflectionSelfTest` 계열). 이번 변경이 만든 것이 아니다.

</details>
