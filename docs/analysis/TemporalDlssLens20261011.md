# DLSS perspective lens 변경과 history reset

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/temporal-validation/Verify-TemporalLens.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Verify-TemporalLens.ps1#L2) — `[Parameter(Mandatory)][string]$EndpointFile,`
- [Tools/temporal-validation/Verify-TemporalLens.ps1:3](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Verify-TemporalLens.ps1#L3) — `[Parameter(Mandatory)][string]$OutputDirectory,`
- [Tools/temporal-validation/Verify-TemporalLens.ps1:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Verify-TemporalLens.ps1#L13) — `$out = [IO.Path]::GetFullPath($OutputDirectory)`
- [Tools/temporal-validation/Compare-TemporalLens.py:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Compare-TemporalLens.py#L7) — `spec=importlib.util.spec_from_file_location('recovery',Path(__file__).with_name('Compare-LiveSSGIRecovery.py'))`
- [Tools/temporal-validation/Compare-TemporalLens.py:64](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Compare-TemporalLens.py#L64) — `p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True)`
- [Tools/temporal-validation/Compare-TemporalLens.py:69](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/temporal-validation/Compare-TemporalLens.py#L69) — `raise SystemExit(0 if result['passed'] else 1)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-10-11. 실행 정본: `Artifacts/PR169/dlss-lens-20261011/checkpoint.json`.

## 구현

라이브 개발용 raw fixture `camera.lens.probe <target> [vertical-fov near far]`를 추가했다. 같은 CameraComponent가 소유한 perspective 렌즈를 바꾸지만 camera cut 또는 명시적 temporal reset을 요청하지 않는다. 따라서 제출된 투영의 변경 감지가 실제 reset을 만드는지를 검사한다. 읽기 응답과 변경 응답에 이전/현재 FOV·near·far 및 cut revision을 남긴다. 이 명령은 Undo/일반 저작 동등성 검사가 아닌 진단용이다. 직교 카메라와 비유한 값, 범위 밖 FOV, 잘못된 클리핑 평면은 거부한다.

새 `Verify-TemporalLens.ps1`/`Compare-TemporalLens.py` 및 기존 launcher의 `-Scenario lens`를 연결했다. 기존 전환 시나리오가 기본값이다. 첫 초기 실행에서 명령을 commandlet 테이블로 잘못 분류해 unknown을 반환했고, 라이브 테이블 등록으로 수정했다. 해당 실행은 GPU 캡처 없이 정상 종료했으며 로그를 보존했다. 렌더러의 reset 알고리즘이나 DLSS preset은 이번에 변경하지 않았다.

## 실행 결과

SDK 활성 Debug/Release 재빌드 후 Native AA/Quality/Balanced/Performance/Ultra Performance 10실행, **180캡처·90비교**를 통과했다. 각 실행은 FOV 60°/near .1/far 500을 기준으로 FOV 45°·90°, near .25, far 100을 개별 변경한다. 첫 번째와 여덟 번째 승인 reset 프레임을 동일한 최종 렌즈의 camera-cut reference와 비교하고, 마지막 baseline 복원도 비교한다.

- depth/baseColor/SSGI/HDR/display 최대 차이 전부 0. 비교 대상과 reference의 D/R 픽셀 해시 동일.
- 카메라 위치 불변, 요청 렌즈의 투영 계수/표시 aspect 일치, cut revision 불변, 별도 설정/reset 세대와 해상도 불변 확인.
- 모든 렌즈 변경에서 baseline 대비 depth 변화 확인. 명령 성공만으로 렌더 반영을 인정하지 않았다.
- 동일 렌즈 재설정 **80건**에서 16프레임 넘게 진행한 뒤 history 유지·DLSS 활성·폴백 없음 확인.
- 잘못된 입력 **60건**을 카메라 변경 없이 거부. 실제 대표 캡처 기반 receipt/신원/투영 변이 **20종** 거부.
- 각 구성/품질 실행의 history 계약 506단정 통과. GPU validation 메시지/문제/누락 0, 정상 종료 코드 0.

Quality Debug pilot 이후 검증기에 no-op 품질과 입력 거부 요약을 강화했다. pilot의 보존된 no-op/거부 metadata도 최종 집계에서 같은 조건으로 다시 확인했다. 원본 픽셀을 복제하거나 손상시키지 않았다.

## 저장과 범위

구성/품질별 성공은 첫 FOV reset 대표만 보존했다. 중복 raw 삭제 ledger 기준 실제 6.109GiB 회수, 여유 81.78GiB다. 로그·판정·manifest·해시는 유지하지만 제거한 전체 raw 시퀀스는 다시 재생할 수 없다.

범위는 DX12 RTX 2080 Ti, Editor Game 뷰, 정적 cube/floor, 고정 SDK 시간, 위 네 perspective 렌즈 변화다. 연속 zoom, 직교 전환, 임의 비대칭 투영, 다른 해상도/GPU, 멀티카메라와 뷰 재생성, 가변 시간, 실제 자산 화질·성능, FG 및 외부 지연은 별도 수용이 필요하다. TR2/TR3/TU3 progress, PHASE 4.5 기성 12/71 유지. 다음 우선 항목은 멀티카메라 전환/뷰 수명과 history 격리다.

</details>
