# RG5 IBL/Surface/Raster reference API 이관 — 2026-10-07

**후속 판정:** 최종 Fog/PostChain/UI/Editor·capture/fixture 수용까지 완료하여 RG5 전체를 done/기성 10인일로 닫았다. [최종 수용 기록](RenderRg5FinalAcceptance20261007.md). 아래 reference 결과와 작성 당시의 범위 한정 판정은 보존한다.

## 변경 범위

유지되는 정밀 reference API 세 경로를 ExplicitVersioned graph에서 사용할 수 있도록 이관했다. 기존 DeclarationOrder는 명시적인 비교 정책으로 보존한다. 제품 live 기본은 기존 ExplicitVersioned/DependencyOrder를 유지한다.

| API | 선언·반환 계약 |
|---|---|
| IblBakeResult::Declare | surface/environment Read, 결과 Write, Ready Read. 결과의 생산 버전을 GraphOutput으로 반환 |
| SurfaceBatch::Declare | geometry/texture Read, 결과 Write, Ready Read. 결과의 생산 버전을 GraphOutput으로 반환 |
| RasterSurfaceBatch::Declare | MRT/private depth Write, mesh Read, resolve 입력 Read·결과 Write, Ready Read. 공유 depth/owner marker는 실제 depth 생산자의 버전을 읽음 |

Raster는 현재 mesh의 GraphOutput을 우선 사용한다. FindImportedBuffer/Texture는 버전 모드에서 v0를 반환하므로, 현재 생산자 대신 이를 읽으면 GPU 버퍼가 같은 물리 리소스여도 의존성 계보가 끊어진다. MRT와 depth의 생산 버전을 batch에 보존하고, 외부 depth 소비자에는 소유 graph/epoch를 검사하는 GraphDepth를 제공했다. 공유 depth는 새 writer를 만들지 않는다.

세 API는 단일 결과 생산이므로 Write를 사용한다. 기존 내용을 수정하는 Modify가 필요한 경로가 아니다. DeclarationOrder 분기의 LegacyState는 rollback/기준 비교 계약이며 명시 모드의 추론 fallback이 아니다. 기존 producer 누락·중복 선언·다른 graph·stale recording 거부를 유지한다.

## 검증 방법

MaterialRasterSurfaceProbe에 `--rg5-reference`를 추가했다. 기존 독립 CPU 수용 기준과 비교하는 raster→surface→IBL 체인을 세 정책(DeclarationOrder, ExplicitVersioned/PreserveDeclarationOrder, ExplicitVersioned/DependencyOrder)으로 각각 실행한다. 각 정책은 순차 및 1/4개 native 기록 워커를 사용한다.

일반 체인은 7개 fixture × 3개 기록 구성 × 3개 정책 = 63 frames이며 geometry/material/IBL readback이 CPU 수용된 기존 기준과 바이트 단위로 일치해야 한다. 공유 depth는 4개 fixture × 2개 재질 선언 순서 × 3개 기록 구성 × 3개 정책 = 72 frames이다. depth 불변, 두 재질의 coverage/정밀 IBL, coplanar/skinned/current-pose 입력과 잘못된 owner 거부를 검증한다. readback도 생산자가 반환한 버전과 명시 Read로 연결했다. ExplicitVersioned compile은 암묵 접근을 허용하지 않는다.

compiled snapshot에서도 모든 접근이 Read/Write이고 writer가 v1을 생산하며 version dependency edge가 존재하는지 확인한다. 기존 기본 probe 실행 옵션과 frame 수는 유지하고, reference 전용 옵션은 전체 SceneHost 통합 대신 해당 reference 체인만 확장한다.

검사 중 두 하네스 문제도 보완했다. `Debug-v1`은 새 옵션이 기존 SSS 경로로 분기되어 exit 0이었지만 reference marker가 없으므로 거부했다. `Debug-final`은 이전 제출 계약에 의존한 `Submission retained graph` 검사에서 실패했다. 현재 EnqueueRecordedBatch는 토큰을 ticket batch에서 GPU retirement로 이동하므로, 이를 batch에 남아 있다고 단정하는 대신 caller/ticket 해제 뒤 실제 graph owner 유지와 GPU 완료/retirement 뒤 해제를 검증한다. 엔진 제출 코드는 변경하지 않았다. 최종 수용은 수정 뒤 별도 디렉터리 결과로 판정한다.

재실행 하네스는 `Tools/regression/verify-rg5-reference.ps1`이다. 먼저 해당 구성의 `MaterialRasterSurfaceProbe.vcxproj`를 빌드한 뒤 새 OutputDirectory를 지정한다. 하네스는 GPU validation을 켜고 exit 0·정확한 frame/list 수·성공 marker·실행 파일 및 전체 관련 소스 해시 불변을 요구한다. Debug/Release 실행은 각각 독립 프로세스다.

증거: `Build/Verification/RG5Reference20261007/`의 구성별 build log, stdout/stderr, source-hashes.json, result.json.

## 실행 결과

**세 reference API 이관·GPU 수용 통과.** Debug/Release 의존 프로젝트 및 probe 빌드 통과. 기존 C4189/C4456/C4100와 Release Utility_Framework PDB LNK4020 경고는 남아 있다.

| 검사 | Debug-v3 | Release-final |
|---|---|---|
| 일반 reference 체인 | 3정책·63 frames·105 native lists·geometry/material/IBL 바이트 일치 | 동일 |
| 공유 depth | 3정책·72 frames·17,064 재질 픽셀·4,266 coplanar 픽셀·18 skinned frames | 동일 |
| compiled snapshot | 암묵 접근 0·writer v1·version edge 존재 | 동일 |
| 거부·소유권 | graph 실패 fixture 6·mesh 실패 4·SceneInput 실패 24, retirement 소유 유지/해제 통과 | 동일 |
| GPU validation·프로세스 | 문제 0·exit 0 | 문제 0·exit 0 |
| 소스/실행 파일 | RenderEngine·probe·기본 셰이더 922파일과 실행 파일 SHA-256 불변 | 동일 |

두 구성의 최종 성공 marker도 일치한다(checks 1,513,767·gpuComponents 1,162,384). 정책 체인의 바이트 일치와 독립 CPU 기준의 부동소수점 허용 오차를 구분한다. 후자의 maxNormalizedError는 8.59499e-05, maxSrgbError는 0.000689566, maxLodError는 0.000169724이다. 이 수치들은 정책 간 픽셀 차이가 아니다. 하네스의 result.json 및 source-hashes.json이 최종 증거이며, 실패한 앞선 디렉터리는 보존한다. 전체 SceneHost 통합·live 검사를 실행했다는 주장은 하지 않는다.

## 완료선

이 기록 자체의 완료 범위는 세 reference API와 해당 GPU probe다. 당시 남았던 최종 Fog/PostChain/UI/Editor 조합 및 capture/fixture는 후속 최종 수용으로 닫아 RG5 완료·기성 10인일을 회수했다. RG6 live 전환 전후 픽셀·성능 검증, MAT-9 품질/성능, Vulkan 교차 검증을 이 검사로 대체하지 않는다. 개별 reference result의 rg5Complete=false는 작성 시점의 범위 한정 증거로 보존한다.
