# Shared visibility 입력 조립 최적화 — 2026-10-20

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

`b7ccc2b1a` 기준으로 shared 입력 생성 평균은 **0.09636→0.09200ms(-4.52%)**다. 최종 Game CPU p50은 **13.46990ms**이며 120 FPS의 8.33333ms 예산을 **5.13657ms 초과**한다. 120 FPS 목표는 미달이다. 최종 코드는 shared 경로의 조립 scratch와 메타데이터 소유만 변경한다.

## 최종 구현

`PrepareMeshlets → PrepareInternal → Frame::DeclareMeshletBatch → BuildShared → ReserveUploadBatch/CreateBindings → SharedBatch::Dispatch` 경로에서, `BuildShared`는 최대 8뷰·4096개 bin을 합친다. candidate의 view/bin/job offset, 5행 world/bounds, projection/history, 출력 UAV 및 geometry/topology SRV를 구성한다.

- Shared 조립은 `BufferPool::UploadScratch` 대신 기존 `ce::RenderArenaPool`의 독점 lease로 descriptor 배열을 구성한다. 이 풀은 8개 CAS 반환 슬롯과 최대 16MiB retained raw block을 사용하며 pmr을 사용하지 않는다. 중첩 선언도 별도 lease를 받는다. 함수 종료 시 CPU 조립용 아레나를 즉시 반환한다.
- HZB owner와 밀봉 입력 owner는 고정 8칸 배열이다. 진단용 count vector와 복사를 없애고 보유한 `MeshletInputs::bins`에서 값을 읽는다. `MeshletInputs`에는 Frame 역참조가 없어 순환 소유가 없다. Frame·graph callback·upload retirement가 GPU 입력과 batch 수명을 유지한다.
- Batch 객체는 renderer allocator로 만든다. batch 객체와 lease control block의 할당은 남으며 raw block을 재사용한다. Shared descriptor scratch의 mutex 획득과 vector proxy/capacity 관리가 제거됐다.
- Shared GPU 입력의 6개 업로드 요청과 6개 descriptor table 생성, job/bin 배치, shader와 dispatch는 기존 계약을 사용한다. 단일 뷰 업로드 및 DX12 리소스 해소는 기존 구현이다.

뷰별 candidate/bin vector, 선언 scratch mutex, buffer/readback pool mutex와 매 배치 descriptor 생성 비용은 남는다. 이번 변경이 엔진 전체의 할당·잠금을 제거하지는 않는다.

## 최종 동일 조건 계측

Debug, software RHI 명시 활성화, Game/Scene 두 뷰, 각 1186×514, 8워커, 같은 카메라·draw 입력이다. profiler ON/OFF 각 15초이며 빌드·GPU 회귀 실행과 겹치지 않았다. 비교는 `before`와 **`after-safe`** 한 쌍이다. 수용하지 않은 중간 바이너리의 `after`/`after-final` 수치는 최종 결과에 합치지 않는다.

| 구간 | 변경 전 | 최종 변경 후 | 변화 |
|---|---:|---:|---:|
| Shared 입력 생성 평균 | 0.09636ms | 0.09200ms | -4.52% |
| Shared 바인딩 생성 평균 | 0.04045ms | 0.04047ms | +0.06% |
| 카메라 1차 컬링 선언 평균 | 0.70323ms | 0.69885ms | -0.62% |
| 그림자 최종 컬링 선언 평균 | 0.46307ms | 0.45586ms | -1.56% |
| 전체 그래프 선언 평균 | 5.04950ms | 5.04524ms | -0.08% |
| 그래프 컴파일 평균 | 0.81515ms | 0.82921ms | +1.73% |
| 명령 기록 평균 | 1.91299ms | 1.93959ms | +1.39% |
| 기록 dependency graph 평균 | 1.73484ms | 1.76005ms | +1.45% |
| 소프트웨어 명령 번역 평균 | 1.72102ms | 1.70775ms | -0.77% |
| Game CPU p50 | 13.41405ms | 13.46990ms | +0.42% |
| Game CPU p95 | 15.86500ms | 15.74600ms | -0.75% |
| Game CPU max | 18.73340ms | 18.61710ms | -0.62% |
| Scene CPU p50 | 13.57790ms | 13.56830ms | -0.07% |
| Scene CPU p95 | 15.84930ms | 15.57500ms | -1.73% |
| Scene CPU max | 19.85700ms | 18.99900ms | -4.32% |
| 기록 OFF 완료 뷰/초 | 55.3548 | 57.0178 | +3.00% |

