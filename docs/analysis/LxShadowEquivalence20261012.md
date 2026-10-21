# LX 그림자 3개 캐스케이드 경로 동치 (2026-10-12)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

후속 목록 3번의 검증기를 `dx12.skinning`의 선택 실행 모드에 추가했다. 제품의 SceneHost 그림자 경로에서 동일한 합성 메시 두 개(스킨드·비스킨드)를 indexed와 meshlet로 각각 렌더링한다. 바인드 포즈·본 이동 포즈 각각에서 2048×2048 D32Float 캐스케이드 3장을 비교한다.

## 판정 조건

- 프레임 준비에 `allowMeshlets=false/true`를 전달한다. 프로그램·메시·카메라·방향광·월드 변환을 공유하고, 같은 팔레트 값을 복구한 후 비교 대상 프레임을 렌더링한다.
- 깊이 배열의 3개 서브리소스를 모두 GPU readback한다. 참조(indexed)의 각 맵이 비어 있지 않아야 하고, 양쪽 깊이가 모두 유한해야 한다.
- 깊이는 float 비트를 직접 비교한다. 누락·추가·불일치 텍셀 수와 최대 절대 깊이 오차를 기록하며, 불일치 0을 요구한다. 허용 오차는 없다.
- 캐스케이드 행렬도 비트 동치를 요구한다. 포즈가 다른 두 맵 사이의 기존 변화 검사도 유지한다.
- 그림자 콜백에서 mesh dispatch가 성공한 직후 캐스케이드별 atomic 카운터를 증가시킨다. meshlet 캡처는 각 캐스케이드에 양수, indexed 캡처는 0을 요구한다. CPU 준비 수나 카메라 경로의 mesh dispatch로 대체하지 않는다. 이 값은 GPU 생존 메시렛 수가 아니다.
- 실행 스크립트는 원본 깊이와 행렬 파일의 크기·SHA256 동치까지 별도로 검사한다. 실행 전후 소스·binary 해시 일치, GPU validation 활성·문제 0·누락 메시지 0, quit 성공·프로세스 종료 코드 0을 요구한다.

일반 `dx12.skinning`은 indexed fallback을 허용한다. 엄격한 동치 검사는 `CREATOR_SHADOW_EQ_OUTPUT`이 설정된 실행에서만 활성화되며, mesh shader 미지원 장치에서는 통과로 처리하지 않는다.

## 실행 결과

**후속 목록 3번 완료.** 현재 소스에서 빌드한 Debug/Release 모두 아래 6개 비교를 통과했다. GPU validation 활성·문제 0·누락 메시지 0, quit 성공·종료 코드 0, 실행 전후 소스·binary 해시 일치를 확인했다. 레이어 메시지 1건은 활성화 안내다.

| 포즈 | C0 참조 깊이 텍셀 | C1 | C2 | 불일치 / 누락 / 추가 / 최대 오차 |
|---|---:|---:|---:|---|
| 바인드 | 45,878 | 8,381 | 449 | 모두 0 |
| 본 이동 | 45,856 | 8,379 | 439 | 모두 0 |

각 meshlet 프레임은 캐스케이드마다 mesh dispatch 2회, indexed 프레임은 0회다. 6개 행렬 쌍도 모두 같다. 첫 캐스케이드의 포즈 변화는 기존과 같은 40,762 텍셀이다.

## 재현·증거

먼저 현재 소스로 Editor를 해당 구성에서 빌드한다. 아래 스크립트는 빌드와 동시에 실행하지 않는다.

```powershell
& Tools/regression/verify-lx-shadow-baseline.ps1 `
    -Configuration Debug `
    -FixtureProject Build/Obj/LxShadowP0/Fixture `
    -OutputDirectory Build/Verification/ShadowEquivalence-New/Debug `
    -ShadowEquivalenceOnly
```

Release는 구성과 출력 디렉터리를 변경한다. 스크립트는 용량 하한 30GiB 검사 후 격리된 프로젝트·설정으로 `dx12.skinning`, `dx12.validation`, `quit`을 실행한다. 성공 구성별 대표 원본을 보존하고 retention 기록을 남긴다.

- 검증 스크립트 (`../../Tools/regression/verify-lx-shadow-baseline.ps1` — 현행 master에 없는 이전/제안 경로; 당시 기록으로만 참조)
- Debug 판정 (`../../Build/Verification/ShadowEquivalence-20261012/Debug/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 동치 상세 (`../../Build/Verification/ShadowEquivalence-20261012/Debug/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 원본 해시 (`../../Build/Verification/ShadowEquivalence-20261012/Debug/capture-manifest.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령별 결과 (`../../Build/Verification/ShadowEquivalence-20261012/Debug/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 판정 (`../../Build/Verification/ShadowEquivalence-20261012/Release-Final/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 동치 상세 (`../../Build/Verification/ShadowEquivalence-20261012/Release-Final/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 원본 해시 (`../../Build/Verification/ShadowEquivalence-20261012/Release-Final/capture-manifest.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령별 결과 (`../../Build/Verification/ShadowEquivalence-20261012/Release-Final/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 빌드 (`../../Build/Verification/ShadowEquivalence-20261012/build-Debug-final.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), Release 빌드 (`../../Build/Verification/ShadowEquivalence-20261012/build-Release-final.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

원본은 각 실행의 `captures/`에 `{indexed,meshlet}-{bind,posed}-C{0,1,2}.f32`로 남긴다. little-endian float32, 행 우선, 2048×2048, 파일당 16MiB다. `.matrix`는 같은 캐스케이드의 4×4 float32 행렬 64바이트다. 구성당 깊이 파일 12개(192MiB)를 보존한다.

Release 빌드가 링크 중일 때 첫 실행 준비에서 DLL 해시 읽기가 거부되었다. GPU 실행·캡처 전에 중단된 준비 디렉터리 `Release/`는 성공 증거가 아니다. 빌드 프로세스 종료 코드 0을 확인한 뒤 `Release-Final/`에서 실행했다.

## 범위와 남은 작업

이는 DX12의 두 합성 메시·두 포즈에 대한 indexed/meshlet 동치 수용이다. 각 메시가 여러 메시렛으로 분할되는 복잡한 모델, 알파 마스크·면 판정·비균일 변환·캐스케이드 경계 확대 수용은 아니다. 두 경로가 공유하는 결함까지 이 검사로 배제하지 않는다.

후속 4번의 **보이는 메시렛 누락 변이 거부**도 D/R에서 확인했다([변이·정상 대조군 증거](LxShadowMeshletOmission20261012.md)). 5번의 [12개 합성 장면 확대](LxShadowCases20261012.md)도 최신 D/R 빌드에서 동치·대조 조건·변이 거부를 통과했다. 다음은 7번 계측 누락 해결·반복 성능 측정이며, compute 사전 컬링 진행 판단은 남아 있다. Vulkan 실행·교차 비교는 사용자 결정대로 PHASE 4.9 소유다. 이번 GPU validation 실행의 시간은 성능 수용에 사용하지 않는다.

Debug/Release stderr의 기존 `[profiler] shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]` 진단은 해결 범위 밖이다. 이번 동치 검사로 프로파일러 종료 진단이 해결되었다고 주장하지 않는다.

검증 runtime SHA256: Debug `2CF091C5D899D8FE756556485C81F02FD0444128B4E0342F9E82100344FE9792`, Release `50A676E2A52599D9008A06240D9BE3DB89AE760E20671E9D7270D0DF75134F2A`. 전체 소스·exe·runtime 해시는 각 실행의 `identity-before.json`과 `identity-after.json`에 남겼다.

</details>
