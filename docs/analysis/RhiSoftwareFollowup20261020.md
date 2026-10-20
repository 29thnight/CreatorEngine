# 소프트웨어 RHI 후속 최적화 — 2026-10-20

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

이 문서는 연구/운영/표현 가이드 성격이므로, 아래 파일은 관련 엔진 경계의 근거다. 문서의 제안 전체가 구현됐다는 증거로 해석하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:78](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L78) — `enum class RGMeasurementDomain : uint8_t { Normal, Capture };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L79) — `enum class RGQueueFallbackReason : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L108) — `enum class RGQueuePlacement : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L34) — `std::free(value);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:43](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L43) — `const auto path = prefix + "." + std::to_string(++serial);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L67) — `std::copy_n(source.data(), source.size(), destination.data());`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L6) — `void EnhancedRenderGraph::RequireQueueIdle() const`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L10) — `throw std::logic_error("Graph is retained by queue recordings; collect completed batches before mutation");`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp#L14) — `bool EnhancedRenderGraph::SubmitQueues(const std::shared_ptr<EnhancedRenderGraph>& self,`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

기준 master `0e9aeccf362b826418a378db3024c147b27eaae6`에서 권장 순서 **1 → 2 → 4 → 5**를 구현했다. 명령 기록 평균 10.34%, 번역 평균 10.10%, Game CPU 중앙값 3.04% 감소를 측정했다. 120 FPS의 8.3333ms CPU 예산은 미달이다. [계측 JSON](RhiSoftwareFollowup20261020.json).

## 구현

1. **기록 작업 그래프 재사용.** RHI topology 변환 배열과 fallback handle 배열을 thread-local 저장소로 유지한다. scheduler는 노드 수·predecessor가 정확히 같은 완료된 enki 그래프를 원자적 슬롯에서 인수한다. 최대 8개, 각 4096노드·16384 dependency까지만 보관한다. 마지막 callback 참조가 해제되면 사용자 캡처와 실패 상태를 비우고 게시한다. 다음 제출은 실패 비트와 진단 시간을 초기화한다. scheduler admission·shutdown 잠금은 기존대로 동작한다.
2. **입력 밀봉의 임시 트리 제거.** geometry identity 검사, temporal 중복 검사와 이전 draw 조회를 정렬된 연속 배열로 바꿨다. 배열 용량은 유지하며 매 호출 map/set 노드를 생성하지 않는다. 이전 draw 포인터는 함수 종료 시 비운다. 이전 순서의 첫 동일 key를 선택하는 규칙, 정확한 mesh 신원, view/reset/topology/pose 조건을 유지한다. fallback bone 배열도 재사용한다.
4. **segment·budget·encoder 재사용.** 프레임마다 지역 recorder를 만드는 제품 경로에서도 전역 저장소 풀을 이용한다. target별 segment 배열의 활성 prefix만 처리하며 recorder에서 batch로 소유권을 이동한다. 마지막 CPU 번역·취소 소비가 끝나면 초기화하고 원자적 슬롯으로 반환한다. 최대 8개·객체/배열 용량 합계 16MiB다. 명령 블록은 기존 비-pmr `RenderArenaPool`을 별도로 사용한다. 재사용 encoder는 현재 validator를 다시 연결한다.
5. **논리적 패스 상태와 네이티브 캐시 분리.** frozen software 번역 풀에서 첫 segment는 전체 native cache를 초기화한다. 이후 segment는 논리적 pipeline 바인딩을 비우고 알려진 root signature·descriptor heaps·PSO 캐시를 유지한다. 같은 PSO의 중복 설정을 생략한다. 각 패스는 유효 pipeline을 직접 바인딩해야 Draw/Dispatch할 수 있다. 일반 native callback과 native list reset은 전체 캐시를 초기화한다.

소유권 이전은 acquire exchange/release CAS를 사용한다. 원자적 슬롯의 포인터 및 크기 atomic은 `is_always_lock_free`로 검사한다. 새 pmr 사용과 프레임마다 생성하는 지역 컨테이너는 추가하지 않았다.

## 측정

CreatorRobot Debug, DX12 software 경로, worker 8, Editor 전경, Scene/Game 각각 1186×514, 동일 입력·카메라 replay다. 워밍업 후 프로파일 ON 15초와 OFF 15초를 분리했고 ETW와 GPU validation은 성능 구간에 사용하지 않았다. ON 구간은 전후 각각 355/371개의 view별 CPU 표본이다. 아래 값은 이번 before/after 실행 쌍에서 얻었다.

| 항목 | 전 | 후 | 변화 |
|---|---:|---:|---:|
| Game CPU p50 | 14.1255ms | 13.6965ms | −3.04% |
| Game CPU p95 | 17.0926ms | 15.9204ms | −6.86% |
| Scene CPU p50 | 14.1158ms | 13.6376ms | −3.39% |
| 명령 기록 평균 | 2.1666ms | 1.9426ms | −10.34% |
| DependencyGraph 구간 평균 | 1.9487ms | 1.7644ms | −9.46% |
| 번역 평균 | 1.9076ms | 1.7150ms | −10.10% |
| InputSeal 평균 | 0.9091ms | 0.9043ms | −0.53% |
| GraphDeclare 평균 | 5.4343ms | 5.2847ms | −2.75% |
| GraphCompile 평균 | 0.8302ms | 0.8263ms | −0.48% |
| GraphReset 평균 | 0.8494ms | 0.8191ms | −3.57% |
| 프로파일 OFF 완료 뷰/초 | 54.58 | 55.86 | +2.35% |

`DependencyGraph` 구간에는 그래프 접수·worker 실행·합류가 포함된다. 전체 구간을 그래프 생성 시간으로 해석하지 않는다. GraphDeclare 코드는 이번에 변경하지 않았으므로 그 감소를 이 변경의 직접 절감으로 배분하지 않는다. InputSeal은 임시 트리 할당을 제거했지만 전체 평균 시간은 거의 같았다. 완료 뷰/초는 두 뷰의 처리량이며 표시 FPS가 아니다.

Game CPU p50은 120 FPS CPU 예산보다 5.3632ms 크다. 현재 큰 잔여 비용은 GraphDeclare 평균 5.2847ms이며, 입력 밀봉의 geometry/pose 생성과 패킷 준비 비용도 남아 있다. 다음 작업은 이 실제 호출 경로의 반복 생성과 재사용 조건을 분석하는 것이다.

## 검증

- 최종 `CreatorEditor.vcxproj` Debug 빌드 통과. GPU 검증을 링크 전에 시작해 발생한 LNK1168은 검증 프로세스 종료 후 다시 링크해 해결했다. 최종 빌드는 `build-linked.log`, 실행은 그 뒤 생성한 `native-linked` 바이너리 신원을 따른다.
- 작업 그래프 Debug/Release 각각 **23,312검사**, mutation gate 통과. 1/4 worker에서 동일 topology 128회 재사용, 실패 전파·독립 노드·다음 성공 실행·캡처 해제·종료 drain을 검사했다.
- 소프트웨어 명령 39메서드 검사 통과. 지연 소유자, arena 재사용, inactive segment 제외, 실제 encoder 주소 재사용 및 validator 교체 거절을 검사했다.
- 입력 밀봉 **302검사**, 기존 malformed fixture 24개 거절. 추가로 이전 draw 순서 변경·정확한 이전 포즈·중복 temporal key·서로 다른 instance·명시적 reset을 검사했다. 전용 `--scene-input-only` 모드로 실행했다.
- DX12 GPU validation 전체 RenderGraph 통과. 비동기 번역·clear/barrier/readback·stale 거절·고정 target·upload rollover·풀 재사용·GPU retirement와 logical pipeline reset을 검사했다. validation 오류 0, 정상 종료.
- 제품 장면 replay와 reload replay의 depth/baseColor/normal/metalRough/emissive/sceneHdr **6개 출력 모두 비트 동일**. 입력·카메라·자산 신원도 일치한다.
- 전후 profiler 모두 complete, unacked/dropped/invalid provenance 0. GPU event 전부 identity 일치. unrelated tracked 변경 10개는 시작 patch와 동일하게 보존했다.

원시 실행 로그·프로파일·출력은 `Artifacts/rhi-followup-20261020/`에 보존한다. 압축된 측정 및 검증 결과는 함께 저장한 JSON에 기록한다. 변경 범위와 수명 계약은 [구현 스펙](../design/RHISoftwareCommandListSpecification.md)을 따른다.

</details>
