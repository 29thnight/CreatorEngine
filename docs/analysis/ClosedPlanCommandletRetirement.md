# 닫힌·완료 계획 Commandlet 은퇴 — 2026-09-12

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
- **run-all 전체는 돌리지 않았다.** HEAD 부터 초록 기준선이 없고
  (`verify-editor-command-surface.ps1` 은 이미 없는 `experiment.matresolve` 를 Exit=0 으로
  기대하는 등 여러 칸이 붉다), 이 변경 전후를 가를 기준이 없다. 남은 칸이 초록이라고
  주장하지 않는다.

## 6. 남은 일 — C++ 자가검사 구현 41개

Commandlet 을 지워도 그것이 부르던 `Editor/RenderTests/**` 의 구현은 남는다. 이번에 호출자가
0이 된 진입점이 41개다(`RunSSAOTest` · `RunVulkanSelfTest` · `RunAssetIdentitySelfTest` 계열).
이번 범위에 넣지 않은 이유는 하나다 — **살아남은 검사가 같은 파일의 공용 호스트를 쓴다.**
`VulkanSelfTest.cpp` 는 은퇴한 `RunVulkanSelfTest` 와 살아 있는 vk 검사들의 장치 준비를 함께
들고 있고, 파일을 지우려면 `RenderTests.vcxproj`·`.filters`와 전용 HLSL 까지 함께 가야 한다.

별건으로 HEAD 시점에 **이미** 호출자가 0이던 진입점이 17개 더 있다(`RunExperimentWeldSelfTest` ·
`RunShaderReflectionSelfTest` 계열). 이번 변경이 만든 것이 아니다.
