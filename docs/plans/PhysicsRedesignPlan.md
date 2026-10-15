# 물리 통합 현황과 후속 검증 지도

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 도달한 구조

물리 경계는 `Engine/Physics`의 SDK 비노출 API, SceneRuntime의 세션·컴포넌트, ScriptCore의 값 ABI로 나뉜다. 옛 단계별 완료 일지는 현재 구현 명세가 아니므로 기능별 계약을 아래 세 문서로 통합한다.

- [API 계약](../design/PhysicsAPIContract.md): 핸들, expected 오류, 작업자 채널, 고정 스텝, query batch, GPU fallback
- [컴포넌트 설계](../design/PhysicsComponentDesign.md): 완전한 body/shape 저작, layer, runtime replacement, character
- [접촉 실행 계약](../design/PhysicsContactExecutionContract.md): 역할 구독, 값 publication, PostPhysics 수명, overflow

현재 코드에는 60Hz·최대 4 catch-up의 Scene 물리 세션, shared cooked collision geometry, 레이어 refilter, CCT 이동 정책, native/managed batch query, ContactStream이 연결돼 있다. 이 목록은 코드 연결을 뜻하며 기존 보고서의 처리량·성능 예산·실장치 PASS를 다시 주장하지 않는다.

## 호스트 세션이 완료를 결정한다

Editor는 `SimulationSessionPolicy::editor_restore`를 선택해 시작 전 문서를 snapshot하고 정지 뒤 복원한다. Player 기본 runtime 정책은 저작 복원을 하지 않는다. 시작 실패는 Play committed가 되면 안 되고, 종료 실패 시 복원 문서를 잃어서는 안 된다. 요청 플래그와 실제 commit을 혼동하지 않는다.

두 호스트는 공통 RuntimeFrame에서 물리를 진행하지만 씬 구조 변경과 표시 단계는 각 Host가 소유한다. 물리 실행 오류는 그 안전 경계에서 정리된다. 고정 물리 accumulator가 있으므로 “누산기가 전혀 없다”는 옛 네트워크 전제는 유효하지 않다. 반면 고정 관리 `OnSimulationTick`은 별도의 미구현 요구다.

## 기능별 수용 기준

- 정의·직렬화: duplicate shape ID, invalid scale, unknown layer, 잘못된 role/geometry UUID를 거부하고 reload 후 동일 정의를 얻는다
- 자산: geometry source부터 cook artifact, mount, exact revision, 실패·교체 수명을 확인한다. primitive preview만으로 cooked Player를 수용하지 않는다
- 세션: Play 실패, stop 복원, reload, disabled body, component 제거, DDOL 계층 이송과 재등록을 확인한다
- 질의: no-hit, zero capacity discovery, overflow, disjoint batch buffers, mixed request 오류와 wrong scene을 확인한다
- 접촉: solid와 sensor, retirement, late subscription, same-owner regeneration, role 전환, overflow를 확인한다
- 실행: SDK task submit/run/release, worker 수, query scratch, snapshot pin, terminal failure와 shutdown drain을 각각 관찰한다

대표 회귀 도구는 `verify-physics-p1.ps1`/`p2`/`p3`, `verify-physics-t2.ps1`, `verify-physics-contact-editor-suite.ps1`, `verify-physics-contact-player-suite.ps1`, `verify-physics-character-player.ps1`, `verify-physics-player-performance.ps1`다. 이들은 실행 조건과 제품 바이너리를 필요로 한다. 파일 존재만으로 시험 완료를 판단하지 않는다.

## 아직 별도 입증이 필요한 것

성능 수치는 scenario, body/shape 수, active/changed 비율, worker 수, SDK build, instrumentation on/off, warmup과 캡처 손실 여부를 함께 기록해야 비교할 수 있다. ETW/CPU capture에서 지연 시간과 실제 CPU 실행 시간을 분리한다. 서로 다른 fixture의 p99를 하나의 물리 예산으로 합치지 않는다.

GPU 요청은 실제 backend 확인을 필요로 한다. 네트워크 복제·rollback·정해진 관리 simulation tick, effect recording은 이 물리 통합에 포함되지 않는다. 다음 작업은 동작 변경을 추측해 추가하기보다 현재 제품 조합에서 회귀 행렬을 재실행하고 실패 위치를 정확히 좁히는 것이다.

## 이전 데이터와 증거

[cutover 감사](../analysis/PhysicsCutoverAudit.md)는 제거된 실행 경로의 재도입을 막는 용도로 유지한다. `Tools/regression/PhysicsSchemaMigration.md`와 migration corpus는 저작 파일 전환의 별도 근거다. 측정 JSON·캡처·이미지는 원래 provenance와 함께 보관하며 이 문서 개정에서 변경하지 않는다.

## 구현 근거

- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/SimulationSessionPolicy.h](../../Engine/SceneRuntime/SimulationSessionPolicy.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/ScenePhysicsSimulation.h](../../Engine/SceneRuntime/ScenePhysicsSimulation.h)
- [Tools/regression/verify-physics-t2.ps1](../../Tools/regression/verify-physics-t2.ps1)
- [Tools/regression/verify-physics-contact-editor-suite.ps1](../../Tools/regression/verify-physics-contact-editor-suite.ps1)
- [Tools/regression/verify-physics-player-performance.ps1](../../Tools/regression/verify-physics-player-performance.ps1)
