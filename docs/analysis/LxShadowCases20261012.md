# LX 그림자 검증 장면 확대 (2026-10-12)

후속 5번의 검증 장면을 `dx12.skinning` 선택 모드에 추가했다. SceneHost·실제 DX12 GPU readback에서 12개 장면 × 두 포즈 × 캐스케이드 3장, 구성당 72개 indexed/meshlet 깊이·행렬 비교를 수행한다. 기본 테스트는 기존 동작을 유지하며, `-ShadowCaseSuite`에서만 확대 장면을 실행한다.

## 장면과 독립 대조 조건

| 장면 | 입력·확인 조건 |
|---|---|
| base | 스킨드 스트립 + 비스킨드 사각형, 두 포즈, 양면 |
| alpha-pass / alpha-equal / alpha-reject | 실제 재질 그래프 BSDF Alpha 소켓 0.5. 사각형에 Masked 설정, cutoff 0.4 / 0.5 / 0.6. 앞 두 장면은 기준 커버리지와 같고 마지막은 사각형 깊이를 제거하며 스킨드 깊이는 유지해야 함 |
| face-front / face-back / face-double | 원래 삼각형 순서·뒤집은 순서의 단면, 뒤집은 순서의 양면. 단면 둘 중 하나는 기준 커버리지, 다른 하나는 사각형 제거 커버리지여야 함. 양면은 기준 커버리지 유지. 이름은 입력 와인딩 표식이며 광원에서의 실제 앞·뒤 판정은 결과로 확인 |
| transform | 사각형에 음수·비균일 스케일 (-1.7, 0.6, 1.2), 비대각 성분 0.3, 평행이동 (8,3,2). 일부 캐스케이드에서 안 보이는 경우를 허용하되 맵의 스킨드 대조군은 남아야 함 |
| boundary-near / boundary-far | 첫 split 9.1의 양쪽에 중심 9.09 / 9.11. 기울인 사각형의 뷰 깊이 범위가 split을 걸쳐야 함(약 8.69~9.49 / 8.71~9.51) |
| far | 광원 쪽으로 1000 이동. 뷰 깊이 -347.770905, 카메라 near 0.1·far 100 밖에 있으며 실제 그림자는 남아야 함 |
| multi | 17×17 정점·512 삼각형 격자. 모든 289개 정점을 참조하므로 정점 64개 제한의 단일 메시렛으로 표현할 수 없음. 중간 메시렛 누락으로 실제 깊이 변화가 발생해야 함 |

방향광은 normalize(0.3,-1,0.4)로 모든 장면에서 비스듬하다. 포즈 검사는 본 이동이 스킨드 띠에 영향을 주고 비스킨드 카메라 대조군을 옮기지 않는 기존 검사를 유지한다.

## 동치·변이 판정

깊이는 2048×2048 D32Float 원본이다. 72개 쌍 모두 유한·비어 있지 않은 참조 맵, 행렬 비트 동치와 깊이 비트 동치를 요구한다. 허용 오차는 없다. 그림자 콜백의 실제 mesh dispatch 제출 횟수도 144개의 경로·캐스케이드 표식으로 확인한다. 이는 GPU 생존 메시렛 수가 아니다. 별도 오프라인 검증기가 원본 파일의 길이·SHA256 동치와 위 알파·면 정책 대조 조건 및 경계·먼 캐스터 입력 표식을 검사한다.

변이는 격리 프로젝트의 `LXSceneShadowMS`에만 적용한다.

```slang
const bool visible = LXSceneMeshletVisible(remap, vertexCount, lane, lx_shadowViewProjection)
    && !((group.x == 0u && vertexCount == 4u) || group.x == 1u);
```

네 정점 사각형 메시렛과 격자의 group 1을 제외한다. 스킨드 스트립의 단일 메시렛·indexed 경로·카메라 셰이더는 그대로다. 이미 알파나 면 정책으로 보이지 않는 캐스터의 제외는 깊이를 바꾸지 않아야 하고, base·alpha-pass·alpha-equal·face-double·transform·boundary-near·boundary-far·far·multi는 각 포즈에서 적어도 한 캐스케이드에 실제 누락을 남겨야 한다. 전체 맵 소실·추가 깊이·행렬 차이·GPU 오류는 예상 거부로 인정하지 않는다.

**후속 5번 완료(위의 선택한 합성 장면 범위).** 현재 소스로 빌드한 Debug/Release 정상은 각각 72개 비교·독립 대조 조건을 모두 통과했다. 누락·추가·불일치·최대 깊이 오차 모두 0, 행렬도 비트 동치다. 두 구성의 변이는 실제 누락으로 거부됐다. 네 실행의 GPU validation 활성·문제 0·누락 메시지 0, quit 성공과 실행 전후 검사 대상 소스·binary·격리 셰이더 해시 일치를 확인했다. 정상 종료 코드 0, 변이는 의도한 테스트 실패 코드 4다.

| 변이 장면 | C0 누락 | C1 누락 | C2 누락 |
|---|---:|---:|---:|
| transform | 0 | 2,851 | 154 |
| boundary-near | 6,143 | 1,118 | 65 |
| boundary-far | 6,144 | 1,119 | 64 |
| far | 15,291 | 2,797 | 143 |
| multi | 2,564 | 477 | 24 |

