# 텍스처 시작 등록 대기 최적화와 모델 로드 전후 비교

2026-10-21. Debug x64 Editor를 재빌드하고 CreatorRobot과 Aria를 실제 DX12 씬에 배치했다.

**첫 배치가 Robot 16.111초 → 5.430초, Aria 12.967초 → 3.923초로 줄었다.** 모델 요청 이후 텍스처 등록 대기는 각각 10.027초 → 0.009초, 9.422초 → 0.377초였다. 두 최종 실행의 8회 배치는 모두 성공했고, 무효화 재시도와 새 Recovered 재질은 0회/0개였다.

## 계측으로 확인한 병목

기존 텍스처 43개는 모두 캐시를 재사용했다. 다시 디코딩하거나 새로 생성하느라 느린 것이 아니었다. 재사용을 검증하기 위해 원본 바이트와 캐시 산출물 전체를 SHA-256으로 읽는 경로가 병목이었다.

수정 전 추적 실행의 캐시 산출물 합계는 **854,085,184바이트**였다. 큰 HDR 텍스처들은 개당 약 134MB였고, 한 파일의 캐시 해시 검사만 약 1.6~1.8초가 걸렸다. 43개 확인 작업은 기존 cook tail에 의해 순서대로 실행된다.

| 시작 작업 전체, 43개 합계 | 수정 전 | 수정 후 Robot 우선 | 수정 후 Aria 우선 |
|---|---:|---:|---:|
| 원본 읽기·generation key 계산 | 2,486.539ms | 825.865ms | 824.771ms |
| record 읽기·캐시 전체 해시 검증 | 11,494.974ms | 3,219.149ms | 3,309.114ms |
| manifest 및 byte-source 게시 준비 | 158.016ms | 150.959ms | 153.138ms |
| 합계 | **14,139.529ms** | **4,195.963ms** | **4,287.024ms** |
| 기존 generation 재사용 | 43/43 | 43/43 | 43/43 |

수정 전후 직접 추적한 Robot 순서에서 이 작업 합계는 약 **70.3% 단축**됐다. 위 세부 시간은 CookTextureGeneration 내부 범위이며, 마지막 줄의 게시 준비는 카탈로그 mount 실행 시간을 포함하지 않는다. 작업은 Editor 시작부터 진행되므로 전체 14.14초와 모델 요청 이후 대기 10.03초는 같은 측정 구간이 아니다.

## 구현 변경과 유지한 검사

[TextureGenerationHash](../Editor/EngineEntry/TextureGenerationHash.h)를 추가하고 [EditorAssetDatabase](../Editor/EngineEntry/EditorAssetDatabase.cpp)의 TextureGenerationKey와 ReadTextureGenerationRecord에서 사용했다. 대용량 바이트의 해시는 Windows BCrypt 스트리밍 SHA-256으로 계산한다.

- 원본과 캐시 바이트를 끝까지 읽고 검사하는 동작을 유지했다. 파일 크기·수정 시각만 보고 검사를 생략하지 않는다.
- generation key의 identity·recipe·format 입력, 길이 접두사, SHA-256 결과와 경로 형식을 유지했다. 기존 캐시를 재사용하며 캐시 마이그레이션이 필요하지 않다.
- 64KB 청크를 유지하므로 전체 854MB를 한꺼번에 메모리에 올리지 않는다.
- provider 생성·Update·Finish 오류를 확인하고 실패 시 digest를 반환하지 않는다. 해시 오류를 검증 성공으로 바꾸는 fallback은 없다.
- 전역 Hash::Sha256과 UUID/subasset 신원 유도 경로는 변경하지 않았다.
- 원본/sidecar의 전후 stamp 검사, 실제 변경 시 재가져오기, 손상된 캐시의 재생성, 카탈로그 revision 검사와 모델 무효화 보호를 유지했다.
- CE_TRACE_TEXTURE_STARTUP 환경변수를 설정하면 텍스처별 재사용 여부·바이트 수·단계 시간을 출력한다.

이번에는 카탈로그 일괄 게시나 cook 작업 병렬화를 추가하지 않았다. 계측상 가장 큰 비용인 Debug scalar 해시 계산을 먼저 줄였다. 실제 카탈로그 게시 구조와 이전 단계의 모델 admission 대기는 유지한다.

## 모델 배치 시간 비교

