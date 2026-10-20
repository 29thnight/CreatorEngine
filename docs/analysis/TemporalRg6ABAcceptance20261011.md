# TR0 — 현재 소스 legacy/product RG6 A/B

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/GpuGeometryOcclusion.h:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.h#L11) — `struct EnhancedFrameContext;`
- [Engine/RenderEngine/GpuGeometryOcclusion.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.h#L15) — `class GpuGeometryOcclusion final`
- [Engine/RenderEngine/GpuGeometryOcclusion.h:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.h#L17) — `struct HistoryPool;`
- [Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h:111](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h#L111) — `const auto name = "hdr-" + std::to_string(stages.size()) + "-" + node.name;`
- [Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h:182](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h#L182) — `Authoring::EnsureRymlErrorPolicy();`
- [Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h:196](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h#L196) — `h["fixedSdkClock"] << c4::fmt::boolalpha(historyFixedSdkClock);`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L46) — `bytes += std::uint64_t(width) * height * sizeof(float);`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L53) — `const auto found = std::ranges::find_if(`
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp:119](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L119) — `std::uint32_t GpuGeometryOcclusion::LevelCount(std::uint32_t width, std::uint32_t height)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

SDK 활성 Debug/Release에서 같은 소스의 진단 DeclarationOrder 빌드와 제품
ExplicitVersioned/DependencyOrder 빌드를 각각 독립 두 프로세스로 비교한다.
현재 재현 가능한 작은 authored plane/camera/light fixture를 사용한다.
이전 lattice fixture 원본은 로컬에서 발견되지 않았으므로 동일 workload라고 주장하지 않는다.
정본은 `Artifacts/PR169/tr0-rg6-ab-20261011/final-result.json`이다.

## 수용 계약

각 경로/구성 첫 프로세스 graph capture 100회, 두 번째 2회이며 일반 프레임
비용 표본은 프로세스마다 100개다. 정상 종료, GPU validation enabled/problems 0/dropped 0,
실제 scene Ready, 서로 다른 CPU 측정/GPU 프레임, 원래 명령 응답과 저장 행의 일치를 요구한다.
전체 manifest의 입력/그래프 identity를 감사하고 선택 대표의 프로세스 내/간·A/B·제품 D/R
픽셀을 비교한다. 408캡처 전체 이미지의 모든 쌍을 비교했다는 의미는 아니다.

CPU graph recording과 GPU pass sum은 독립 비동기 축이다. 전체 frame time으로 합산하지 않는다.
제품은 geometry occlusion과 occluder depth 패스를 추가하므로 순수 scheduler overhead 또는
역사적 PR 전체 비용으로 해석하지 않는다. GPU validation을 켠 작은 정적 fixture 결과다.

## 현재 그래프 차이

`GpuGeometryOcclusion::LevelCount`처럼 각 차원을 올림 절반으로 줄여 1×1까지 계산한다.
332×182에서는 9 mip이다. LX-only authored geometry는 traditional occluder depth가 없어
제품 ExplicitVersioned 경로에서 LX.Scene.OccluderDepth prepass/resource가 추가된다.
GizmoIcon.Texture 명시 read 2개와 Cull→CullOcclusion 대체를 별도로 요구한다.

`EnhancedPbrCapture::DeclareStage`는 index/version/kind/epoch가 같은 versioned image만
readback을 공유한다. 비활성 Forward+/Sprite/SSS/SSR/LX.Scene.Volume/Temporal.Upscale의
중복 PBR.Stage 패스는 제거되지만 이름별 diagnostic image와 모든 단계의 픽셀 비교는 유지한다.
수용 도구는 manifest에 증명된 sharedReadback에 대응하는 정확한 패스 차이만 허용한다.
예상 밖 패스/리소스 또는 pixel threshold를 느슨하게 허용하지 않는다.

## 용량 정책

행렬은 순차 실행하고 매 구성 전 실측 여유 및 30GiB floor/최대 추정을 검사한다.
구성 내부 oracle 후 각 독립 프로세스 capture-0 두 개만 A/B용으로 핀한다.
최종 A/B 이후 성공 대표는 경로/구성마다 하나로 축소한다. 로그/manifest/판정/profile은 보존한다.
진단 바이너리는 exe/runtime DLL만 보존하고 전체 배포·PDB 복사본은 만들지 않는다.
raw 삭제 ledger는 역사적 픽셀 재생 불가를 명시하고 실측 free-space 차이를 기록한다.

## 최종 결과

네 SDK 활성 빌드와 네 baseline, 공동 BASE-0 및 RG6 최종 게이트가 모두 통과했다.
독립 live 프로세스 8개 정상 종료, 실제 CPU/GPU 비용 표본 800개, graph capture 408개를 수용했다.
validation 문제/누락은 0이다. 현재 소스 해시·GPU/driver·fixture identity와 모든 manifest가 일치한다.
네 native graph 자가검사도 정상 종료하며 RG4/RG5 픽셀/worker/정책 게이트가 통과했다.

A/B 4건 모두 17개 attachment의 허용 오차 초과 픽셀 0이다. 최종 display 최대 차이는 0이고,
depth는 maxError 0.004901885986328125 / RMSE 0.0006814756696137096 / changed pixels 5,039다.
legacy의 Grid 갱신 후 depth readback과 제품의 버전 지정 GBuffer.Depth readback 차이가 있으며,
기존 absError ≤ 0.002 + 0.005 × max(abs(before),abs(after))를 그대로 적용해 통과했다.
깊이까지 비트 동일하다고 주장하지 않는다. 제품 Debug/Release 17 attachment는 모두 차이 0이다.

실제 저장 population의 15변이 × 8프로세스 = 120개를 거부했다. 입력 변경·예상 밖 패스·픽셀
훼손 3개 comparator 변이도 거부했다. artifact 자체 변이는 legacy 25개/product 29개씩
각 독립 프로세스에서 거부했다(총 216개). 실제 명령 응답 metadata 회귀 46개도 통과했다.
metadata 회귀의 CPU test-zero는 성능 population에 사용하지 않는다.

| 구성/경로 | 일반 CPU record 중앙값 / p95(ms) | 일반 GPU pass sum 중앙값 / p95(ms) |
|---|---:|---:|

| Debug/Reference | 1.62630 / 1.81930 | 3.52198 / 3.83440 |
| Debug/Product | 3.66040 / 4.61540 | 3.63686 / 3.96861 |
| Release/Reference | 1.09585 / 1.39990 | 2.02327 / 3.60925 |
| Release/Product | 2.25195 / 3.01060 | 2.24991 / 3.99610 |

제품 CPU record 중앙값 증가는 Debug +125.1%, Release +105.5%다. 구조·회귀 수용이며
성능 개선 판정이 아니다. mode-dependent geometry delta가 포함돼 순수 scheduler 비용으로
귀속하지 않는다. RG6 JSON에 live pass별 분포, capture compile/record/GPU/queue span/memory를
분리해 보존했다. 제품 전체 frame·master 비교·제품 budget 또는 일반 4K 성능은 범위 밖이다.

각 baseline 내부 판정 후 raw를 정리하면서 legacy 각 약 1.50GiB, 제품 각 약 0.95GiB를
회수했다. 최종 A/B 이후 독립 두 번째 프로세스의 추가 대표 4개도 해제해 약 0.05GiB를 회수했다.
성공 raw 대표 4개(각 경로/구성 process-0 capture-0)와 로그/manifest/profile/판정/입력만 유지한다.
별도 진단 바이너리는 Debug/Release 각각 exe/runtime DLL 한 쌍이며 전체 배포/PDB 복사본은 0개다.
reference는 원래 Bin에서 실행하고 판정용 exe/runtime만 Reference/x64-*/Editor에 보존했다.
해당 사본은 독립 실행 가능한 전체 배포라고 주장하지 않는다.
최종 실측 디스크 여유는 92.37GiB이며 storage-end.json과 삭제 ledger에 기록했다.
역사적 모든 캡처의 픽셀 재생은 불가하며 보존된 대표만 재생 가능하다.

## TR0 잔여

현재 소스의 전체 legacy/product × Debug/Release × 독립 2프로세스 RG6 행렬 수용을 종료한다.
이전 lattice fixture와 동일 workload 또는 모든 자산/재질 조합의 전수 수용은 아니다.
UI 육안/화면, 실제 Player generated 신원, 외부 기구의 표시·지연 및 제품 budget/전체 frame
수용은 남는다. TR0 progress/기성 0, PHASE 4.5 12/71을 유지한다. 기존 RG6 기성을 중복 계상하지 않는다.
이전 BASE-0 checkpoint는 당시 바이너리 증거이고 최신 A/B/source/runtime 정본은 이 실행의 final-result.json이다.

</details>
