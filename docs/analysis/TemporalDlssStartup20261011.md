# DLSS 비 Quality 모드 startup 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-10-11. Native AA → Balanced → Performance → Ultra Performance의 Debug/Release startup 검증을 완료했다. 판정 근거는 `Artifacts/PR169/dlss-startup-20261011`에 저장한다. 전체 PHASE 4.5 종료 판정과 구분한다.

## Balanced 수정

Balanced 기본 모델은 startup 첫 두 이동 프레임에서 실패했다. 첫 이동 HDR 평균/p95는 0.68467990/0.72480769, display 평균/p95는 0.75833025/0.79710146이다. GPU validation 0, 정상 종료 0이므로 프로세스 충돌이 아닌 화질 실패다.

최적 해상도 질의와 실제 dispatch 양쪽의 `balancedPreset`을 J로 고정했다. 기존 Native AA/Quality 정책과 같은 방식이며 Performance/Ultra Performance 기본 모델은 유지한다. 원래 실패와 수정 Debug 실행의 17프레임 × SDK 입력 11종 raw SHA256, 지터/이전 지터/SDK 시간/reset/품질 값이 모두 동일했다. 따라서 이 fixture에서 모델 선택에 따른 출력 차이를 확인한 것이며 SDK 내부 원인을 특정한 것은 아니다.

수정 후 첫 이동 HDR 평균/p95는 0.00019333/0.00038462, display 평균/p95는 0.00222965/0.01449275다. 허용치 평균 0.10/p95 0.25는 변경하지 않았다. 실패 전체 raw와 입력 비교 기록은 보존했다. 프리셋 변경의 실제 장면 깜빡임/성능 수용은 별도 검사 대상이다.

## 검증 범위

DX12 RTX 2080 Ti, display 760×312, 고정 SDK 60 Hz의 불투명 fixture다. startup은 정확히 한 번 수용된 제출 이후 첫 관측 non-reset 상태부터 기준+이동/사라짐/회복 17캡처를 검사한다. reset 프레임 자체의 캡처나 실제 게임 전체 품질 수용을 뜻하지 않는다.

## 최종 결과

최종 SDK 활성 Debug/Release 빌드의 네 모드 startup 8실행·136캡처가 통과했다. 모드별 17프레임 × 12 attachment의 D/R 해시가 동일하다. 모든 실행에서 실제 DLSS provider와 요청 품질, 정확한 warmup 1회, DX12 validation 메시지/문제/누락 0, 정상 종료 0을 검사했다.

| 모드 | 렌더 해상도 | 전체 HDR 최대 평균 / p95 | 전체 display 최대 평균 / p95 | D/R |
|---|---|---|---|---|
| native-aa | 760×312 | 0.00048720 / 0.00076923 | 0.01120039 / 0.01449275 | 통과 |
| balanced | 441×181 | 0.00057566 / 0.00076938 | 0.01106247 / 0.01449275 | 통과 |
| performance | 380×156 | 0.00260299 / 0.00538358 | 0.01101153 / 0.01449275 | 통과 |
| ultra-performance | 253×104 | 0.00403239 / 0.00692441 | 0.01136863 / 0.01449275 | 통과 |

수정된 Balanced의 warmed D/R도 추가 2실행·34캡처 통과, D/R 17×12 해시 동일, history 계약 각 506단정 통과, validation 0·exit 0이다. 기존 Hierarchy 단정은 이번 검사에서 재발하지 않았다. 빌드에는 기존 FidelityFX 중복 심볼 경고가 있으나 오류 없이 완료했다.

근거: `checkpoint.json`, `model-input-comparison.json`, `warmed-regression.json`, 각 실행의 launch/oracle/validation 및 빌드 로그. 첫 Native AA 실행은 Balanced 수정 전 이력이며 최종 136캡처 집계에는 최종 바이너리 재실행만 포함했다. 전체 실제 실행은 초기 Native AA 2, Balanced 실패 1, 최종 startup 8, warmed 2로 총 13회다.

## 저장 및 잔여

이번 작업의 삭제 ledger 기준 실제 회수량 합계 5.474 GiB, 최종 디스크 여유 84.42 GiB다. 성공 가족별 첫 이동 대표만 남기고 이전 Native AA 대표와 비교 완료 시퀀스는 정리했다. 필요한 Balanced 기본 모델 startup 실패 전체 raw 1개는 보존했다. 판정·로그·manifest·해시와 삭제 ledger는 유지하며 삭제된 전체 시퀀스의 raw 재생은 불가능하다.

이 해상도와 고정 시간 fixture에서 다섯 DLSS 품질의 startup/warmed D/R 근거가 누적됐다. Quality 검증은 이전 빌드의 근거이며 이번 최종 바이너리로 다섯 모드를 모두 재실행한 것은 아니다. 다른 해상도·실제 모델/재질·얇은 형상·가변 시간·reset/resize·깜빡임/성능 및 실제 FG 수용은 남는다. TU3 progress/기성 0, PHASE 4.5 12/71은 유지한다.


## 대시보드 정합성

정본 계획서와 HEAD에 존재하지만 작업 대시보드에서 누락된 TU4(XeSS, 진행/기성 0, 3일)를 복원했다. 실제 TASKS 평가 결과 16행/71일이며 meta 산수 불일치 0이다. JavaScript 전체 파싱과 렌더 검사는 통과했다. 기존 전체 검사 스크립트는 별도 PHASE 4.85의 따옴표 키로 작성된 14항목을 인식하지 못해 문자열 140건/모양 14건을 보고하므로 전체 도구 통과로 기록하지 않는다. 근거는 dashboard-check.log 및 phase-total-check.json이다.

</details>
