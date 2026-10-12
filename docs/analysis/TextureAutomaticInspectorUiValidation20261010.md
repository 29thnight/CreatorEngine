# PHASE 12 — 자동 처리 Inspector 실제 입력 검증

기준일: 2026-10-10 KST. [TexturePipelinePlan](../plans/TexturePipelinePlan.md) §16 후속.
작은 격리 프로젝트 `Build/TAUI-Debug-534117b3/Project`에서 현재 Debug Editor를 실행했다.
runtime DLL SHA-256: `4F776088C80F6B3275536526AB374736FA95FC64EC441EC5017334406A8DB22F`.
엔진 코드는 수정하지 않았다. 기존 동기화 후 로컬 변경을 보존했다.

## 실제 입력 결과

시작 시나리오는 저장된 ImageComponent/SpriteRenderer fixture를 로드하고,
미저장 UnsavedAutoTexture 엔티티를 만든 뒤 Content Browser를 표시했다.
수용 대상 필드·선택·메뉴·Retry는 Windows Computer Use로 직접 조작했다.

1. fixture.png를 클릭해 새 자동 처리 Inspector 표시
2. Maximum Dimension에 Ctrl+A와 숫자 키 2를 입력; .meta의 2 저장 확인
3. Save/Reimport 버튼이나 씬 재로드 없이 씬 표시 변화, 미저장 엔티티 유지 확인
4. other.png를 실제 선택해도 fixture의 설정 2와 other의 설정 0을 구분해 표시
5. Inspector를 닫은 후 외부 .meta를 0으로 변경하자 watcher가 처리하고 씬의 원본 무늬 복원
6. 외부 입력으로 PNG를 손상시킨 뒤 Inspector 재표시; `Import failed; previous generation retained`
   및 signature mismatch 진단 표시. 씬의 기존 무늬와 미저장 엔티티 유지
7. 손상 소스에서 Retry Import를 실제 클릭해도 실패 진단 및 기존 씬 유지
8. PNG 원본 복구 후 별도 버튼/재로드 없이 Cooked generation ready 복귀
9. Normal Map=false 상태에서 BC5 선택 시 계약 오류 표시; 디스크는 compression=None 유지
10. 해당 무효 설정에서 Retry Import 클릭 후에도 metadata hash 불변

초기 키 입력 직후 캡처에는 이전 프레임 값이 보였으나, 후속 확인에서 입력값을 관찰했다.
재확인한 숫자 키 2는 화면과 디스크에 반영됐다. 붙여넣기 입력은 검사하지 않았다.

## 파일/화면 근거

receipt: `Build/TAUI-Debug-534117b3/result.json`.
`keyboard-settings.meta`와 `invalid-not-saved.meta`에 각각 저장된 2와 유효한 None/0을 보존했다.
최종 PNG는 Tiny4.png와 hash 일치, GUID 보존, invalid 설정의 디스크 hash 불변을 확인했다.
미저장 엔티티는 실행 중 화면에 유지됐으며 저장 씬 파일에는 기록되지 않았다.
즉 자동 리소스 갱신이 씬 전체를 자동 저장한 결과로 해석하지 않는다.

화면 근거는 같은 폴더의 `auto-keyboard-switch.png`, `hidden-watcher-recovery.png`,
`cook-failure-retained.png`, `automatic-recovery.png`, `invalid-bc5-blocked.png`다.
시작 시나리오와 stdout/stderr를 보존한다. 오류 표시 2건은 손상 입력/Retry의 의도한 사례다.

## 수용 경계

Debug 실제 UI 한 프로젝트의 기능 검증이다. 화면을 관찰했으며 GPU 픽셀 독립 oracle이나
현재 component owner/sceneId의 별도 조회는 수행하지 않았다. 이 부분은 앞선 API/프레임
자동 갱신 검증과 구분한다. 선택 변경은 편집 후 관찰했으나 400ms 대기 중 전환을
보장하지 않는다. 패널 닫힘 수용은 닫은 상태의 외부 .meta watcher 변경이다.

Release UI, 붙여넣기, 디스크 충돌 경합, 대기 저장 중 패널 닫기/종료,
여러 창/배율 및 모든 소비자/포맷/GPU/Shipping/품질/장시간 수용은 남긴다.
무효 설정에서 Retry는 시각적으로 활성 상태지만 valid guard로 실행을 막는다.
버튼 disabled 스타일의 UX 판단과 처리 거부의 기능 수용을 구분한다.
T0/T1a/T2 진행 중, earnedDays 0, T1b 대기, T3 중단은 유지한다.

종료 버튼 조작 후 stdout의 Finalize 완료 및 DX12 잔여 작업 0/0/0을 확인했다.
계획/대시보드 변경의 공백 검사는 통과했다. 최초 구조 검사에서 보고한 문자열 140건·항목 14건은 PHASE 4.85의 정상 따옴표 키를 인식하지 못한 검사기의 오탐이었다. 검사기를 수정한 뒤 전체 JavaScript 파싱·렌더/진행률·457개 항목 구조·대상 phase-meta 8개 문장 산수 검사가 모두 통과했다. 따옴표 키 수용과 값 내부의 잘못된 따옴표·ID 누락·알 수 없는 상태 거부도 별도 변형 입력으로 확인했다.
