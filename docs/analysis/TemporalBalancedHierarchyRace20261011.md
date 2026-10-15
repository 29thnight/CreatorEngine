# Temporal fixture의 씬 구조 동기화

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 계약

EditorMain GameLogic은 TemporalMotionFixture::RequiresSceneWriteBoundary가 true이면 m_sceneStructureMutex를 잡는다. hierarchy UI 순회와 fixture entity 생성/삭제가 같은 구조 경계를 공유해야 한다. Balanced SDK 품질과 vector iterator invalidation은 다른 실패 축이다.

## 재현·수용 조건

fixture 준비·실행·정리와 UI hierarchy 표시를 겹쳐 검사하고 정상 종료까지 수집한다. 과거 dump가 일치 PDB 없이 주소로만 분석되었다면 확정 unwind stack으로 표시하지 않는다. 간헐 실패 후 성공 반복은 최초 실패 증거를 지우지 않는다.

## 기록의 범위

[시간축 재구성 정본](../plans/TemporalReconstructionPlan.md)과 함께 읽는다. 이 파일명은 이전 검사 주제로 연결되는 경로를 유지한다. 과거 특정 GPU·해상도·바이너리의 성공 표본, 삭제된 원본 픽셀과 보존 manifest를 현재 전체 제품 수용으로 확대하지 않는다. 이번 개정에서는 새 실행·측정 결과를 만들지 않았다.

## 소스 근거

- [Editor/EngineEntry/EditorMain.cpp](../../Editor/EngineEntry/EditorMain.cpp)
- [Editor/EngineGUIWindow/HierarchyWindow.cpp](../../Editor/EngineGUIWindow/HierarchyWindow.cpp)