| 실행 순서 / 모델 | 수정 전 | 수정 후 | 관측 차이 |
|---|---:|---:|---:|
| Robot 우선, 첫 Robot | 16.111초 | 5.430초 | 66.3% 단축 |
| 같은 실행, 두 번째 Aria | 3.905초 | 3.534초 | 9.5% 단축 |
| 같은 실행, Robot 재배치 | 1.074초 | 1.067초 | 거의 동일 |
| 같은 실행, Aria 재배치 | 0.272초 | 0.272초 | 거의 동일 |
| Aria 우선, 첫 Aria | 12.967초 | 3.923초 | 69.7% 단축 |
| 같은 실행, 두 번째 Robot | 5.158초 | 5.100초 | 거의 동일 |
| 같은 실행, Aria 재배치 | 0.280초 | 0.278초 | 거의 동일 |
| 같은 실행, Robot 재배치 | 1.066초 | 1.131초 | 6.1% 증가 |

| 첫 요청의 텍스처 등록 대기 | 수정 전 | 수정 후 |
|---|---:|---:|
| Robot 우선 | 10,026.590ms | 8.791ms |
| Aria 우선 | 9,421.561ms | 376.955ms |

등록 확인 작업이 사라진 것은 아니다. 약 4.2초로 줄어 시작 시 사전 대기·카메라 설정 구간과 대부분 겹치게 됐으므로, 모델을 요청한 시점 이후의 대기가 훨씬 짧아졌다. 더 이른 시점에 요청하면 남은 확인 작업을 기다릴 수 있다.

두 번째 모델과 재배치는 시작 등록 대기의 영향을 거의 받지 않아 큰 개선이 없다. 재배치 Robot의 증가도 표에서 제외하지 않았다.

## 비교 조건과 해석 범위

- 동일 프로젝트·GLB, i7-8700K / RTX 2080 Ti, Debug x64, DX12 Debug Layer on, GPU Validation off 조건이다. 파일/OS/shader 캐시를 지우지 않았다.
- 이번에 수정 전 Robot 우선 실행도 새로 계측했다. 원인 추적 로그만 추가한 빌드를 사용했다. Aria 우선 기준선도 이번 작업 중 이전 runtime으로 다시 실행했으며, 끝난 뒤 최종 runtime을 복원하고 해시를 확인했다.
- 실행 순서는 수정 전 Robot → 수정 후 Robot → 수정 후 Aria → 수정 전 Aria다. 각 프로세스에서 새 씬·카메라·조명을 만들고 두 모델의 첫 배치와 재배치를 측정했다.
- 성능 실행 중 다른 빌드·프로파일 분석·기능 검사는 실행하지 않았다. 모든 성능 실행이 끝난 뒤 캡처를 분석했다.
- Robot 전후와 수정 후 Aria에는 텍스처 세부 추적이 있다. Aria 기준선은 이전 runtime이므로 텍스처 세부 로그가 없고 모델 전체 시간과 admissionWaitMs로 비교한다.
- 모델 수치는 요청 생성부터 씬 적용 완료까지의 벽시계 시간이다. 첫 정상 화면까지의 지연과 동일하지 않다. 실제 표시 내용은 별도 DX12 캡처로 확인했다.
- 각 순서 1쌍의 관측 결과다. 표의 단축률은 안정된 장기 평균이나 모든 프로젝트에 대한 보장값이 아니다. 이전 보고서의 Robot 14.078초 / Aria 12.465초 표본과 이번 새 기준선이 달라진 점도 이 변동을 보여준다.

## 검증 결과