Shared 입력 생성에 바인딩 생성이 포함되므로 두 절감량을 더하지 않는다. 뷰당 shared 생성은 2회이며 이 구간의 뷰당 절감은 0.00870ms다. 완료 뷰/초는 두 뷰의 처리량이며 표시 FPS가 아니다. 위 Game/Scene CPU 값과 120 FPS 예산을 별도로 판정한다.

## 검증

- 최종 Editor Debug 및 MaterialRasterSurfaceProbe Debug 빌드: `build-safe.log`, `probe-build-shared-isolated.log`.
- 최종 DX12 전체 RenderGraph GPU validation: `native-safe/result.json`. software 기록·비동기 번역·업로드 rollover·retirement를 검사한다.
- Shared 큐/LOD: `shared-isolated/verdict.json`, sourceStable=true. 32프레임·7212픽셀·6364개 전환·mixed LOD 8프레임, queue header 224개, shared queue read 192개를 검사했다. 밀봉 job 9152개 중 2712개가 게시·완료됐고 6440개는 게시 전에 필터됐다. 오류 0이다.
- 최종 4096-bin dispatch/admission, HZB/history, temporal 버킷: `material-safe/verdict.json`. 실제 실행 결과와 GBV coverage notice 수는 JSON에 보존한다.
- 제품 matched/reload replay의 depth/baseColor/normal/metalRough/emissive/sceneHdr 여섯 출력은 기준과 비트 동일하다.
- 전후 profiler complete, unacked/dropped/invalid provenance 0, GPU event 전부 matched. GPU pass 표본/실제 프레임 수는 Game 139, Scene 142로 동일하다.
- 변경 구간 clang-format 19.1.5, diff 공백 검사, 시작 시 tracked 변경 10개 보존 검사를 수행했다.

원본은 `Artifacts/shared-visibility-20261020`, 저장소에 보관하는 수치·판정은 [JSON](SharedVisibilityInputs20261020.json)이다. 최종 제품 캡처와 native GPU 검증의 runtime SHA256 일치를 검사한다. 이 결과는 DX12 Debug 경로이며 Vulkan 실행 결과를 포함하지 않는다.

## 검증 중 제외한 변경

초기 실험에는 단일 뷰의 history projection 업로드 통합과 DX12 descriptor 리소스 포인터 재사용이 포함됐다. 추가 LOD 검증에서 픽셀 깊이 회귀가 발생했다. 변경 전 엔진 3개 소스와 실행 파일을 `baseline-build`에 별도 보관해 같은 검사로 비교했다. 기준 엔진은 32프레임·7212픽셀·header 224개를 통과했다. 전체 실험은 actual 0.194015/expected 0.249719, DX12 변경을 제외한 visibility 실험도 actual 0.248759/expected 0.249719로 실패했다.

단일 뷰 변경을 원복하고 shared 조립만 남긴 `shared-isolated`는 전체 검사를 통과했다. 단일 뷰 업로드 확장과 DX12 포인터 재사용은 최종 변경에서 제외했다. 실패 결과를 보존하며 성능 수용 근거로 사용하지 않는다.

초기 회귀 도구에는 별도의 잘못된 기대값도 있었다. 현재 master shader는 `GeometryPrepareMeshlet`에서 LOD 선택·residency를 통과한 meshlet만 큐에 게시하지만 검사는 전체 입력이 게시돼야 한다고 요구했다. 첫 진단은 입력 13, 게시·완료 6, 오류 0, partition 208/208, 잔여 작업/lease 0이었다. master 원문 비교는 `assertion-origin.txt`에 보관했다.

검사는 게시 수가 입력 상한 이내이며 완료 수와 정확히 같아야 하고, 게시 0/비0에 맞춰 working wave가 0/비0인지 확인하도록 수정했다. 오류 0, partition 전수 처리, head/tail 회계, 빈 슬롯, 작업·lease 0, 독립 partition 계산 및 픽셀 검사를 유지했다. 거절 partition의 범위와 `0 < publishedJobs < sealedJobs`도 검사한다. 이 수정 뒤 발견된 픽셀 회귀를 기준 엔진과 비교해 위와 같이 제거했다.

</details>
