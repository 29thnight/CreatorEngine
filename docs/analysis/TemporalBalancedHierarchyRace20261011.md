# Balanced 검증과 Hierarchy 반복자 크래시

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/EngineEntry/EditorMain.h:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.h#L2) — `class ProjectLayerSettings;`
- [Editor/EngineEntry/EditorMain.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.h#L5) — `class AudioHost;`
- [Editor/EngineEntry/EditorMain.h:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.h#L6) — `class PlaybackService;`
- [Editor/EngineGUIWindow/HierarchyWindow.h:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L6) — `class Entity;`
- [Editor/EngineGUIWindow/HierarchyWindow.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L7) — `class HierarchyWindow`
- [Editor/EngineGUIWindow/HierarchyWindow.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L9) — `public:`
- [Engine/SceneRuntime/TemporalMotionFixture.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.h#L5) — `enum class TemporalMotionFixtureState : std::uint8_t`
- [Engine/SceneRuntime/TemporalMotionFixture.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.h#L10) — `struct TemporalMotionFixtureStatus`
- [Engine/SceneRuntime/TemporalMotionFixture.h:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.h#L22) — `class TemporalMotionFixture`
- [Editor/EngineEntry/EditorMain.cpp:74](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.cpp#L74) — `auto* window = WindowsCoreWindow::GetForCurrentInstance();`
- [Editor/EngineEntry/EditorMain.cpp:81](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.cpp#L81) — `Editor::EditorMain::EditorMain()`
- [Editor/EngineEntry/EditorMain.cpp:991](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorMain.cpp#L991) — `void Editor::EditorMain::OnGui()`
- [Editor/EngineGUIWindow/HierarchyWindow.cpp:240](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.cpp#L240) — `void HierarchyWindow::DrawSceneObjectRow(Entity* obj, const editor::hierarchy_flat_row& row)`
- [Editor/EngineGUIWindow/HierarchyWindow.cpp:370](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.cpp#L370) — `void HierarchyWindow::Draw()`
- [Editor/EngineGUIWindow/HierarchyWindow.cpp:680](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.cpp#L680) — `const auto& flatRows = m_flat.Rows(scene, flatKey, m_searchFilter);`
- [Engine/SceneRuntime/TemporalMotionFixture.cpp:280](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.cpp#L280) — `session.status.error += " Cleanup is pending: " + std::string(exception.what());`
- [Engine/SceneRuntime/TemporalMotionFixture.cpp:904](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.cpp#L904) — `void TemporalMotionFixture::TickAfterAnimation()`
- [Engine/SceneRuntime/TemporalMotionFixture.cpp:1188](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/TemporalMotionFixture.cpp#L1188) — `void TemporalMotionFixture::TickAfterAnimation()`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

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

</details>
