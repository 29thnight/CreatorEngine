# Phase 19 R0 철거 기록

2026-10-01. P0 baseline HEAD: 12f970c7ed3a4408d268479f5d5acb5f80372b8c.

구 구현 55개 파일을 삭제 전에 P0 ZIP의 SHA256과 대조했다. 삭제 목록과 해시는
Tools/regression/physics-legacy-files.json에 보존한다. ZIP은
Build/Obj/Phase19P0/pre-r0-12f970c7.zip이며 이 ZIP을 철거 후 새 snapshot으로 덮어쓰지 않는다.

## 제거한 실행 소유자

- Engine/Physics의 구 PhysicX, 바디/리소스 상속 래퍼, CCT/이동, 이벤트/디버그 구현.
- PhysicsManager, 구 8종 물리 컴포넌트 및 CharacterControllerSystem 구현.
- Physics/SceneRuntime 프로젝트 및 filters의 구 파일 편입.
- Physics 프로젝트의 CUDA imports/compile/link 설정과 SceneRuntime/Editor/Mono 역방향 include.
- 구 컴포넌트 reflection/lifecycle/type UUID 등록 및 Editor 물리 저작 그룹.
- bootstrap singleton 생성/파괴와 Editor/Player 물리 초기화, PVD 연결.

PhysX SDK 의존과 Physics 프로젝트의 identity는 P1 새 구현에 사용하기 위해 유지한다.
구 UUID는 활성 등록에서 제거하고 P0 fixture의 legacy-type-uuids.json에 마이그레이션 자료로 보존한다.
구 제품 바이너리/Physics.lib는 디스크에 있어도 철거 후 소스 검증이나 실행 증거로 사용하지 않는다.
R0 당시 빈 Physics 프로젝트는 명시적으로 빌드를 거부했다. P1 새 PhysicsScene 편입 후 이 임시 guard는 제거했다.

## 검증 및 미완성 경계

audit-physics-cutover.ps1 -Gate Owners 통과: owner files/project references=0.
Complete gate는 남은 소비자 때문에 실패하며 이를 정상 제품 통과로 표시하지 않는다.
실제 Debug 빌드는 Scene의 Collision/PhysicsManager/ColliderContainerType 미이전으로 reflgen에서 실패했다.
RequirePhysicsImplementation 직접 실행도 비어 있는 새 모듈을 의도대로 거부했다.
제품을 빌드 가능하게 만들기 위한 빈 물리 구현이나 구 타입 호환 어댑터는 추가하지 않았다.

현재 lexical 소비자 12개 파일의 정확한 위치는 Build/Obj/Phase19R0/cutover-audit.json에 있다.
Scene.cpp/h의 컬렉션·fixed-step·이벤트는 B/C/T 연결, ClrHost/Native/managed API는 M0,
model/terrain 생성과 editor authoring/inspector는 B1/M0에서 새 계약으로 이전한다.
Articulation/Link 등 구 ragdoll 데이터 참조도 새 지원 범위와 스키마 이전을 확정해야 한다.
이 감사는 AST/링크 도달성 검증이 아니며 최종 M4에서 제품 빌드·실행과 함께 다시 검증한다.

**상태: R0 소유자 철거 완료, R0 전체 완료는 미확보.**
P1~P3 API를 먼저 작성하고 B/C/T/M 소비자 교체 후 Complete gate를 닫는다.
R0 표의 '새 계약으로 전량 교체'는 후속 단계가 필요한 최종 cutover 조건이다.
P1 새 PhysicsScene CPU 기반과 단독 모듈 Debug/Release 빌드를 검증했다. 제품 소비자 이전은 아직 남아 있다.
