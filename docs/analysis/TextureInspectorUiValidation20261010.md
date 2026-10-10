# PHASE 12 — Inspector 실제 입력 검증

기준일: 2026-10-10 KST. 동기화 후 HEAD b2f918c19 및 로컬 후속 수정 상태.
정본: [TexturePipelinePlan](../plans/TexturePipelinePlan.md).

## 실행 범위

현재 Debug Editor를 별도 프로젝트 `Build/TUI-Debug-1479b598/Project`로 실행했다.
기존 SpriteRenderer/ImageComponent 저장 fixture와 원본 4×4 PNG를 복사하고
저장 참조를 격리 프로젝트 경로로 교체했다. 시작 시나리오는 저장 씬 로드,
미저장 `UnsavedTextureGate` 생성 및 Inspector 표시만 준비했다.
수용 대상 버튼·경고·취소·파일 선택은 Windows Computer Use의 실제 마우스 입력으로
조작했고 매 행동 뒤 화면을 다시 확인했다. 내부 명령으로 클릭을 대체하지 않았다.

runtime DLL SHA-256: `2BBDE3F4CA8459E030B67251408F6FB3A4EF0DB909A310D0A7C20AF3A040C73F`.

## 관찰 결과

1. 실제 Content Browser 텍스처 클릭으로 Import Settings 표시
2. Maximum Dimension의 + 버튼을 두 번 눌러 0→2 변경; Save 전 .meta는 0 유지
3. Save and Reimport 클릭 뒤 .meta maxDimension=2, GUID 보존 및 Cooked generation ready 표시
4. Reload Saved Scene 클릭으로 새 cooked 참조 연결 안내와 미저장 편집 폐기 경고 표시
5. 경고창 Cancel 클릭 뒤 현재 씬의 TextureGate/UnsavedTextureGate 둘 다 유지
6. Choose Scene and Reload로 native 파일 선택창 열기; 파일 선택 취소 뒤 두 엔티티 유지
7. 다시 선택창에서 SavedFixture.creator 선택·Open 클릭 후 저장 씬 표시,
   TextureGate 유지 및 UnsavedTextureGate 제거, Inspector 설정 2 유지

실제 입력과 화면 관찰 기준 위 흐름은 통과했다. PNG 원본 hash는 Tiny4.png와 동일하고
GUID도 유지됐다. 마지막 재로드 화면은 `Build/TUI-Debug-1479b598/reloaded-inspector.png`,
receipt는 같은 폴더 `result.json`, 시작 시나리오와 stdout/stderr도 보존했다.
GUI 선택창의 기본 폴더가 격리 프로젝트 Assets/Scenes임을 확인했다.
검증용 Editor 종료와 Finalize 완료를 확인했다. 이번 검사에서 엔진 코드는 변경하지 않았다.
대시보드 전체 JavaScript 파싱·렌더·유한 진행률 및 문서 whitespace 검사는 통과했다.
기존 PHASE 4.85 구조 문제는 문자열 140건/항목 모양 14건 그대로 남아 전체 구조
검사는 실패한다(`Build/phase12-inspector-ui-dashboard.log`).

## 판정 한계

Debug 실제 UI 한 번의 작은 fixture 검증이다. Release의 실제 UI 반복, 여러 창/배율,
invalid setting·cook 실패 시 UI 상태/disabled gating, 대량 자산 및 입력 경합은 미검증이다.
문자 붙여넣기 자동화는 입력값을 바꾸지 못해 + 버튼으로 값을 변경했다.
숫자 키/붙여넣기의 제품 결함으로 확정하지 않으며 키보드 입력 검증은 남긴다.
sceneId/owner identity는 이번 UI 실행에서 독립 조회하지 않았다. 앞선 API/프레임
종단 검증과 구분한다. GPU 화면을 관찰했으나 기대 픽셀 oracle이나 압축 품질 검사도 아니다.

T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.
다음 수용은 UI의 남은 입력/실패 경계와 GPU·모델·Shipping 소비 검증이다.
