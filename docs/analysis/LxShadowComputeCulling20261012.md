# LX shadow meshlet compute 사전 컬링 — 2026-10-12

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

사용자 지시로 반복 성능 근거가 부족해도 8번 구현을 착수했다. 착수 승인은 이전 측정의 성능 수용 판정을 바꾸지 않는다. Vulkan 실행·교차 비교는 PHASE 4.9에 둔다.

## 구현

DX12 shadow meshlet 경로에서 실제 스키닝·월드 변환이 끝난 정점과 각 cascade의 광원 행렬을 읽는다. 모든 정점이 같은 clip 평면 밖에 있을 때만 제거하며, 비유한 값과 경계는 보수적으로 유지한다. 후보 수만큼 할당한 목록에 원래 meshlet ID를 한 번씩 기록하고, 생존 수를 `DispatchMeshIndirect(x,1,1)`에 전달한다. shadow mesh shader는 압축된 ID로 원래 토폴로지를 읽는다.

각 cascade가 목록·인자 버퍼 두 개를 공유하며 draw별 목록 시작 위치를 256바이트로 정렬한다. 출력은 cascade당 32 MiB로 제한하고 초과 입력은 잘라내지 않고 실패시킨다. draw 수에 비례해 DX12 committed 버퍼를 만들지 않는다. reset/cull은 cascade별 그래프 패스로 묶는다. 변환 출력·토폴로지 읽기, 목록 UAV→SRV, 인자 UAV→IndirectArgument 의존 관계를 그래프에 선언한다. 기존 GPU 가시성의 recording/descriptor 검증, 제출 완료 전 보존, 완료된 버퍼 풀 재사용을 이용한다. `CREATOR_LX_SHADOW_MESHLET_COMPUTE=0`은 기존 shadow meshlet 경로를 선택한다. SpirV는 이 구현을 활성화하지 않는다.

Scene host ABI 식별자를 19에서 20으로 올려 이전 cooked mesh shader가 새 압축 목록과 함께 실행되지 않도록 했다. 이전 cooked Scene 제품은 재생성이 필요하다.

이 작업 트리의 DX12 meshlet shadow 경로에서 compute는 기본 활성화한다. `CREATOR_LX_SHADOW_MESHLET_COMPUTE=0`으로 compute를 끌 수 있다. 기본 활성화와 구현 완료는 성능 이득 수용을 의미하지 않는다.

## 검증 범위

GPU 인자·목록·변환 정점을 리드백하여 CPU clip 판정과 비교한다. 생존 ID 범위·중복·용량을 검사하며 CPU가 요구한 ID 누락은 실패, 보수적 추가는 허용한다. 이는 GPU 변환 정점을 입력으로 한 CPU 판정 비교이며, 독립 CPU 스키닝 구현과의 동치는 아니다. 기존 스키닝 검증과 모든 cascade의 indexed/meshlet 깊이 맵 비트 동치를 함께 사용한다.

성능은 shadow raster 3개 패스에 reset/cull 6개 구간의 비용을 더해 평가한다. compute 비용을 뺀 raster 시간만으로 이득을 주장하지 않는다.

현재 반복 비교는 indexed 경로와 compute를 포함한 meshlet 경로의 전체 차이다. `CREATOR_LX_MESHLETS`는 다른 패스에도 적용되므로 CPU 준비·기록 차이를 compute만의 효과로 해석하지 않는다. 기존 meshlet 경로에서 compute만 켜고 끈 비교는 별도 후속 항목이다. 전체 GPU 패스 평균합은 제품 FPS나 프레임 지연 시간의 수용 근거가 아니다.

확대 검증은 상수 알파 임계값, 면 방향, 반사·비균일·전단 변환, 두 clip 경계, 먼 캐스터와 여러 meshlet을 포함한다. 여러 meshlet 사례는 기존 17×17 정점 격자의 world Y 스케일을 32로 확대하여 첫 cascade에서 실제 부분 생존(6개 중 3개)을 요구한다. 이전 5번의 원본 입력과 구분한다. 텍스처 알파 패턴·mip과 모든 실제 모델·스키닝·광원·경계 조합은 수용 범위에 포함하지 않는다.