1. Debug x64 Editor 재빌드 성공. 로그는 Build/texture-startup-fixed-build-20261021.log다.
2. [해시 비교 검사](../Tools/regression/texture_generation_hash_tests.cpp) 80개 통과. 공표된 빈 입력/abc 벡터, 기존 scalar 구현과의 일치, SHA 블록 및 64KB 청크 경계, 1MB 입력의 서로 다른 Update 분할, 같은 크기의 바이트 변경, 잘못된 CNG 상태에서 출력 digest를 보존하며 실패하는 동작을 확인했다.
3. 수정 후 두 실행에서 동일한 854,085,184바이트의 기존 generation 43개를 모두 재사용했다. 원본 바이트나 cache key 형식이 바뀌어 재생성된 결과가 아니다.
4. 별도 프로젝트에서 캐시 artifact의 마지막 바이트 하나만 바꿔 크기를 유지했다. 재시작 시 손상 generation을 거부하고 다시 생성해 원래 digest를 복원했다. 다음 실행에서는 해당 generation을 재사용했다. 결과는 corrupt-cache/summary.json에 있다.
5. 최종 8회 모델 배치 모두 완료·0실패·0재시도·새 Recovered 0개. 명령 결과도 모두 성공했다. 8개 DX12 캡처 모두 finite=true, 332×144였으며 Robot draw 4개·유효 깊이 픽셀 324개, Aria draw 3개·525개를 확인했다.
6. 실제 텍스처 재가져오기·ImageComponent/SpriteRenderer 참조 갱신·씬 재로드·실패 시 이전 참조 보존·Undo/Redo·prefab 동작은 저장소의 verify-texture-reimport-scene.ps1로 검증했다. 수정 후 단독 실행이 2회 연속 통과했다. 세부 실행 기록과 초기 실패는 아래에 함께 남긴다.
7. 네 성능 캡처 모두 complete=true, unacked=0, droppedEvents=0이다. 원본 두 GLB의 SHA256은 이전 기록과 일치한다. 실행으로 바뀐 active.workspace는 작업 전 파일로 복원했다. 기존 다른 변경은 유지했고 커밋하지 않았다.

### 자동 재가져오기 검사에서의 초기 실패

첫 수정 후 통합검사는 stage=1에서 60초 timeout이 났다(TRS-Debug-95f8a94b). 당시에는 성능 캡처 분석도 병행 중이었다. 이전 runtime의 별도 실행은 통과했고(TRS-Debug-78873baa), 수정 후 runtime을 다른 작업 없이 단독 실행한 검사도 2회 연속 통과했다(TRS-Debug-06859dd4, TRS-Debug-61084093). 최초 timeout의 정확한 원인은 이번 변경에서 확정하지 못했으므로 분석 작업 탓으로 단정하거나 실패 기록을 삭제하지 않는다. 이 실패를 성능 표본으로 사용하지 않았다.

## 남은 비용

수정 후에도 모델 recipe 준비는 첫 Robot 약 4.38초, 첫 Aria 약 3.05초가 남았다. 재질 제품 컴파일 범위는 각 실행 7회로 전후 동일했다. 이번 개선은 시작 시 텍스처 캐시 확인 비용에 집중한 결과다.

RenderThreadFrame 최장 CPU 범위는 Robot 우선 1,867.598 → 1,569.099ms, Aria 우선 1,036.135 → 1,198.762ms였다. 따라서 첫 표시 시 긴 렌더 준비가 해결됐다고 할 수 없다. 이 수치는 CPU 프로파일이며 GPU 실행 시간이나 화면에 표시된 FPS를 뜻하지 않는다. 다음 우선순위는 이전 보고서에서 확인한 MaterialGraphScenePrepare / MaterialSceneCasterSnapshot과 메시 cluster residency 준비다.

## 증거와 실행 식별자

원시 자료: Build/texture-startup-20261021/.

- before-robot/, before-aria/, after-robot/, after-aria/: 시간 로그, 명령 결과, runtime 해시, DX12 캡처와 CEPROF·summary.
- comparison.json: 네 실행의 시간·텍스처 단계·렌더 검증 수치.
- corrupt-cache/, check-corrupt-cache.ps1: 동일 크기 캐시 손상·재생성·재사용 검증.
- reimport-before-receipt.json, reimport-after-receipt.json, reimport-after-repeat-receipt.json: 통합검사 통과 receipt. 초기 실패는 Build/TRS-Debug-95f8a94b/에 보존한다.
- measure.ps1: 모델 계측 재현 도구. 해시 검사는 Tools/regression/texture_generation_hash_tests.cpp를 CE_CONTRACT_STANDALONE, NOMINMAX, Debug(/Od)로 컴파일해 실행했다.

Aria 수정 전 runtime: 04DB2DADDEA7C19F6AABE04C455E79E73278F0A124D38FEA89B9E522680FF8B5.

Robot 수정 전 추적 runtime: EFC454767565FD26D9AF7AD5A100F241D229200D3D66495EC3103CA9F0FAFB30.

최종 runtime: 22FF24E957267C274803263B1256E5B9E8C108CE5DEB97B990C050983C13FDF7.

이전 단계: [무효화 재시도 원인 수정 보고서](ModelInvalidationRetryRootCause-20261021.md).
