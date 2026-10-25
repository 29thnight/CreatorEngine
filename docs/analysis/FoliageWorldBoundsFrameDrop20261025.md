# 폴리지 월드 AABB 캐시 프레임 드랍 조사 (2026-10-25)

## 판정

기능 수용 검사는 통과했지만 성능 개선은 입증되지 않았다. 10만 인스턴스에서 캐시 적용 후 컬링 중앙값은 21.770ms에서 22.590ms로 증가했고, 완료 렌더 프레임 간격은 371.688ms에서 409.497ms로 증가했다. 캐시를 끈 상태도 이미 느리다. 따라서 심한 프레임 드랍 전체를 캐시의 신규 회귀로 단정할 수 없다.

현재 증거는 **전체 인스턴스 CPU 처리와 렌더 전달·대기 구간을 우선 조사해야 한다**는 판단을 지지한다. 수백 ms의 주원인은 아직 확정하지 못했다. 이 문서의 조사 순서는 측정 완료된 병목 순위가 아니라 후속 분석 우선순위에 대한 추정이다.

## 측정 대상과 한계

- 기준 소스: `master`, `3a5e4c2b2a294f61605886b27093fbb8080e3432`. 이 커밋에 있던 캐시에 미커밋 진단 배선과 빌드 보정을 추가한 실행 파일을 사용했다. 커밋 자체를 그대로 빌드한 결과가 아니다. 다른 미커밋 작업도 남아 있던 작업 트리이므로 결과를 깨끗한 해당 커밋의 성능으로 일반화하지 않는다.
- 실행 파일 runtime DLL SHA-256: `00580D630B1B027615FA3B6DA15999DDAB9A2B1BF14E59B0B87A84D79755C256`.
- Intel Core i7-8700K / NVIDIA GeForce RTX 2080 Ti / 드라이버 `32.0.16.1088`, Release DX12 실제 표시되는 Editor. GPU validation 꺼짐, `EngineEffectPackage=false` 빌드.
- 실제 게임 뷰포트는 native **510×250**, 업스케일러·프레임 생성 꺼짐. 요청한 호스트 창 크기 1280×720을 렌더 해상도로 취급하지 않는다.
- 카메라 `(10,15,-20)`, FOV 60도, far 60, 그림자 라이트 없음. 총 1만·10만 개 모두 **567개 가시**. 숨은 인스턴스도 게임 컬링의 전체 순회에는 포함된다.
- 전부 보이는 조건은 기존 4,096개 드로 예산 제한으로 정상 렌더가 진행되지 않아 제외했다. 이 결과는 1만·10만 개가 모두 보이는 장면의 성능을 대표하지 않는다.
- 동일 프로세스/실행 파일에서 production 컬링의 uncached/cached 경로를 전환했다. 각 규모 ABBA(기준·캐시·캐시·기준), trial마다 64 warm 프레임 제외 후 128개 표본, 모드별 총 256개 표본이다. 배치 생성 비용은 측정 구간에서 제외했다.
- 모드별 trial은 2개뿐이다. CPU 고립 측정이나 신뢰구간 검증이 아니며 실행 순서·열·스케줄링·Editor·명령 관측 비용이 남는다. GPU 시간은 trial 말미의 희소 표본이므로 GPU 성능 개선/악화 판정에 사용하지 않는다.

## 실제 엔진 수용 검사

별도 32개 fixture에서 실제 Scene/Component, 자산 reimport/rebind, 플레이 전환 경로를 사용했다. 각 플레이 검사 뒤 실제 렌더 프레임이 8개 초과 추가 완료되고 GPU geometry 상주와 유효 timestamp가 확인될 때까지 기다렸다. 독립 uncached 가시성 oracle과의 불일치는 모든 검사에서 0건이다.

| 검사 | 확인된 결과 |
|---|---|
| 편집 | 첫 인스턴스를 x=100000, 회전·음수/비균일 스케일로 변경. identity 유지, 바운드 재계산 1개, 가시 32→31 |
| 자산 교체 | 이름이 있는 동일 mesh의 GLB X 크기를 4배로 변경 후 실제 reimport/rebind. local extent X 0.4→1.6, identity 유지, 재계산 32개 |
| 삭제 | 편집한 인스턴스 삭제. 개수 32→31, 남은 31개 바운드 재계산 |
| 플레이 종료 | 저장된 Editor 데이터 32개와 초기 위치 복구 |
| 재진입 | 32개와 교체된 자산 바운드로 다시 로드 |

Debug/Release 컬링 회귀 로그 모두 `FOLIAGE_WORLD_BOUNDS_CACHE_OK`, `FOLIAGE_SEALED_CULLING_OK`를 기록했다. 1만 개 CPU 계약 검사에서 cold allocation 1회, warm allocation/transform 0회, 단일 편집 transform 1회였다. 이는 전체 엔진 프레임의 무할당을 뜻하지 않는다. 별도 ASan/장시간 ownership 스트레스 수용 결과는 이번 검사에 포함하지 않는다.