## 실행 상태

DX12 구현과 선택 사례의 정확도·변이·반복 측정을 완료했다. 성능 이득은 미수용이다. 사용자 승인은 구현 착수 조건을 해제한 것이며 이 성능 판정을 바꾸지 않는다.

정상 확대 검증은 최종 ABI 20 Debug/Release 각각 12개 사례·깊이 맵 72쌍·GPU 목록 144건을 통과했다. 두 구성의 목록 누락·보수적 초과는 모두 0건이며 여러 meshlet 사례의 C0은 두 포즈 모두 후보 6개/필수 3개/생존 3개였다. 실행마다 GPU 검증 레이어 문제·메시지 유실 0건, 종료 코드 0과 정상 quit을 확인했다. 자료는 `Build/Verification/ShadowCompute-20261012/Normal-{Debug,Release}-V20/{verdict,cases,capture-manifest,identity-before,identity-after}.json`과 구성별 최신 원본 깊이 맵이다.

정상 바이너리 SHA-256은 Release `F6BBA6D61D6F84CD173DC085FD73D2128FC243B10DED4D7B21D38247871D9E22`, Debug `E7466775A76E405FE0AB8A027BA78B23C979ED7968FA3488C69BD13E571874DB`이다. backOff 제거 변이의 D/R 실패를 확인한 뒤 원본 소스를 SHA-256 `A75EB1D2C8F41EA226BC04F8F2686BBE4F5DB188D1177DAC309B7C418FD21625`로 복구하고 재빌드한 결과다.

최종 D/R 기본 회귀는 `dx12.shadowquality`, `dx12.skinning`, `dx12.iblshade`, `dx12.rendergraph`, `dx12.scene`를 각각 통과했다. GPU 검증 레이어 문제·메시지 유실은 0, 종료 코드 0과 정상 quit을 확인했다. `Regression-{Debug,Release}-V20`에 판정·원본 로그·신원을 보존했다.

음성 대조군은 D/R 각각 세 종류, 총 6건을 의도한 이유로 거부했다. `BackOffMutant-{Debug,Release}`는 backOff 제거 후 먼 판 그림자 누락, `MeshletMutant-{Debug,Release}-V20`는 원래 meshlet ID의 MS 출력 누락, `ListMutant-{Debug,Release}-V20`는 compute 생존 목록의 필수 ID 누락을 검사했다. 모든 변이는 실패 종료 코드 4, 정상 quit, GPU 검증 문제·메시지 유실 0을 확인했다. compute 목록 변이의 첫 사례는 후보 1/필수 1/생존 0/누락 1이다. 따라서 성공적인 검증기 실행과 의도한 렌더 테스트 실패를 구분한다.

GPU 목록 로그를 제거한 옛 확대 결과와 compute 타이밍 개수를 제거한 복제 프로파일도 새 검증기가 거부했다. `negative-missing-lists.json`, `negative-compute-timing.json`에 해당 검증기 대조 조건을 보존했다.

초기 Release 실행은 셰이더 모듈 로드에 실패했다. 새 목록의 t156이 기존 temporal 정점과 겹쳤고, 편집 중 추가한 UTF-8 BOM은 host 문자열을 생성 소스 중간에 붙이는 경로에서 잘못된 문자가 되었다. 첫 두 실패 자료를 보존했다. 목록은 t157, compute 입력은 HZB와 겹치지 않는 t18/t19로 옮기고 셰이더 BOM을 제거했다. 수정된 루트를 다시 빌드한 바이너리로 최종 새 실행을 완료했다.

## 최종 반복 성능

최종 Release에서 3개 합성 장면마다 indexed·compute meshlet 각각 4회, `indexed→meshlets→meshlets→indexed→indexed→meshlets→meshlets→indexed` 순서로 8초 기록했다. 총 24개 독립 프로파일과 타이밍에서 제외한 CPU audit 3개를 보존했다. 렌더·표시 크기는 모두 332×182이며 runtime·입력·shader 신원 일치, native-real 출처, 정상 quit/종료 코드 0, 계측 누락·overflow·유실·span/slice 오류 0을 통과했다. 측정 실패 제외나 재시도는 없었다.

