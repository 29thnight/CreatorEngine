# Phase 19 R0 철거 기록

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 물리 쿼리는 sceneAnchor Entity, 호출자 버퍼와 PhysicsQueryResult를 사용한다. 현재 hit 타입은 PhysicsHit이며 LayerId는 신원 값이다.
- 접촉은 OnBeginSimulation에서 ContactStream을 등록하고 PostPhysics에서 소비한다. OnTrigger*/OnCollision* 훅과 RaycastAll을 현행 공개 API로 사용하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/SceneRuntime/Scene.cpp:84](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L84) — `m_begin = Clock::now();`
- [Engine/SceneRuntime/Scene.cpp:94](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L94) — `const TimePoint end = Clock::now();`
- [Engine/SceneRuntime/Scene.cpp:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.cpp#L102) — `m_begin = Clock::now();`
- [Tools/regression/physics-legacy-files.json:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/physics-legacy-files.json#L1) — `[`
- [Tools/regression/audit-physics-cutover.ps1:3](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-physics-cutover.ps1#L3) — `$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/audit-physics-cutover.ps1:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-physics-cutover.ps1#L13) — `if([IO.Path]::GetFileName($node.Include) -in @($legacy | ForEach-Object {[IO.Path]::GetFileName($_.path)})) {`
- [Tools/regression/audit-physics-cutover.ps1:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/audit-physics-cutover.ps1#L30) — `if($hits.Count){$consumers+=@{path=[IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/');hits=@($hits|ForEach-Object {@{line=$_.LineNumber;text=$_.Line.Trim()}})}}`
- [ScriptCore/Physics.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L13) — `public struct PhysicsHit`
- [ScriptCore/Physics.cs:41](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L41) — `public readonly struct PhysicsQueryRequest`
- [ScriptCore/Physics.cs:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Physics.cs#L65) — `public readonly struct PhysicsBatchResult`
- [ScriptCore/Collision.cs:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Collision.cs#L1) — `// ContactStream.cs defines the replacement contact value and stream API.`
- [Engine/Physics/PhysicsScene.h:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Physics/PhysicsScene.h#L12) — `enum class resource_kind : std::uint8_t`
- [Engine/Physics/PhysicsScene.h:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Physics/PhysicsScene.h#L29) — `enum class execution_preference : std::uint8_t`
- [Engine/Physics/PhysicsScene.h:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Physics/PhysicsScene.h#L35) — `enum class execution_backend : std::uint8_t`
- [Engine/SceneRuntime/ClrHost.h:96](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ClrHost.h#L96) — `enum class AniEventKind : int { Enter = 0, Update = 1, Exit = 2 };`
- [Engine/SceneRuntime/ClrHost.h:266](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ClrHost.h#L266) — `enum class BTNodeKind : int { None = 0, Action = 1, Condition = 2, ConditionDecorator = 3 };`
- [Engine/SceneRuntime/ClrHost.h:341](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ClrHost.h#L341) — `enum class ScriptFieldType : int`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

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

## 2026-10-03 현재 판정

Fresh Complete gate 통과: 구 파일55개 제거, 남은 파일/프로젝트 참조/소비자0.
앞의 소비자12개 및 제품 미빌드 기록은 철거 당시 이력이다.
새 API 제품 빌드·실행은 PhysicsRedesignPlan의 M2 수용 근거와 연결한다.
R0는 완료로 갱신하며 AST/최종 링크/재유입 감사는 M4에서 수행한다.

</details>
