# 행동 트리 C# 우선 재설계 — 실행 설계

> **보관 · 엔진 범위 완료 (정리: 2026-09-06).** PHASE 9의 9-8·9-10 완료 기록. 기존 게임 BT 콘텐츠 43종 이식은 별도 잔여이며 엔진 완료로 대신하지 않는다.
> [보관 색인](README.md) · [활성 계획과 대시보드](../../RefactoringPlanDashboard.html#doc-index)

작성: 2026-08-07 · 결정: 사용자 선택(9-4 후속 ③) · 선행: CoreCLR 스크립팅(2-14~2-21), C++ 핫리로드 은퇴(9-4)

## 1. 왜 이 방향인가

9-4에서 C++ 스크립트 노드가 사라진 뒤, BT 스크립트 노드를 C#으로 되살리는 길은 셋이었다.

| 방향 | 크로싱 | 불변식 | 판정 |
|---|---|---|---|
| 네이티브 트리 + C# 노드 | 노드마다 동기 + 블랙보드 접근마다 중첩 | **위반** | 기각 |
| 빌트인 노드만 유지 | 없음 | 유지 | 기능 후퇴 |
| **트리 전체를 C#으로** | **틱당 1회** | **유지** | **채택** |

핵심 근거는 **BT의 틱이 본질적으로 동기**라는 데 있다. `m_root->Tick(dt, blackboard)`는 재귀로 내려가며
각 노드의 `NodeStatus` 반환값으로 다음 순회를 정한다 — 배치로 묶을 수가 없다. 노드 하나를 C#에 두면
크로싱이 노드 수만큼 생기고, 그 안에서 블랙보드를 읽으면 크로싱이 또 중첩된다.

반대로 **트리 전체가 관리 측에 있으면 크로싱은 "이 컴포넌트를 틱하라" 한 번**이다. 순회도 블랙보드
접근도 전부 관리 영역에서 끝난다 — `BehaviourRegistry._byObject`가 GetComponent를 관리 측에서
끝낸 것과 같은 해법이다(설계 문서 03.1 · 11.2).

## 2. 경계 계약

```
네이티브                        관리(C#)
BehaviorTreeComponent
  Awake   ──▶ ClrHost::CreateBehaviorTree(owner, graphJson) ──▶ 트리 구축 + 인스턴스 id
  InternalAIUpdate ──▶ (큐에 담기) ────────────────────────▶ FlushAITicks (틱당 1회)
  OnDestroy ─▶ ClrHost::DestroyBehaviorTree(id)
```

- **틱은 배치**: `ScriptAITick { int instanceId; float deltaTime; }` 배열로 모아 틱 경계에서 한 번에
  넘긴다. 물리·애니메이션·메시지 큐와 같은 규약이며, `Dx11Main::TickScripts`에 플러시 한 줄이 는다.
- **블랙보드는 관리 측 소유**: `BlackBoard`를 C#으로 옮긴다. 네이티브가 값을 읽어야 하는 곳이
  있으면 그때만 개별 접근자를 추가한다(현재로선 에디터 디버그 표시뿐).
- **저작 데이터는 그대로**: `BTBuildGraph`/`BTBuildNode`의 직렬화 형식(YAML)을 바꾸지 않는다.
  구축 시점에 그래프를 관리 측으로 한 번 넘기고, 트리 조립은 C#이 한다.

## 3. 옮길 것과 남길 것

| 요소 | 현재 위치 | 이후 | 비고 |
|---|---|---|---|
| `NodeStatus`, `BehaviorNodeType`, `ParallelPolicy` | BTEnum.h | 양쪽 미러 | 값이 어긋나면 조용히 오동작 — 검사 스크립트 대상 |
| 빌트인 노드(Sequence·Selector·WeightedSelector·Parallel·Inverter·ConditionDecorator) | BTHeader.h | **C#으로 이식** | 로직이 짧다(각 10~30줄) |
| `ActionNode`/`ConditionNode` 파생(사용자 노드) | Dynamic_CPP(은퇴) | **C#에서 신규 작성** | 생성기가 등록표를 만든다 |
| `BlackBoard`(9타입) | Blackboard.h | **C#으로 이식** | 값 소유가 관리 측으로 |
| `BTBuildGraph`/`BTBuildNode` | ScriptBinder | **네이티브 유지** | 에디터 저작·직렬화는 그대로 |
| `NodeFactory` | NodeFactory.h | **C#으로 대체** | 이름→노드 생성 |
| `AIManager`의 그래프 캐시 | AIManager | 네이티브 유지 | 그래프는 여전히 네이티브가 읽는다 |
| BT 노드 에디터 UI | MenuBarWindow | 네이티브 유지 | 노드 타입 목록만 ClrHost에서 받는다 |

## 4. 슬라이스

각 슬라이스는 독립 커밋이고, 끝날 때마다 전체 빌드 그린 + 회귀 통과를 확인한다.

1. **B1 관리 측 골격** — `BTNode`/`CompositeNode`/`DecoratorNode`/`ActionNode`/`ConditionNode` 기반
   클래스와 `NodeStatus` 열거를 C#에 세운다. 빌트인 6종 이식. 이 단계는 네이티브 무변경.
2. **B2 BlackBoard 이식** — 9타입 값 저장소를 C#에 만든다. GameObject/Transform 타입은 기존
   `ObjectHandle`을 재사용한다.
3. **B3 그래프 전달과 조립** — `BTBuildGraph`를 평평한 POD 배열로 직렬화해 한 번에 넘기고
   (`ScriptBTNodeDesc { guid, parentGuid, type, policy, nameUtf8, scriptNameUtf8 }`), C#이 트리를
   조립한다. 문자열이 섞이므로 이름은 고정 길이 배열로 — `ScriptMessage`에서 쓴 방식과 같다.
4. **B4 틱 배선** — `CreateBehaviorTree`/`DestroyBehaviorTree`/`QueueAITick`/`FlushAITicks`.
   `BehaviorTreeComponent`의 `BuildTree`·`m_root`·`m_built`를 걷어내고 인스턴스 id만 든다.
5. **B5 생성기 확장** — 사용자 BT 노드(`ActionNode`/`ConditionNode` 파생)를 이름으로 등록하는 표를
   `ScriptRegistry`에 추가한다(`RegisterAllBTNodes`). Behaviour·AniBehaviour와 같은 구조.
6. **B6 에디터 배선** — 노드 타입 목록을 `ClrHost::GetBTNodeTypeNames(kind)`로 받아 BT 편집기의
   스크립트 노드 선택을 되살린다. `AIManager`의 이름 목록 3종은 여기서 소멸.
7. **B7 정리** — `BTHeader.h`의 노드 구현, `NodeFactory`, `BlackBoard` 구현을 제거한다.
   `BTBuildGraph`/`BTBuildNode`와 에디터 경로만 남는다.

## 5. 위험

- **열거 값 미러링**: `NodeStatus`·`BehaviorNodeType`·`ParallelPolicy`·`BlackBoardType`이 양쪽에
  존재하게 된다. API 테이블이 순서 사고로 접근 위반을 낸 전례가 있으므로(`check-api-table.ps1`이
  그래서 생겼다), 같은 방식의 열거 대조 검사를 붙인다.
- **Running 상태의 수명**: BT는 `Running`을 프레임 간에 이어 간다. 트리 인스턴스가 관리 측에
  살아 있어야 하므로, 씬 언로드·재생 정지 왕복에서 `ScriptComponent`와 같은 되살리기 규약을
  따라야 한다(`PrepareForReload` 참조). 짝이던 `SuspendInstance`는 2026-09-05에 제거했다 —
  "재생이 씬 사본을 만드니 원본 인스턴스를 접어야 한다"는 전제로 서 있었는데 재생은 in-place라
  그 상황이 없었고, 호출자도 0건이었다.
- **에디터 미리보기**: 현재 BT 편집기가 노드 상태를 색으로 보여 준다면 그 정보는 관리 측에 있게
  된다. 필요하면 진단용 읽기 API를 하나 추가한다(틱당 1회, 편집기 열려 있을 때만).

## 6. 완료 기준

- 대표 AI 씬에서 기존 BT 그래프 에셋이 수정 없이 로드되고 같은 행동을 보인다
- C#으로 작성한 Action/Condition 노드가 편집기 목록에 뜨고 부착된다
- 프레임당 BT 관련 크로싱이 1회임을 로그로 확인
- `BTHeader.h`의 노드 구현과 `NodeFactory`가 제거된 상태로 전체 빌드 그린