비교 지표는 `Shadow.TotalIncludingCompute`다. raster C0~C2 패스 평균에 실제 view당 reset/cull 비용을 더한다. 모든 compute 기록에서 두 marker 각각의 호출 수는 cascade 호출 수의 3배였으며 indexed에서는 0이었다.

| 합성 입력 | indexed 평균합 ms | compute meshlet 평균합 ms | 관측 평균 변화 | CV indexed / meshlet | 판정 |
|---|---:|---:|---:|---:|---|
| MatGrid 8×8, inside | 4.951 | 10.665 | +115.41% | 20.93% / 22.72% | 미수용 |
| Suzanne 12×12, inside | 0.919 | 2.188 | +138.21% | 18.32% / 21.82% | 미수용 |
| Suzanne 12×12, inside, far=30 | 0.659 | 2.368 | +259.27% | 33.84% / 9.96% | 미수용 |

두 경로 모두 CV ≤10%이고 평균 비용이 감소해야 성능 이득을 수용한다. 세 장면 모두 그 조건을 충족하지 못했다. 표의 변화율은 이번 표본의 관측 평균 차이이며, 안정적인 회귀량 또는 제품 FPS/지연 시간으로 수용한 수치가 아니다. cull 자체가 실제 view당 평균 약 4.89/1.15/1.34 ms를 더했다(위 표 순서). 해당 component의 CV도 원본 판정에 공개했다.

총합 원본 표본(ms, 각 경로 4회)은 다음과 같다. 더 높은 정밀도의 값과 각 cascade·compute·CPU component는 `overall.json`에 있다.

- MatGrid indexed: 5.941180, 5.458188, 4.858246, 3.546019; meshlet: 9.221028, 10.877532, 8.566076, 13.994079.
- Suzanne indexed: 0.970497, 0.993492, 0.669904, 1.040234; meshlet: 1.575522, 2.046094, 2.522876, 2.607651.
- far=30 indexed: 0.676771, 0.597271, 0.949859, 0.412859; meshlet: 2.291368, 2.169601, 2.301839, 2.710394.

전체 측정 manifest·판정은 `Build/Verification/ShadowPerformance-20261012-Compute/{runs,overall}.json`, 각 원본은 manifest의 `Build/Obj/LxShadowP0/Run-Release-*20261012-Compute*` 경로의 `.ceprof`, summary, 입력 manifest, 명령 응답, 시작/종료 계측 상태와 로그다. 정확도·변이·현재 binary 신원을 묶은 최종 판정은 `Build/Verification/ShadowCompute-20261012/overall.json`이다. 정상 D/R 최신 깊이 원본은 보존하고 예상 거부를 확인한 변이 픽셀은 정리했다. 이전 실패와 당시 성능 자료는 수정하지 않았다.

## 남은 수용 범위

동일 meshlet 경로에서 compute만 켜고 끈 A/B, 추가 cull/준비 비용 최적화와 안정된 재측정이 남는다. 이득을 수용하기 전까지 구현 완료와 성능 수용을 구분한다. 출력 32 MiB 초과 실패와 비유한 입력의 보수적 유지 정책은 구현 계약이며, 이번 선택 fixture가 모든 용량·비유한 입력의 실행 경계를 검증한 것은 아니다. 독립 스키닝 정답, 텍스처 알파, 실제 제품·cooked Player 장면의 일반 수용은 별도다. Vulkan 실행·교차 비교는 PHASE 4.9에 유지한다.


## LX Scene 단일 배선 후속 — 2026-10-12

일반 GBuffer 구현과 구경로 테스트·A/B 도구를 제거했다. Scene ABI 21과 모델 CEMC 12를 사용하며 이전 리소스는 강제 리임포트·재쿠킹한다. 현재 변경과 검증 판정은 [LX 전환 보고서](../analysis/LxSceneBigBangCutover20261012.md)를 따른다. Vulkan 신규 배선 실행 검증은 PHASE 4.9에 남는다.

</details>
