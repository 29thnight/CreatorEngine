# Balanced 검증과 Hierarchy 반복자 크래시

## 발견

자동 DLSS Balanced Debug 검증 PID 728에서 `can't decrement invalidated vector iterator` 단정이 발생했다. 사용자는 에디터를 조작하지 않았다고 확인했다. 첫 캡처 전 종료 코드 `0x80000003`으로 종료됐으며 최초 실패는 성공으로 대체하지 않는다.

Windows가 보존한 `CreatorEditor.exe.728.dmp`(124,736,601 bytes)를 발견해 같은 파일의 하드링크와 SHA256을 검증 폴더에 보존했다. 덤프 발견 전에 진단 빌드를 했으므로 당시 일치 PDB는 이미 덮어써졌다. 현재 incremental PDB를 강제 로드한 **스택 메모리 주소 탐색**에서 `vector<int>::iterator::operator--`, reverse iterator, `HierarchyFlatView::Rebuild`, `Rows`, `HierarchyWindow::Draw` 주소가 이어졌다. 정상 unwind로 완전히 복원한 호출 스택으로 주장하지 않는다.

독립적으로 소스에서 누락된 동기화 경계를 확인했다. Presentation의 `EditorMain::OnGui`는 `m_sceneStructureMutex` 아래에서 계층의 children vector를 순회한다. 하지만 `Scene::LateUpdate`에서 실행되는 `TemporalMotionFixture::TickAfterAnimation`의 준비/정리(`Isolate`/`Cleanup`)는 엔티티를 생성·삭제하면서 이 잠금을 잡지 않았다. EndOfFrame에 있던 잠금은 앞서 실행된 fixture 구조 변경을 보호하지 못한다. fixture asset 준비 완료 시점에 따라 재현되는 경쟁 조건이다.

## 수정

fixture가 Preparing/Running 상태인 동안 Editor GameLogic을 동일한 씬 구조 mutex로 보호한다. 정리가 보류된 상태도 포함한다. 일반 프레임은 추가 잠금 없이 기존 경로를 유지한다. fixture pose 갱신은 기존 LateUpdate/proxy seal 순서를 유지하며 UI의 vector를 임의로 복사하거나 반복자 검사를 끄지 않는다.

추가로 Debug SDK 검증 프로세스에 `CREATOR_ASSERT_STACK`을 설정한다. 선택적 CRT report hook이 단정 메시지와 호출 스택을 stderr에 기록하고 기존 실패 처리를 계속한다. 실행기 부모의 환경 변수는 자식 시작 직후 복구한다. 이것은 진단 개선이며 크래시 수정은 씬 구조 경계에 있다.

## 검증과 범위

수정 전 진단 빌드에서 Debug 5회 및 Release 1회가 정상 실행돼 간헐성을 확인했다. Balanced 기본 preset은 변경하지 않았다. 첫 이동 HDR 평균/p95는 0.00334363/0.01827274, display 평균/p95는 0.00690573/0.02898551로 기존 기준 0.10/0.25를 통과했다. 초기 D/R 및 Debug 반복의 17프레임 × 12 attachment 해시는 동일하다.

최종 수정 제품의 별도 결과는 `Artifacts/PR169/dlss-balanced-20261011/fixed-checkpoint.json`에 기록한다. 범위는 DX12 RTX 2080 Ti, display 760×312/render 441×181, warmed 고정 SDK 60 Hz, 불투명 이동/사라짐/회복 fixture다. Performance/Ultra Performance, 다른 해상도/실제 장면, startup 및 전체 TU3/PHASE 4.5 수용은 남아 있다.

성공 원본은 비교가 끝나면 구성별 첫 이동 대표만 남긴다. 판정/로그/해시는 보존하고 삭제 ledger에 과거 전체 픽셀 재생 불가를 기록한다. 최초 실패와 필요한 크래시 덤프는 유지한다.

## 최종 결과

수정 후 Debug/Release 빌드 및 34캡처 통과. 각 구성 history 506단정 통과, GPU validation 메시지/문제/누락 0, 정상 종료 0. 17프레임 × 12 attachment 해시가 수정 전후와 D/R 간 동일했다. 최초 간헐 크래시를 강제로 반복 재현한 결정론적 테스트는 없으며, 확인된 동기화 누락을 수정하고 제품 실행으로 검증한 범위다.

최종 성공 대표 frame-1 두 개(원본 34개)와 최초 실패 기록/덤프를 보존했다. 마지막 정리에서 실제 1.55 GiB 회수, 여유 85.29 GiB를 확인했다. 중간 성공 및 최종 전체 시퀀스는 판정/manifest/해시만 남아 전체 픽셀 재생은 불가하다.
