# GraphDeclare 선언 저장소 재사용 — 2026-10-20

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/GpuGeometryVisibility.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.h#L13) — `struct EnhancedFrameContext;`
- [Engine/RenderEngine/GpuGeometryVisibility.h:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.h#L22) — `class GpuGeometryVisibility final : private IRHIUploadTransactionListener`
- [Engine/RenderEngine/GpuGeometryVisibility.h:27](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.h#L27) — `enum class MeshletStage : std::uint8_t { Reset, Cull, Count, Finalize, Scatter };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:78](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L78) — `enum class RGMeasurementDomain : uint8_t { Normal, Capture };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L79) — `enum class RGQueueFallbackReason : uint8_t`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L108) — `enum class RGQueuePlacement : uint8_t`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L77) — `error = std::move(message);`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:91](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L91) — `if (!std::isfinite(matrix.m[row][column]))`
- [Engine/RenderEngine/GpuGeometryVisibility.cpp:1427](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/GpuGeometryVisibility.cpp#L1427) — `void GpuGeometryVisibility::Frame::DeclareMeshletBatch(EnhancedRenderGraph& graph,`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L34) — `std::free(value);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:43](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L43) — `const auto path = prefix + "." + std::to_string(++serial);`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L67) — `std::copy_n(source.data(), source.size(), destination.data());`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

기준 master `1162d4f9a088f251d0801683f7d426959e424c11`에서 패스별 누적 비용을 확인하고 기하·컬링·패킷·하이브리드 가시성의 반복 저장소 생성을 줄였다. GraphDeclare 평균은 5.3156→5.0129ms, Game CPU 중앙값은 13.8340→13.4333ms다. [원본 수치·검증 JSON](GraphDeclarationReuse20261020.json).

## 코드 변경

- `ce::render_arena_objects`: 기존 비-pmr `render_arena_resource` 위에 객체 소멸 기록과 연속 배열 복사를 추가했다. 객체 주소는 성장 중 유지된다. 생성에 성공한 객체만 소멸 목록에 등록하고, 배열 복사가 실패하면 구성한 원소를 즉시 역순으로 해제한다. Reset은 객체들을 역순으로 소멸시킨 뒤 블록 오프셋을 되돌린다. 원시 블록은 다음 선언에서 재사용한다. 객체별 해제·동기화 잠금은 없다.
- `EnhancedRenderGraph::RetainDeclaration/CopyDeclaration/StoreExecute`: 그래프가 선언 데이터와 큰 콜백을 소유한다. Reset은 패스 콜백을 먼저 제거한 뒤 선언 데이터를 해제한다. 필드 선언 순서도 그래프 소멸 시 같은 수명을 보장한다. 큐 기록이 그래프를 점유하는 동안은 기존 변경 금지 검사를 적용한다.
- `GpuGeometryVisibility::Frame::DeclareMeshletBatch`: 공유 frame 배열과 shared dispatch 소유자를 선언당 한 번 저장한다. 각 stage callback은 그 데이터의 포인터와 stage 값만 캡처한다.
- `PacketDispatchWork::Declare`: owner·6개 pipeline·입력 바인딩 함수를 한 번 저장한다. 단계별 큰 callback은 그래프 아레나에 저장하며 std::function에는 포인터만 담는다. 각 단계의 사용 목록은 TLS 용량을 재사용하고 그래프가 즉시 복사한다.
- `MeshSurfaceBatch::DeclareBatch`: 매 호출 생성하던 소유자/사용 목록을 선언 깊이별 TLS scratch로 옮겼다. 업로드 합류의 보조 실행으로 선언이 재진입해도 별도 객체를 사용하며, 포인터 배열이 성장해도 외부 선언의 저장소 주소가 유지된다. 완료·실패·예외 모두에서 shared owner와 borrowed pointer를 비운다. GPU 기록 callback의 owner 배열은 그래프 아레나에 한 번 복사한다.
- 기하 producer 중복 검사는 노드별 할당 대신 `ce::render_pointer_set`의 연속 open-addressing 슬롯을 사용한다. 선언 시작 시 최대 원소 수의 두 배 이상을 확보하고 삽입 시 개별 할당을 하지 않는다. 첫 등장 순서로 producer를 발행하는 규칙을 유지한다.
- `SceneHybridVisibility`의 분류·소프트웨어 래스터 사용 목록은 TLS 배열 용량을 재사용한다. native 자원 핸들, RDG 버전, 접근 상태와 패스 순서는 기존대로 선언한다.

이번 변경의 아레나는 그래프별 단일 writer가 기록한다. 프레임별 자원·상수·포즈는 해당 프레임의 값을 사용한다. 전체 DAG의 자원·버전 선언은 매 프레임 수행한다.

## 측정

CreatorRobot Debug, DX12 software 경로, Editor 전경, 8워커, Scene/Game 각각 1186×514다. 워밍업 후 프로파일 ON 15초와 OFF 15초를 분리했다. GPU validation은 정확성 실행에 사용했고, 성능 실행에는 ETW·GPU validation을 켜지 않았다. 이번 before/after 한 쌍에서 view별 CPU 표본은 365/376개다.

| 항목 | 전 | 후 | 변화 |
|---|---:|---:|---:|
| GraphDeclare 평균 | 5.3156ms | 5.0129ms | -5.70% |
| 기하 선언 평균 | 0.4685ms | 0.3563ms | -23.95% |
| Game CPU p50 | 13.8340ms | 13.4333ms | -2.90% |
| Game CPU p95 | 16.4384ms | 15.4317ms | -6.12% |
| Scene CPU p50 | 13.8322ms | 13.5218ms | -2.24% |
| Scene CPU p95 | 16.2219ms | 15.5617ms | -4.07% |
| Scene CPU max | 19.6324ms | 21.0767ms | +7.36% |
| 기록 OFF 완료 뷰/초 | 54.21 | 54.38 | +0.32% |

기하 선언 평균은 23.95%, 전체 선언 평균은 5.69%, Game CPU 중앙값은 2.90% 줄었다. 그림자 Occluder/Raster 평균도 0.4661→0.4231ms / 0.4310→0.3864ms였다. 카메라 첫 컬링 평균은 0.7014→0.6973ms로 거의 같았다. 입력 밀봉 평균도 0.9236→0.8993ms이며, 이번 변경에서 그 구현을 바꾸지 않았다.

기록 OFF 처리량은 거의 같다. 완료 뷰/초는 두 뷰의 처리량이며 표시 FPS가 아니다. Scene p95는 줄었지만 이번 실행의 최대값은 19.6324→21.0767ms로 늘었다. Game CPU p50은 120 FPS의 8.3333ms 예산보다 약 5.0999ms 크다.

가장 큰 잔여 선언 노드는 카메라 첫 컬링 0.6973ms다. 다음 분석 대상은 그 안의 shared visibility 입력 생성·업로드·바인딩과 그림자 최종 컬링이다. 입력 밀봉에서는 기존 토폴로지 캐시 뒤에 수행하는 프레임별 view/world/bone wrapper 생성 비용이 남아 있다.

## 검증

- 최종 Editor Debug 빌드 통과: `build-verified.log`. 측정 runtime DLL과 `native-verified/identity.json`의 SHA256이 일치한다.
- standalone 소프트웨어 RHI 검사 Debug/Release 모두 통과. 새 아레나의 4096 정렬, 성장 중 주소 유지, 역순 소멸, Reset 주소 재사용, 복사 예외 시 부분 소유자 해제를 검사했다. 연속 포인터 집합은 4096신원을 32회 반복 삽입·중복 거절했다.
- 입력 밀봉 302검사, malformed 24개 거절 통과.
- DX12 전체 RenderGraph GPU 검증 통과. 그래프 선언 아레나를 10000객체 이상 성장시킨 뒤 callback 실행과 Reset 소유자 해제도 검사했다.
- 최종 temporal 버킷 검사: 12프레임·2인스턴스, 0/1/4워커, opaque packet 12회·layer replay 3회, validation 오류 0, sourceStable=true. GBV coverage notice 1044는 1건 기록됐다.
- 실제 제품 matched/reload replay의 depth/baseColor/normal/metalRough/emissive/sceneHdr 6개 출력은 모두 비트 동일하다. 입력·카메라·자산 신원도 일치한다.
- 전후 profiler complete, unacked/dropped/invalid provenance 0, GPU event identity 전부 일치. view별 GPU pass 표본 수/실제 프레임 비율도 전후 동일하다.
- 변경 구간 formatter 검사 통과. 기존 tracked 변경 10개는 시작 patch와 동일하게 보존했다.

실행 이력: 넓은 `scene-only` 검사는 소스 동결과 검증 범위 조정 과정에서 중단했으며 통과 근거에 포함하지 않는다. 첫 temporal 실행은 fixture가 backend를 생략해 실행하지 않는 SPIR-V까지 생성하면서 E41012로 실패했다. fixture가 실행 backend를 명시하게 수정한 뒤 위 최종 검사가 통과했다. 회귀 도구에는 Debug/Release 선택과 독립 산출물 경로를 추가했다.

원시 로그·프로파일·출력은 `Artifacts/graph-declare-20261020/`에 보존한다. 기존 [소프트웨어 RHI 구현 스펙](../design/RHISoftwareCommandListSpecification.md)에 수명 계약을 함께 기록했다.

</details>
