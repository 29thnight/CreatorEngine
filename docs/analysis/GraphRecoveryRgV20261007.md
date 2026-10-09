# 그래프 재질 복구·RG-V 수용 — 2026-10-07

## 결과와 계약

Editor 모델 import/load와 씬 재질 로드에서 요청한 그래프의 원본·컴파일·텍스처 바인딩이 실패하면 원본을 보존하고 별도 `Assets/Materials/Recovered_<GUID>.shadergraph`와 identity를 게시한다. 기본 그래프는 **Principled BSDF → Material Output** 두 노드·한 연결이며, 공통 그래프 컴파일·재질 소비 경로에 연결된다. 유효한 모델 geometry는 유지한다. 복구 그래프를 열어 편집·저장·적용할 수 있고 씬 저장·재로드에서도 같은 그래프를 해석한다.

이 정책은 재질 자산 소유자의 복구다. Shadow/GBuffer/Forward의 그래프 전용 admission과 정확한 generation 선택을 유지한다. GPU 프로그램 준비 중에는 기다리며, device/PSO 실패를 성공한 렌더링으로 처리하지 않는다. Player의 cooked closure와 모델 geometry/identity 검증은 엄격하게 유지한다. 기존 authored model graph 또는 sidecar가 있으면 오류를 이유로 원본을 덮어쓰지 않는다.

**RG-V 기본 native C++ viewer는 DX12 Debug/Release 수용 완료**다. C# IR 연결은 PHASE 4.6 CSRP-5/6, alias/queue/subresource range 확장은 RG7~9의 별도 범위다. Vulkan runtime 수용을 이번 결과로 올리지 않는다.

## 구현과 발견한 문제

- immutable compiled snapshot에 view/history/scene epoch/extent를 담는다. reader는 inactive/unready 결과와 다른 씬·뷰·history·크기의 snapshot을 거부한다. Editor 카메라 history가 씬 전환에서 고정되므로 scene epoch 검사가 필요하다.
- Window > RenderPass에서 pass/resource/edge 원인, authored/compiled order, version, culling, lifetime/state/barrier, wave와 critical path를 읽는다. sampled submitted frame을 표시하며 미지원 alias/queue/range를 명시한다. GPU 객체를 소유하지 않는다.
- `render.graph [scene|game|preview]`와 `render.graph view <target>`은 같은 snapshot을 조회하고 실제 UI reader를 선택한다. UI draw/matching/stale 계수로 실제 표시와 창 닫기 후 정지를 관측한다.
- 완료된 Material Preview를 재사용하면 나중의 snapshot 요청이 실행되지 않던 문제를 수정했다. 요청이 있을 때 한 번 새 snapshot을 만들고, 일치하는 preview snapshot은 재사용한다. DX12/Vulkan 두 구현에 적용했다.
- `.shadergraph`가 Editor 등록 확장자에서 누락돼 복구 원본 게시가 거부되던 문제를 수정했다.
- 제거된 fallback 스트림을 전달하던 네 replay fixture 호출을 정리했다. Debug RenderTests의 Unity object section 한도는 `/bigobj`로 해결했다.
- 검증 도구는 native commandlet과 live HTTP 명령을 분리한다. 최초 graph UI fit이 문서 revision을 바꾸므로 편집 전 다시 읽는다. 캡처에서는 정확한 pending-program 응답만 재시도한다. quit의 사라지는 endpoint를 polling하지 않고 최종 process exit code로 판정한다.

## 최종 증거

Artifacts: `Build/Verification/GraphRecoveryRgV20261007/Scene-Debug-final` 및 `Scene-Release-final`.

| 검사 | Debug | Release |
|---|---:|---:|
| 네 프로젝트 빌드: RenderEngine / Editor / RenderTests / CreatorEditor | 통과 | 통과 |
| native 24 permutations + missing-edge/wrong-order/stale scene/history/resize/view + immutable reset retention | 통과 | 통과 |
| 복구 graph 수 / 손상 원본 hash 보존 | 5 / 7개 모두 | 5 / 7개 모두 |
| capture: 전체 / CreatorRobot의 서로 다른 메시 / 편집한 Ground generation 소비 | 5 / 4 / 1 | 5 / 4 / 1 |
| 실제 Scene / Game / 완료 후 늦게 연 Material Preview reader | 통과 | 통과 |
| 같은 뷰·크기에서 씬 epoch 교체 | 4 → 5 | 4 → 5 |
| render extent 변경 | 2246×1094 → 1124×548 | 2246×1094 → 1124×548 |
| sample snapshot 최대 capacity bytes / copy ms | 38,596 / 2.0093 | 34,780 / 0.1750 |
| RenderPass UI p95 ms, 512 samples | 0.5181 | 0.1374 |
| viewer 닫기 후 UI draw 정지 | 통과 | 통과 |
| GPU validation problems / dropped / exit code | 0 / 0 / 0 | 0 / 0 / 0 |

각 `result.json`은 `complete=true`, failures가 비었고 exitCode=0이다. `compiler-binary-hashes.json`은 검사한 launcher/runtime DLL identity를 기록한다. `capture-model-audit.json`은 Robot의 네 mesh ID와 편집한 Ground의 정확한 runtime generation 소비를 추가로 검사한다. 모든 draw는 `lattice` 경로다. 저장·재로드에서 불필요한 추가 복구 graph가 생성되지 않았고 기본 graph의 Roughness 편집은 새 runtime generation으로 반영됐다.

비용 gate는 snapshot copy ≤10 ms, capacity estimate ≤4 MiB, 실제 reader UI p95 ≤10 ms로 검사했다. 측정 최대 UI 시간은 Debug 38.5329 / Release 38.9193 ms였다. 따라서 이 수용을 모든 전환 프레임의 hard 10 ms 상한으로 해석하지 않는다. capacity estimate는 vector/string capacity 기준이며 allocator bookkeeping은 제외한다. 기본 표시 상태의 비용으로, 모든 tree를 펼친 최악 표시 비용을 증명하지 않는다.

## 제외한 시도와 범위

초기 native HTTP 호출, 누락된 `.shadergraph` 게시, 확장자 없는 model 경로, 최초 UI revision, GPU preparation 응답과 quit polling 때문에 끝난 실행은 최종 수용에서 제외했다. 숨김 640×480 창은 canvas가 38×11로 줄어 렌더 크기가 64×64로 clamp됐으므로 그 resize 검사도 제외했다. 마지막 두 실행은 일반 크기의 Editor와 새 scene epoch 검사로 독립 수행했다. fixture 자산의 변경/손상은 검증용 프로젝트 복사본에만 가했다.

C# source 표시·IR 연결, RG7~9 확장과 Vulkan runtime은 기존 해당 페이즈에 남는다. CSM depth-content cache를 구현한 작업은 아니다. 계획 공수는 RG-V 기본 viewer 4인일을 회수하여 전체 355인일 중 기성 106 / 잔여 249(+별도 미산정)로 갱신한다. commit/push는 하지 않았다.

대시보드 집계 대조에서 RND-ENV의 명시적 earnedDays가 없어 progress가 자동 5인일 기성으로 계산되던 기존 불일치도 수정했다. 이 행은 잔여 10인일·미수용 상태이므로 earnedDays=0을 명시한다. PHASE 4 계열 데이터 집계는 원장과 동일한 355 / 106 / 249다.