## 적용 전후 수치

시간 단위 ms. 전→후는 같은 실행 파일의 uncached→cached 전환이다.

| 항목 | 1만 | 10만 |
|---|---:|---:|
| CPU 컬링 중앙값 | 2.51275 → 2.45605 | 21.76990 → 22.59010 |
| CPU 컬링 p95 | 3.78410 → 3.63290 | 28.45190 → 29.27170 |
| 게임 업데이트 간격 중앙값 | 58.69185 → 58.25315 | 77.60750 → 78.28840 |
| 완료 렌더 프레임 간격 평균 | 165.59343 → 162.36739 | 371.68783 → 409.49745 |
| 캐시 backing array 할당 바이트 | 0 → 400,000 | 0 → 4,000,000 |
| trial 컬링 평균의 표본 CV | 9.42% → 1.72% | 0.56% → 5.84% |

1만 컬링 중앙값은 약 2.3% 감소, 10만은 약 3.8% 증가했다. 10만 완료 렌더 프레임 간격은 약 10.2% 증가했지만, 서로 다른 시간축과 적은 trial 때문에 이 차이를 캐시의 안정적인 회귀 폭으로 확정하지 않는다. 성능 개선 수용은 실패이며 반복 검증 전 최적화 성공으로 표시하지 않는다.

게임 업데이트 간격은 같은 FoliageComponent 업데이트 진입 사이의 wall time이다. 렌더 프레임 간격은 관측 구간 wall time / 실제 view 렌더 완료 수이며, 개별 GPU 실행 시간·디스플레이 Present 시간과 다르다. 서로 다른 프레임의 통계를 더하거나 빼서 미측정 CPU 비용을 계산하면 안 된다.

메모리는 컴포넌트 캐시 vector의 capacity × Entry 크기이며 여기서는 인스턴스당 40바이트다. 약 0.381/3.815MiB 증가다. 진단 표본 버퍼·렌더 캐시·프로세스 전체 working set과 구별한다.

## 확인된 구조와 원인 후보

### 1. 전체 순회와 교차 검사는 남는다 (소스·컬링 시간 확인)

[FoliageCulling.h](../../Engine/SceneRuntime/FoliageCulling.h)의 `WorldBoundsCache::Cull`은 캐시가 warm이어도 모든 인스턴스를 순회하고 identity/type/dirty 검사와 프러스텀 교차 검사를 수행한다. 월드 바운드 변환만 제거한다. 이번 구현에는 공간 계층으로 검사 대상을 줄이는 단계가 없다.

실제로 10만 컬링 중앙값 22.59ms는 60Hz의 16.67ms 예산보다 크다. 다만 이 wall time에는 스케줄링 영향이 포함되므로 교차 검사 함수 단독의 CPU 실행 시간이라고 단정하지 않는다. 전체 프레임 지연 중 정확한 기여도는 별도 scope가 필요하다.

### 2. 렌더 입력 경로에서 별도의 전체 바운드 생성이 남는다 (소스 확인, 시간 미계측)

[PrimitiveRenderProxy.cpp](../../Engine/RenderEngine/PrimitiveRenderProxy.cpp)의 `FoliageRenderProxy::CaptureDrawSources`는 호출할 때 로컬 `draws` vector를 만들고 전체 인스턴스 수로 reserve한다. 타입별 instanceMap을 순회하며 각 인스턴스의 worldMatrix와 `math::transform(bounds, worldMatrix)` 결과를 DrawSource에 기록한다. 이 함수에는 `m_isCulled`에 따른 제외가 없다. 후단의 view 컬링과 별개다.

컴포넌트 캐시는 이 함수의 바운드 생성·버퍼 확보를 제거하지 않았다. 호출당 임시 저장소와 데이터 작성이 남는 것은 확인했지만 **실제 호출 빈도·할당 바이트·시간 비중은 미계측**이다. 이것을 수백 ms의 주범으로 확정하거나 매 프레임 10만 개가 반드시 복사된다고 보고하지 않는다.

### 3. GPU 연산 외의 전달·대기가 길다 (상태 표본 확인, 원인 미확정)

10만 trial 말미 GPU 실행 표본은 기준 6.059/5.649ms, 캐시 6.243/6.238ms였다. 같은 상태 응답의 `admission.lastAdmissionAgeMs`는 기준 364.670/369.058ms, 캐시 362.730/384.336ms였다. 렌더 admission 지연이 큰 상태는 확인됐다.

이 age는 CPU scope 소요 시간이 아니고 GPU 표본과 동일 프레임이라고 보장되지 않는다. `gpuAdmissionWaits`·`producerPacingWaits` 같은 누적 카운터만으로 대기 시간을 배분할 수도 없다. CPU producer 지연, snapshot 생성/전달, admission 정책·display lease·pacing 대기, 관측 비용을 구분해야 한다. GPU 표본이 짧다는 사실만으로 GPU/드라이버/동기화 문제를 완전히 배제하지 않는다.

