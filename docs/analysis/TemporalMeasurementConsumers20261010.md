# TR0 — 계측 소비자·예산·Player 기록 체크포인트

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/base0_artifacts.py:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/base0_artifacts.py#L13) — `import struct`
- [Tools/regression/base0_artifacts.py:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/base0_artifacts.py#L20) — `raise ValueError(message)`
- [Tools/regression/base0_artifacts.py:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/base0_artifacts.py#L44) — `expected_order = [i for i, p in enumerate(passes) if not truth(p["culled"])]`
- [Tools/regression/audit-rg8-adoption.py:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L25) — `raise ValueError(message)`
- [Tools/regression/audit-rg8-adoption.py:56](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L56) — `"skybox": Path(manifest["skyBoxPath"]).name}`
- [Tools/regression/audit-rg8-adoption.py:121](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-rg8-adoption.py#L121) — `assignment = {p: (i, b["queue"]) for i, b in enumerate(batches) for p in b["passes"]}`
- [Tools/regression/measure-rg8-adoption.ps1:32](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/measure-rg8-adoption.ps1#L32) — `$estimatedTelemetryGiB=if($TimingOnly){[math]::Max(1,$NormalCandidates/64.0)}else{1}`
- [Tools/regression/measure-rg8-adoption.ps1:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/measure-rg8-adoption.ps1#L34) — `$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/measure-rg8-adoption.ps1:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/measure-rg8-adoption.ps1#L35) — `$out=[IO.Path]::GetFullPath($OutputDirectory)`
- [Engine/EngineDiagnostics/ProfileService.cpp:80](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/EngineDiagnostics/ProfileService.cpp#L80) — `static std::mutex* lock = new std::mutex();`
- [Engine/EngineDiagnostics/ProfileService.cpp:95](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/EngineDiagnostics/ProfileService.cpp#L95) — `profiler_service::profiler_service()`
- [Engine/EngineDiagnostics/ProfileService.cpp:107](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/EngineDiagnostics/ProfileService.cpp#L107) — `profile_tick profiler_service::ticks_per_second()`
- [Tools/regression/native_real_measurement.py:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/native_real_measurement.py#L5) — `raise ValueError(message)`
- [Tools/regression/native_real_measurement.py:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/native_real_measurement.py#L17) — `for field in ('realFrameId', 'publicationFrameId', 'viewId', 'sceneEpoch',`
- [Tools/regression/native_real_measurement.py:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/native_real_measurement.py#L29) — `check(int(p['publicationFrameId']) == row['gpu']['frame'] and int(p['viewId']) == row['gpu']['viewId'] and`
- [Tools/regression/summarize-material-profile.py:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/summarize-material-profile.py#L15) — `import struct`
- [Tools/regression/summarize-material-profile.py:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/summarize-material-profile.py#L21) — `raise ValueError(message)`
- [Tools/regression/summarize-material-profile.py:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/summarize-material-profile.py#L24) — `class Reader:`
- [Tools/regression/NativeRealMeasurementGate.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/NativeRealMeasurementGate.ps1#L2) — `function Assert-NativeRealMeasurement($Snapshot) {`
- [Tools/regression/NativeRealMeasurementGate.ps1:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/NativeRealMeasurementGate.ps1#L6) — `foreach($field in @('schemaVersion','valid','frameKind','realFrameId','publicationFrameId',`
- [Tools/regression/NativeRealMeasurementGate.ps1:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/NativeRealMeasurementGate.ps1#L19) — `if(![uint64]::TryParse([string]$p.$field,[ref]$value) -or $value -eq 0){throw "Invalid native-real identity/extent: $name.$field"}`
- [Tools/regression/audit-material-measurement.py:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-material-measurement.py#L15) — `spec = importlib.util.spec_from_file_location('material_profile', Path(__file__).with_name('summarize-material-profile.py'))`
- [Tools/regression/audit-material-measurement.py:92](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-material-measurement.py#L92) — `parser = argparse.ArgumentParser(description=__doc__)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 현재 판정

golden/profiler에 이어 명시적 CPU render-submit 예산 소비자와 실제 Development Player의 연속 presenter 기록을 검증했다. **TR0는 progress·기성 0, PHASE 4.5는 12/71(17%) 유지**다. 프로젝트의 성능 목표 수치, 전체 기존 소비자/진단 UI 수용, actual generated 출력 신원은 아직 완료하지 않았다.

이번에 `ProfileService.cpp`의 presenter admission을 수정하고 SDK 활성 Editor/Player Debug/Release를 빌드했다. 이전 golden/모션/DLSS 종료 기록은 당시 바이너리의 증거로 보존한다. 이번 새 바이너리에는 native profiler와 Player 기록 회귀를 실행했으며, 이전 전체 픽셀/SDK 품질 행렬을 새 바이너리로 다시 실행했다고 표기하지 않는다.

## 소비자 목록과 단위

| 소비 경로 | 판정 단위와 현재 근거 | 잔여 |
|---|---|---|
| `base0_artifacts.py`·`render.pbr.capture` | actual native real manifest·attachment·GPU timing; 두 구성 실제 전경·반복 픽셀 0·29변이. 이전 measurement checkpoint | 모든 기존 golden 호출 드라이버의 제외 설정 전수 |
| `summarize-material-profile.py`·`.ceprof` | CPU render-submit와 GPU pass를 실제 view/frame/submission에 결합. `frameMs`는 engine frame 경계 시간으로 명시. 12 wire 변이와 reader 계약 검사 | 모든 downstream 소비자/예산에서 동일 축 준수 |
| `measure-material-scene-performance.ps1` | 공통 native preflight·62거부 검사에 이어 2026-10-11 실제 D/R empty/Aria stage·lossless 기록·정상 종료·저장 population/geometry/전경 감사를 통과 | 원래 CreatorRobot fixture 부재; 전체 제품 해상도·마스터 비교·제품 예산 수용은 별도 |
| `analyze-render-budget.py` (신규) | 단일 native 실프레임 view/scene·명시적 render/display extent·CPU render-submit p95. 실제 D/R capture에서 13거부·예산 초과 대조 검사 | 프로젝트 budget 정책 승인/그 정책의 실제 성능 판정. 테스트용 threshold는 제품 예산이 아님 |
| `Record-TemporalEvidence.ps1`·Player presenter | actual v7 axis3 owner 관측·QPC return·연속 sequence. 실제 D/R native Player 기록·normal exit·loss 0 | actual generated 출력 신원, FG-on 조건. aggregate interpolated count는 개별 표시 신원이 아님 |
| `analyze-temporal-evidence.py` | 별도 raw/CSV hash·instrument/clock/axis·real frame와 immutable presenter 결합. physical/OS/SDK presentation과 CPU 반환을 분리. 27계약 검사 및 실제 D/R 반복 Present sequence 연결 검사 | 외부 원본/기구를 이용한 실행 수용. 반복 source는 외부 `presenterSequence`가 정확히 일치해야 하며 누락 시 ambiguous로 거부 |
| `measure-rg8-adoption.ps1`·`audit-rg8-adoption.py` | native preflight·환경 제외 요청·저장된 CPU/GPU provenance와 측정 GPU frame/view/extent 결합 추가. 새 actual D/R snapshot으로 52거부 검사 통과 | 전체 RG8 warmup/forward-reverse/queue workload 비교 실행·수용 미수행 |
| Scene/Game overlay·ProfilerViewer | 표시용 FPS/engine-frame 시간과 view GPU timing 진단 | 게이트로 사용하지 않으며 모든 표시 라벨/metadata 축 감사는 잔여 |

CPU submit duration은 게임 루프 전체 frame time·GPU 프레임 wall time·표시 FPS·input-to-photon과 다르다. GPU 개별 pass p95를 합쳐 GPU frame budget으로 사용하지 않는다. 원본에 없는 generated 타임스탬프나 표시 신원을 현재 SDK 설정에서 보충하지 않는다.

## 수정한 두 결함

1. 기록 도구의 writer 상태 poll은 `profile.save status`의 현재 `writer.state`를 읽어야 한다. 이전 `recording.state`는 존재하지 않아 실제 실행이 중단됐다. 새 Player 드라이버는 패키지 사본만 변경하고 자신의 PID만 종료하며 정상 quit/exit 0을 요구한다.
2. Record는 비동기이며 요청한 first frame과 실제 첫 collected boundary가 다를 수 있다. GPU admission은 첫 실제 경계 준비를 기다리지만 presenter admission은 이를 확인하지 않아 오래된 prefix 3측정이 귀속 프레임을 찾지 못했다. 실패 기록에는 `sourceDroppedEvents=3`, `lateGpuSpans=3`, complete=false가 남았고 strict gate가 거부했다. presenter도 actual frame range가 준비된 뒤만 받아들이도록 수정했다. 기존 손실 집계와 complete 요구는 유지한다.

독립 C++ 회귀는 Record(20) → 경계 확정 전 presenter → 실제 첫 boundary 25 → presenter → frame 26 → Pause를 실행한다. 앞 관측은 받지 않고 뒤 관측만 frame 26에 들어가며 capture complete가 유지돼야 한다. 기존 codec·late frame ownership·presenter bounded loss·진단 codec 회귀와 함께 실행했다. Python reader 회귀 10개도 통과했다.

실패 원본은 `tr0-player-Release-*`의 최초 두 실행에 보존했다. 재시도 결과만으로 원본 complete/loss 값을 덮어쓰지 않는다. native Player에서 생성 출력·OS 표시·물리 photon 타임스탬프는 모두 unavailable이며 CPU presenter 검사가 통과해도 pacing/latency acceptance는 false다.

## 외부 계측 소비자 연결 후속

실제 Player 기록에서 동일 real-frame source를 반복 Present한 source 신원은 Debug 182개·Release 181개였다. 기존 importer는 source만으로 유일 관측을 요구해 이런 기록을 전체 거부했다. v6/v7의 모든 owner 관측을 보존하고 외부 CSV의 `presenterSequence`와 source의 네 신원 필드를 함께 검사하도록 수정했다. 기존 CSV는 해당 source가 단일 관측일 때만 허용한다. 반복 관측 중 최신 값을 임의로 선택하지 않는다. pacing 중복 검사도 source·sequence·kind·ordinal을 함께 사용한다. latency는 동일 실프레임의 반복 trial을 계속 거부한다.

`TemporalExternalEvidenceTests.py`는 27계약 검사를 통과했다. 모호한 sequence, 다른 source의 sequence, 생성 프레임의 real 오표기, ordinal 초과, native override, generation 불일치, 손실/미완료 provenance, 혼합 scene/extent, hash 불일치, 비단조/비유한 timestamp, CPU/SDK의 물리 지연 오표기, 예산 초과를 검사한다. `minimumSamples`의 소수값이 정수로 잘려 허용되던 문제도 수정했다. 합성 계측 입력과 decoded-profile test double은 importer 계약 검사만 증명한다.

기존 actual native Player capture를 수정하지 않고 실제 reader를 통해 Debug 476건·Release 429건의 sequence 연결을 모두 확인했다. 같은 실제 파일을 FG-on으로 잘못 표기하고 합성 generated CSV를 붙인 입력은 actual presenter mode 불일치로 거부됐다. 원본 SHA256은 전후 동일하다. 결과 정본은 `Artifacts/PR169/tr0-external-association.json`, 로그는 `tr0-external-association-tests.log`다. 새 Player 실행·엔진 재빌드·실제 외부 기구 측정 또는 실제 generated 표시 검증은 이번 후속 범위가 아니다. physicalAcceptance와 generatedOutputAcceptance는 false다.

## 실행 정본과 다음 순서

기존 성능 소비자 후속에서는 `NativeRealMeasurementGate.ps1`로 material의 live preflight를 분리했다. 필수 필드와 true/false 타입을 검사하므로 `deepDvcApplied` 누락을 false로 취급할 수 없다. missing CPU/GPU metadata 34건·잘못된 temporal 상태/extent/신원 26건·알 수 없는 throughput 축 2건을 거부했다. RG8 collection은 같은 preflight를 사용하고 각 normal row에 두 provenance와 축을 저장한다. 파일 감사의 `native_real_measurement.py`는 이를 다시 확인하고 GPU publication/view/display extent를 측정 행에 결합한다. uint64 command 신원은 원래 JSON decimal string이므로 숫자 신원과 비교할 때 명시적으로 해석한다. 해당 reader 검사는 52변이를 거부한다. 과거 RG8 행의 누락 provenance는 새 감사에서 실패하며 현재 설정으로 보충하지 않는다.

실제 실행은 새 격리된 commandlet `tr0-live-consumer-Debug-01b0fe3157674fd2bdf1ad63a24da110`과 `tr0-live-consumer-Release-62b5e9d2c4b341acafe86f2ae722d9d8`에서 각 2개의 `dx12.live` snapshot을 얻고 normal exit 0을 확인했다. 엔진 소스 변경·재빌드는 없으며 앞 체크포인트의 바이너리를 사용했다. `Artifacts/PR169/tr0-existing-consumer-checkpoint.json`이 runtime hash와 actual 결과 경로를 묶는다. material의 전체 empty/robot 성능 실행이나 RG8 mode 비교 결과를 이번 preflight 검사로 대체하지 않는다.

진단 UI 정찰에서 Scene overlay의 `Scene FPS`는 `promotionCount` 증가/경과 시간으로 계산되는 뷰 텍스처 갱신률임을 확인했다. ProfilerFrameOverview는 `tick_begin..tick_end` 엔진 경계 시간을 그리며 60/30 참고선을 쓴다. 이 값들은 physical scanout FPS 또는 승인된 제품 성능 예산이 아니다.

UI 후속 수정은 Scene 기본 HUD를 `Scene updates .../s`, 통계 HUD를 `View updates .../s`, 메뉴를 `View update rate`로 바꿨다. GPU 수치는 `DX12GpuProfiler::RebuildTimings`에서 pass duration을 더하므로 Scene와 Profiler 모두 `GPU pass sum`으로 표기한다. GPU wall time으로 바꾸거나 SDK generated count를 포함하는 계산 변경은 없다. Profiler Rendering은 `Render timing` 아래 CPU live render(all views), GPU pass sum(selected view), real view renders를 구분하고 중첩 pass가 합계에 중복될 수 있음을 설명한다. FrameOverview는 `Engine frame boundary time (ms)`와 16.67/33.33ms 참고선·제품 예산 미설정을 표시하며 hover에도 engine boundary를 명시한다. 긴 라벨은 값 열의 위치를 실제 글자 폭에 맞추고 참고 설명은 패널 폭에 따라 줄바꿈한다.

이 UI 변경의 빌드·실행 정본은 `Artifacts/PR169/tr0-ui-label-checkpoint.json`이다. 빌드 및 native live smoke와 육안/픽셀 라벨 검증은 별도 판정이며, 이 체크포인트는 육안/픽셀 수용을 주장하지 않는다. 전체 이전 golden/모션/SDK 품질 행렬을 새 UI 바이너리에 재실행한 것으로 간주하지 않는다.

`Artifacts/PR169/tr0-consumer-checkpoint.json`이 실제 run 경로, 최종 바이너리 SHA256, native 프로파일과 Player sequence/GPU match 수 및 테스트 로그를 묶는다. 전체 소비자·기존 예산/진단 표면의 판정 연결과 actual generated 신원 범위를 수용한 뒤 TR0 종료를 판정한다. master 대비 회귀율·제품 성능 목표·물리 지연 개선은 이번 결과로 산정하지 않는다.

## 2026-10-11 실제 material 소비자 실행

원래 CreatorRobot/LX_CookFixture 입력이 없어 기존 기본 경로는 보존하고 `-ModelName`·`-CreateEmptyScene`으로 격리된 Aria 입력을 지원했다. 원본 Dynamic_CPP를 변경하지 않고 사본에서 카메라·광원·모델을 생성한다. 실제 `model.loadcached`의 generation 자동 복구도 성공했다. HTTP에 노출되지 않은 `model.async` 호출, 셰이더 복사 대상 폴더 누락, 비동기 결과의 중첩 형식과 quit 후 서비스 종료를 실행 드라이버에서 처리했다.

live snapshot이 완전해도 별도 상태 문자열 조회만 잠금을 얻지 못해 busy가 될 수 있었다. 완전한 CPU/GPU metadata를 판정 기준으로 유지하고 부분 snapshot만 재조회한다. 고정 3초 모델 대기에서는 후속 geometry 업로드가 측정에 들어갔다. 이제 실제 프레임 진행·resident geometry·2초 이상 upload 안정성을 확인한 뒤 Record를 시작한다. 측정 구간 업로드 증가·native provenance·complete/loss 게이트는 그대로 유지했다.

`audit-material-measurement.py`는 저장된 단일 view/scene/extent population을 시작·종료 CPU/GPU provenance에 연결하고 모델 geometry 안정성을 확인한다. 캡처의 attachment·graph·유한 픽셀 검사와 실제 draw/전경 깊이 검사를 함께 수행한다. 드라이버의 캡처 이후·quit 이전에 연결했으며, 최초 D/R 성공 기록에는 동일 감사를 별도로 실행했다. 최종 연결 버전은 `Mat9Perf-Release-tr0-1011-wired`에서 Record→캡처→감사→quit 전체 경로와 normal exit 0을 다시 통과했다.

`Mat9Perf-Debug-tr0-1011-warm`은 empty/model CPU 표본 213/222개, GPU 7029/13542건 전부 귀속, CPU render-submit p95 5.9382/11.7605ms다. `Mat9Perf-Release-tr0-1011-warm`은 CPU 215/236개, GPU 7095/14396건 전부 귀속, p95 1.9643/2.009ms다. 두 구성 모두 complete/native eligible·normal exit, 실제 draw 3개·전경 깊이 픽셀 858개를 확인했다. 이는 offscreen 단일 view 332×182의 측정 도구 검증이며 전체 viewport 성능·픽셀 품질·마스터 대비 회귀율 판정이 아니다.

최초 `-first`(미노출 명령), `-retry`(busy 재조회 과잉으로 incomplete/loss), `-final`(초기 업로드가 측정에 진입) 실패 기록은 그대로 보존했다. 정상 결과로 실패 원본을 덮어쓰거나 손실을 무시하지 않았다. 정본은 `Artifacts/PR169/tr0-material-consumer-checkpoint-20261011.json`이며 runtime/model/도구 hash와 실제 run/audit 경로를 묶는다. 엔진 재빌드는 이번 범위가 아니다. RG8 전체 workload·진단 UI 육안 수용·제품 budget·actual generated/외부 기구 수용은 남아 TR0 기성 0과 12/71(17%)를 유지한다.
# 검증 산출물 보존 안내 — 2026-10-11

대표 native/DLSS/golden/FSR/XeSS 원본과 실패 분석에 필요한 원본은 유지하고, 그 밖의 raw 픽셀은 삭제 ledger와 함께 정리했다. 모든 프로파일·판정·manifest·로그는 유지하지만 대표 외의 이전 픽셀 판정은 과거 요약이며 재검사 가능한 원본이 아니다. 이번 계측 수용의 .ceprof와 material 캡처는 보존했다. 실제 72.01 GiB 회수와 이후 용량/보존 정책·검증은 [산출물 정리 기록](ArtifactRetention20261011.md)에 있다. TR0/페이즈 완료 판정은 변경하지 않았다.

## 2026-10-11 측정 표면 후속 정본

SDK 활성 Debug/Release 검증과 측정 표면·기존 소비자 변경은 [TemporalMeasurementSurfaces20261011.md](TemporalMeasurementSurfaces20261011.md)를 참조한다. 과거 기록은 당시 범위이며, 이번에도 TR0 전체 기성은 0이다.



2026-10-11 후속: 현재 바이너리의 BASE-0 실제 population 및 용량 정리는 [TemporalBase0ConsumerWorkload20261011](TemporalBase0ConsumerWorkload20261011.md)과 tr0-base0-workload-20261011/checkpoint.json이 정본이다. SDK D/R 빌드·400표본·204캡처 수용, raw 대표 성공 2/필요 실패 2 및 로그 보존, 여유 93.21GiB. 이전 runtime 해시의 검증은 당시 실행 증거로 유지한다. TR0 progress·기성 0/PHASE 4.5 12/71 유지.

</details>