위 값은 Debug/Release 및 두 포즈에서 같다. transform의 C0는 원래 사각형이 보이지 않아 누락 0이 옳고, C1/C2가 검사를 성립시킨다. alpha-reject와 원래 와인딩 단면(face-front)은 이미 사각형을 거부하므로 변이도 깊이 변화 0이다. 반대로 뒤집은 단면(face-back)과 양면은 사각형 깊이를 남겼다. 모든 변이의 추가 깊이는 0이며 불일치 텍셀은 누락 텍셀과 같다.

## 재현·증거

현재 소스로 Editor Debug/Release를 빌드한 뒤 다음을 실행한다.

```powershell
& Tools/regression/verify-lx-shadow-baseline.ps1 `
    -Configuration Debug `
    -FixtureProject Build/Obj/LxShadowP0/Fixture `
    -OutputDirectory Build/Verification/ShadowCases-New/Normal-Debug `
    -ShadowCaseSuite
```

변이는 새 출력 디렉터리에서 `-ExpectedVisibleMeshletRejection`을 추가한다. Release는 구성을 바꾼다. 디스크 30GiB 하한을 확인하고 격리 프로젝트·설정을 사용한다. 깊이 파일은 `captures/<case>/{indexed,meshlet}-{bind,posed}-C{0,1,2}.f32`, 행 우선 little-endian float32다. 구성당 정상 대표 원본 144개(2.25GiB)를 보관한다. 예상 거부한 변이 픽셀은 삭제 기록을 남기고, 로그·판정·행렬·manifest·해시·변이 셰이더는 보존한다.

- [실행 스크립트](../../Tools/regression/verify-lx-shadow-baseline.ps1), [오프라인 검증기](../../Tools/regression/assess-lx-shadow-cases.ps1)
- [Debug 정상 판정](../../Build/Verification/ShadowCases-20261012/Normal-Debug/verdict.json), [72쌍·대조 조건 상세](../../Build/Verification/ShadowCases-20261012/Normal-Debug/cases.json), [원본 해시](../../Build/Verification/ShadowCases-20261012/Normal-Debug/capture-manifest.json)
- [Release 정상 판정](../../Build/Verification/ShadowCases-20261012/Normal-Release/verdict.json), [72쌍·대조 조건 상세](../../Build/Verification/ShadowCases-20261012/Normal-Release/cases.json), [원본 해시](../../Build/Verification/ShadowCases-20261012/Normal-Release/capture-manifest.json)
- [Debug 변이 판정](../../Build/Verification/ShadowCases-20261012/Mutant-Debug/verdict.json), [누락·대조 조건 상세](../../Build/Verification/ShadowCases-20261012/Mutant-Debug/cases.json), [변이 규칙·해시](../../Build/Verification/ShadowCases-20261012/Mutant-Debug/mutation.json)
- [Release 변이 판정](../../Build/Verification/ShadowCases-20261012/Mutant-Release/verdict.json), [누락·대조 조건 상세](../../Build/Verification/ShadowCases-20261012/Mutant-Release/cases.json), [변이 규칙·해시](../../Build/Verification/ShadowCases-20261012/Mutant-Release/mutation.json)
- [전체 판정·원본 대조](../../Build/Verification/ShadowCases-20261012/overall.json)
- [Debug 빌드](../../Build/Verification/ShadowCases-20261012/build-Debug.log), [Release 빌드](../../Build/Verification/ShadowCases-20261012/build-Release.log)

각 실행의 `identity-before.json`·`identity-after.json`은 소스·실행 스크립트·오프라인 검증기·exe/runtime·실제 격리 셰이더 해시를 기록한다. `fixture-inputs.json`은 복사·변이 적용 후 프로젝트 입력을 기록한다. 원본·변이 저장소 셰이더가 아니라 실제 사용하는 격리 셰이더까지 실행 전후 일치를 요구한다.

정상·변이의 indexed 깊이 및 모든 행렬 해시를 다시 대조했고, meshlet 깊이의 해시 차이도 실제 불일치 표식과 일치했다. 정상 base의 원본 깊이·행렬은 이전 [3장 동치 검사](LxShadowEquivalence20261012.md)의 동일 구성 원본과도 비트 동치다. 이전 원본은 보존했다. 정상 확대 장면의 원본 144개씩은 보존하며, 예상 거부 직후 변이 144개씩을 삭제하고 ledger에 실제 회수량 약 2.25GiB씩을 기록했다. 변이 픽셀 재생은 불가능하며 판정·해시는 남아 있다.

검증 runtime SHA256: Debug `1A24E254F224685761DC318EF9866EF0328538342A2D6358098EF2B85BEBEEB8`, Release `175F57542F9B5328710F3CC5CB45300E19E282AF9AD729CF53CF9090C067F7CA`.

## 수용 범위

선택한 합성 장면에서 정책이 실제 깊이에 영향을 주고, indexed/meshlet 경로가 동치이며 보이는 메시렛 누락을 거부하는 기능 검사다. 텍스처의 공간 알파 패턴·mip·샘플러, 임의 실제 캐릭터, 모든 변환과 모든 캐스케이드 경계, 독립 정답 형상 전체를 수용한 것은 아니다. 새로운 compute 생존 목록 검증도 아니다.

GPU validation 활성·문제 0·누락 메시지 0, 정상 quit와 소스·binary 안정성을 별도로 요구한다. 레이어 메시지 1건은 활성 안내다. 기존 profiler 종료 진단은 해결 범위 밖이며 검증 실행 시간은 성능 수용에 사용하지 않는다. Vulkan 실행·교차 비교는 PHASE 4.9다. 다음 목록 항목은 7번의 계측 누락 해결·반복 성능 측정이다.