### 4. 캐시 자체 비용 증가 후보 (추정)

별도 40바이트 Entry 배열 접근, identity/type 비교와 분기가 바운드 변환 절약을 상쇄했을 수 있다. 인스턴스와 캐시의 동시 순회에 따른 메모리 접근도 후보이다. CPU hardware counter·교차 검사 단독 scope·반복 통제가 없어 cache miss나 branch miss를 원인으로 확정하지 않는다.

## 후속 원인 분석 절차

아래 순서는 조사 효율에 대한 추정이며 병목 순위나 예상 개선율이 아니다. 이번에는 전체 CPU 프로파일러가 `state=stopped`, `retainedFrames=0`이었다. 기존 profile JSON은 상태 기록일 뿐 CPU 타임라인 캡처 증거가 아니다.

1. **전체 프레임 캡처를 먼저 활성화한다.** 동일 고정 장면에서 게임·렌더·collector·표시 스레드의 capture를 보존하고 frame/scene/view ID로 연결한다. 이벤트 유실·clock 유효성·실제 표시 상태를 검증한다. 원격 명령/상태 polling 비용도 관측을 줄인 대조 실행과 비교한다.
2. **게임 컬링을 분해한다.** 전체 순회, dirty 재계산, 교차 검사, 진단 기록 시간을 구분하고 warm dirty 수가 0인지 확인한다. uncached/cached 순서를 교대하며 여러 독립 실행을 확보한다. 개선율이나 목표 달성률은 그때 계산한다.
3. **렌더 입력 생성과 전달을 분해한다.** `CaptureDrawSources` 호출 수·처리 수·할당 수/바이트·시간, proxy payload publish/copy, Scene snapshot과 view 컬링을 같은 프레임에서 측정한다. 화면 밖 인스턴스를 포함하는 shadow/다른 view 소비자를 확인한다.
4. **렌더 admission·표시 대기를 분해한다.** source/admitted/completed/display frame을 연결해 producer 지연과 admission/lease/pacing/GPU fence 대기를 별도 scope로 기록한다. age·누적 카운터와 실제 대기 시간을 혼동하지 않는다.
5. **측정된 병목만 수정하고 재수용한다.** 무관한 캐시 복제나 raw pointer 공유로 ownership 배선을 우회하지 않는다. 수정 후 동일 가시 부하에서 컬링·전체 프레임·할당·메모리와 편집/교체/삭제/플레이 전환을 다시 검사한다. 모든 인스턴스가 보이는 대규모 조건은 기존 드로 예산 문제를 별도 해결/검증한 뒤 추가한다.

소유권은 기존 Scene/Component/asset generation에 유지한다. 비소유 파생 캐시에 자산 소유권을 추가하지 않고 coherent publish 이후 무효화, 삭제/슬롯 이동의 identity 검증, quarantine와 해제 순서를 보존한다. 버퍼 재사용은 소비가 끝난 경계를 확인한 뒤 적용하며 GPU/렌더 소비 중 메모리를 덮어쓰지 않는다. 할당을 줄이려고 필요한 proxy publish나 자산 교체 배선을 제거하지 않는다.

## 보존한 근거와 재현 경계

[측정 근거 JSON](FoliageWorldBoundsFrameDrop20261025.evidence.json)에 원시 시간 표본, trial별 실제 렌더 완료 수, summary, 수용 상태, GPU/admission 표본과 profiler 상태를 보존했다. 명령 인증 정보나 fixture 자산은 포함하지 않는다. 이 근거는 당시 결과를 재검산하는 자료이며 새 환경에서 같은 수치를 보장하지 않는다.

로컬 원본은 저장소 루트 기준 아래 경로에 남아 있다. Build 경로는 로컬 기록이며 게시 다운로드 링크가 아니다.

- 성능: `Build/Verification/FoliageEngine/Run-7d01fc340b0c43dd8dbff8ac3f2c6bbf`
- 렌더 완료 확인을 강화한 별도 수용: `Build/Verification/FoliageEngine/Run-520ca960bf32415d881ca47b28e70af7`
- 최종 빌드: `Build/Verification/FoliageEngine/build6.log`
- 회귀: `Build/Tests/FoliageCulling/x64-Release/test.log`, `Build/Tests/FoliageCulling/x64-Debug/test.log`

진단 배선은 `FoliageComponent.cpp/.h`, `FoliageCulling.h`, `FoliageCacheProbe.inl`, 명령 등록에 있었고 측정 스크립트는 `Tools/regression/measure-foliage-world-bounds.ps1`, `scale-foliage-probe-glb.py`, `summarize-foliage-world-bounds.py`다. **이번 보고서 커밋은 해당 미커밋 구현·도구를 게시하지 않는다.** 보고서만으로 새 checkout에서 측정 명령을 재실행할 수 있다고 주장하지 않는다. 후속 실행 전 진단 도구의 별도 검토·게시와 최종 소스/바이너리 해시 확보가 필요하다.
