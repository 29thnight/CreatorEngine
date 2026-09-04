# 스크립트 표면 재설계 (PHASE 9.5)

> 신설 2026-09-04. C# 스크립트 계층이 무엇을 좇도록 설계돼 있었는지와, 그것을 현재
> 네이티브 기준으로 다시 세우는 작업. `ScriptApiMigrationPlan.html`을 승계한다.

## 0. 왜 이 페이즈가 필요한가

C# 표면은 엔진을 따라가지 못한 것이 아니라, **애초에 엔진을 따라가도록 설계되지
않았다.**

`ScriptApiMigrationPlan.html` §11의 T1~T4 등급표는 2026-07-29 시점의
`Dynamic_CPP\Assets\Script` 코퍼스를 실측해 만든 것이다. "GetComponent 734회 ·
Transform 접근 1위 · 입력 43회" 같은 숫자가 표면의 모양을 정했다. 그런데 그
코퍼스는 `8ae17250`에서 포팅 없이 통째로 삭제됐다(349파일 · 37,788줄 삭제 ·
새 GameScripts 0건). **기준이 사라졌는데 표는 갱신되지 않았고**, 그 계획서의
S3·S4는 대시보드 항목이 된 적이 없다. 같은 기간 `Engine/`에 115커밋이 들어왔다.

그래서 표면은 "옛 게임 코드가 그때 무엇을 얼마나 불렀는가"를 기준으로 굳어 있었다:

- **Transform이 자유 함수 묶음**이었다 — 계획서가 "Transform은 컴포넌트가 아니다"를
  전제로 적었는데, 그 전제는 트랙 L2가 닫힌 이틀 뒤 `c9bbdd56`(S1-b)에서 뒤집혔다
- **입력이 폴링 전용**이었다 — 티어표가 "T3: 호출이 적어 단순 래핑으로 충분"이라 적었다
- **컴포넌트 32종 중 13종만 노출**돼 있었다 — 나머지는 늦게 생긴 것이 아니라
  애초에 범위 밖이었다(전부 `8-16` 이전부터 kTable에 있다)

## 1. 완료분

### W1 — 골격 (완료 `79ac93ae` · 2026-09-04)

기반 클래스를 네이티브와 1:1로 세웠다.

```
Component (6단계 훅)              ← 사용자 스크립트가 직접 상속 (옛 Behaviour)
└ NativeComponent : Component     ← 핸들 래퍼
  └ Transform : NativeComponent   ← 옛 readonly struct
```

- `Behaviour` 이름 폐기 — 네이티브에서 6단계 생명주기를 소유하는 계층의 이름이
  `Component`이고 `Transform`·`MeshRenderer`·`ScriptComponent`가 모두 거기서 파생한다.
  `Behaviour`는 일어나지 않은 Unity 관례 이식의 잔재였다
- `BehaviourRegistry`→`ScriptRegistry`, `BehaviourGenerator`→`ScriptGenerator` —
  `Component*`로 가면 네이티브 `ComponentFactory`·`RegisterAllComponents`(컴포넌트
  **타입** 등록부)와 뜻이 부딪힌다
- `Transform_Exists` 신설(`kApiVersion` 19→20) — UI/Canvas는 Transform이 없고
  `Entity::Transform_()`이 공유 더미를 돌려주므로 쓰기가 조용히 버려진다.
  썽크는 `HasTransform()`을 직접 본다(전자를 쓰면 검사가 항상 "있다"가 된다)

### W2 — 컴포넌트 노출 (완료 `84419886`·`ebab7c7f` · 2026-09-04)

**저작분이 규모를 정했다.** 계획 메모는 미노출 19종을 적었지만 실측하니:

| 컴포넌트 | 저작 건수 | 처리 |
|---|---:|---|
| Transform | 440 | 기존 |
| BoneComponent | 234 | 마커(필드 0·메서드 1) — 노출할 것 없음 |
| MeshRenderer | 103 | 기존 |
| **LightComponent** | **30** | **W2에서 신설** |
| **CameraComponent** | **20** | **W2에서 신설** |
| RectTransform·Image·Canvas·Text·Animator | 4~20 | 기존 |
| Terrain·Volume·Decal·Foliage·Ragdoll·MeshCollider·PlayerInput·SpriteRenderer·SpriteSheet·StateMachine·TerrainCollider | **0** | 착수 안 함 (§2.5) |

관리 측이 네이티브 컴포넌트를 **붙일 수단이 없으므로**(`CreateComponent`는
네이티브→관리 방향의 스크립트 인스턴스 생성이다) 저작분 0인 타입에 래퍼를 만들면
`GetComponent<T>()`가 원리적으로 언제나 null인 죽은 표면이 된다.

두 컴포넌트의 **검증해야 할 사슬이 서로 달랐다**:

- `LightComponent`는 렌더 프록시를 쓴다. 값 writer가 `PublishRenderProxyDirty`를
  내지 않으면 화면이 그대로인데 컴포넌트 필드는 바뀌므로 **되읽으면 새 값이 나온다** —
  값 왕복 단정은 이 결함에 원리적으로 눈멀다. dirty 사슬 축을 따로 세웠다
- `CameraComponent`는 프록시를 쓰지 않는다(`Scene.cpp`의 프록시 `Kind` 열거에
  Camera가 없다). 매 프레임 `CaptureFrameSnapshot`으로 읽히므로 발행할 dirty가
  애초에 없고 게이트 축도 하나다

**명명 규칙**: C# 래퍼는 네이티브 이름 그대로이고 예외가 없다(15종 대조 확인).

---

## 2. 잔여 트랙

### W3 — 입력 액션 값 통로 (대기 · P1 · 4일)

**지금 스크립트는 액션이 발화했다는 "이름"만 받고 값은 못 받는다.**

경로가 이렇게 서 있다:

```
PlayerInputComponent::Update
  → ActionMap::CheckAction(playerIndex, scriptType, onFired)
    → onFired(action->funName)
      → ClrHost::QueueScriptMessage(instanceId, functionName)
        → ScriptMessage { int instanceId; char name[64]; }
          → C# InvokeMessage(instanceId, name)
```

값이 두 곳에서 끊긴다:

1. **`ActionMap.cpp:317,327`의 `valueAction(action->value.v2Value)`가 주석 처리돼
   있다.** 바로 위에서 `v2Value`를 키보드 4방향 또는 게임패드 스틱으로 채워 놓고
   `onFired(funName)`만 부른다 — 계산된 값이 그 자리에서 버려진다
2. **`ScriptMessage`에 인자 필드가 없다.** `{int instanceId; char name[64]}`뿐이라
   1을 살려도 값을 실을 곳이 없다

**착수 전 결정이 필요하다** — 둘 중 하나를 골라야 하고, 고르는 순간 미러 계약이 바뀐다:

| 안 | 방법 | 대가 |
|---|---|---|
| A | `ScriptMessage`에 값 필드 추가 | 구조체 배치가 바뀌어 `kApiVersion` 상승 + 큐 메모리 증가(모든 메시지가 안 쓰는 값 필드를 든다) |
| B | 질의 API 신설 — 메시지는 이름만, 값은 `Input_GetActionValue(name)` | 발화 시점과 질의 시점 사이에 값이 바뀔 수 있다(틱 경계라 실제로는 안전하나 계약이 약해진다) |

**소비자도 함께 저작해야 한다.** 어느 씬에도 `PlayerInputComponent`가 0건이므로
지금은 이 경로가 한 번도 돌지 않는다. 게이트를 세우려면 컴포넌트를 붙인 씬과
액션 맵 자산을 함께 만들어야 하고, 그것 없이는 "고쳤다"를 증명할 수 없다.

완료 조건: 스크립트가 Vector2 액션 값을 받아 단정하는 회귀가 run-all에서 돈다.

---

### W4 — 저작 dirty 통로 (대기 · P1 · 3일)

**리플렉션 인스펙터가 값을 대입만 하고 dirty를 모른다.**

`ReflectionTypedDraw.h`에 `ProxyDirty` 언급이 0건이다. 그래서 인스펙터에서 라이트
색이나 세기를 바꾸면 컴포넌트 필드는 바뀌지만 `LightRenderProxy`는 낡은 채 남는다.
W2가 만든 게이트의 **변이 상태가 정확히 지금의 인스펙터 경로**다 —
`LightComponent`의 setter에서 `PublishRenderProxyDirty` 6줄을 지우면 프로브의
값 왕복 15건은 그대로 통과하고 dirty 사슬 축만 붉어진다.

W2는 setter를 신설하는 데서 멈췄다. 필드가 여전히 `public`이라 우회할 수 있고,
지금 `private`으로 내리면 저작 경로가 멈추기 때문이다.

닫는 순서:

1. 리플렉션 draw가 값 변경을 알릴 훅을 갖는다(필드 단위 후처리 또는 컴포넌트 단위 통지)
2. 렌더 프록시를 쓰는 컴포넌트가 그 훅에서 dirty를 발행한다
3. 그 뒤에야 `LightComponent`의 값 필드를 `private`으로 내릴 수 있다

완료 조건: 인스펙터 경로로 값을 바꾸는 CLI 저작이 프록시까지 닿는 것을 게이트가 잰다
(그 관측은 W5에 걸린다).

---

### W5 — 렌더 델타 관측 하네스 (대기 · P2 · 3일)

**`--script` 헤드리스는 렌더 프레임이 거의 돌지 않아 프록시 값을 관측할 수 없다.**

실측(2026-09-04, 기본 씬 라이트에 스크립트로 8회 쓰기):

```
기준선  publish=1 committed=1 queued=3 applied=2
이후    publish=9 committed=2 queued=4 applied=2
```

게임 스레드 쪽은 전부 정상이고 `applied`만 멈춰 있다. `RenderScene::UpdateCommand`는
프록시에 쓰지 않고 `ProxyCommandQueue`에 델타를 넣을 뿐이며, 그것을 적용하는
`ExecuteBatch`는 렌더 소비 스레드 전용이다. 같은 실행의 종료 로그가
`publish 64 / consume 3 / latest-wins 61 / overflow 61`.

**이 한계를 제품 결함으로 오진한 적이 있다.** `scene.switch`로 씬을 열면 그 씬의
광원이 프록시에 안 잡히고 옛 씬 값이 남는 것을 "씬 전환 시 광원 미등록"으로
보고했는데, 새 광원의 `RegisterCommand`도 옛 광원의 `UnregisterCommand`도 같은
큐로 가므로 같은 한계의 두 얼굴이었다. 갈라 준 것은 `enqueued`/`applied` 한 쌍이다.

그래서 W2의 Light 게이트는 게임 스레드 쪽 사슬(writer → dirty → 커밋 → 큐잉)까지만
재고, 마지막 한 칸을 파일 머리에 실측치와 함께 적어 뒀다.

닫는 방법 후보:

- 헤드리스에서 렌더 프레임을 N회 강제로 도는 CLI(`dx12.live`는 렌더 0프레임으로 이미 기록됨)
- 또는 프록시 델타를 게임 스레드에서 동기 적용하는 검사 전용 진입점

완료 조건: Light 게이트의 ② 축이 프록시 **값**을 대조하도록 돌아가고, dirty 제거
변이에 붉어진다.

---

### W6 — 미러 커버리지 (대기 · P2 · 2일)

게이트 3종이 서 있지만(`check-api-table`·`check-bt-enums`·`check-entry-points`)
경계를 넘는 것 중 **검사 없는 미러가 셋 남아 있다**. 셋 다 `ClrHost.h`에 정의가
있고 주석만 "C#과 맞춰야 한다"고 적는다:

| 미러 | 어긋나면 |
|---|---|
| `ScriptLifecyclePhase` | 6단계 훅이 엉뚱한 단계로 불린다 |
| `ScriptFieldType` | `SerializeField` 값이 잘못된 타입으로 왕복한다 |
| `ScriptMessage` 배치 | 메시지 이름이 깨진다(W3이 필드를 더하면 더 위험해진다) |

**`SerializeField` 왕복 게이트도 없다.** `CaptureFields`/`RestoreFields`가 7종
대칭인데 저작 자산에 직렬화된 값이 **0건**이라 실자산 왕복 전례가 없다. 저작분을
만들어 저장/로드 왕복을 재야 한다.

완료 조건: 미러 3종 대조가 run-all에서 돌고 각각 변이로 이빨을 증명한다.
`SerializeField` 7종을 실제 저작한 씬으로 왕복 게이트가 선다.

---

### W7 — 문서 청산 (대기 · P3 · 1일)

- `ScriptApiMigrationPlan.html` §11의 T1~T4 등급표는 **폐기됐다**. 기준 코퍼스가
  삭제된 뒤에도 그 표가 남아 다음 작업자를 잘못 이끈다 — 폐기 표시와 이 계획서로의
  포인터를 박는다
- 같은 문서의 "Transform은 컴포넌트가 아니다"는 `c9bbdd56`(S1-b)에서 뒤집혔다
- `BehaviorTreeManagedPlan.md`가 `BehaviourRegistry`를 지목한다(→ `ScriptRegistry`)
- 애니메이션의 `SetBehaviour`/`CreateBehaviour` 계열은 **별개 체계**라 개명 대상이
  아니다. 문서에 그 경계를 적어 두지 않으면 다음 스윕이 또 건드린다

### W8 — 디스패치 오버라이드 감지 (대기 · P1 · 1일)

**`ScriptRegistry`가 훅마다 `_active` 전체를 돈다.** 오버라이드하지 않은 스크립트도
빈 가상 호출을 겪는다. 2026-09-04 실측: `override void PrePhysics`는 **0건**인데
`Component` 파생 42개를 매 프레임 순회한다.

같은 파일 안에서 이미 갈려 있다 — 6단계 훅은 `Invoke(b, static x => x.OnEnable(), ...)`로
할당 없는 static 람다인데, 틱 훅만 `Invoke(b, x => x.PrePhysics(dt), ...)`라 `dt`를
캡처해 **호출마다 할당**이 붙는다.

네이티브에 선례가 있다. `Lifecycle::MaskOfType<T>()`가
`&T::Hook != &Component::Hook`으로 컴파일 타임에 판정하고 `PhaseBits` 마스크를
세운다(옛 틱 비트 `1u<<3~5`는 2026-09-04 `5b9c8250`에서 철거했고 8개 단계 훅
판정만 남았다). 관리 측은 컴파일 타임이 아니므로 **타입당 1회 계산해 캐시**한다 —
`GetType().GetMethod(...).DeclaringType != typeof(Component)`, 또는 `ScriptGenerator`가
마스크를 생성. 디스패치는 마스크로 걸러진 목록만 순회한다.

**왜 지금인가.** `NetworkFrameworkPlan` N3-b가 고정 축 훅
`OnSimulationTick(fixedDelta)`을 신설하는데, 그것은 **tick당** 돈다. 감지 없이
추가하면 30fps에서 프레임당 순회가 42→84회가 된다. **W8이 그 슬라이스의 선행이며,
`ScriptRegistry`를 이 페이즈가 일관되게 소유하도록 PHASE 20이 아니라 여기에 둔다.**

완료 조건: 훅별 구독 목록이 실제 오버라이드 수에 비례하고, 오버라이드 0건인 훅의
순회가 0이 된다. 생명주기 순서 게이트가 개명 때와 같이 사건 순서를 보존한다.

---

## 3. 착수 순서와 의존

```
W3 (입력) ── 독립. 단, 결정 A/B가 W6의 ScriptMessage 미러 계약을 바꾼다
W4 (저작 dirty) ──┐
                  ├── W5가 없으면 W4의 완료를 증명할 수 없다
W5 (관측 하네스) ─┘   └─► NetworkFrameworkPlan ND-b가 W4의 훅에 관찰자를 분기한다
W6 (미러 커버리지) ── W3의 결정 뒤에 ScriptMessage 항목을 확정
W7 (문서) ── 언제든
W8 (디스패치 감지) ── 독립 ─► NetworkFrameworkPlan N3-b의 선행
```

권장: **W5 → W4 → W3 → W6 → W7**. W5가 먼저 서야 W4의 "고쳤다"가 관측 가능해지고,
W3의 결정은 W6의 계약을 정하므로 W6보다 앞서야 한다. W8은 이 사슬과 독립이라
언제든 넣을 수 있으나, PHASE 20이 N3-b를 착수하기 전에는 서야 한다.

**PHASE 20이 이 페이즈에 거는 의존 둘.** 네트워크 페이즈는 통째로 외부 담당자에게
이양할 것이므로(`NetworkFrameworkPlan` §6), 그 경계가 이 페이즈의 산출물 위에 선다.

| PHASE 20 슬라이스 | 이 페이즈의 선행 | 이유 |
|---|---|---|
| N3-b 고정 클럭 + `OnSimulationTick` | **W8** | tick당 도는 훅을 감지 없이 더하면 순회가 배가 된다 |
| ND-b 쓰기 dirty 확장점 | **W4** | W4가 리플렉션 draw → 훅 통로를 세워야 그 위에 관찰자를 붙인다. 지금은 `ReflectionTypedDraw.h`에 `ProxyDirty`·`OnPropertyChanged` 언급이 0건이라 저작 축에 통로 자체가 없다 |

## 4. 이 페이즈가 지키는 원칙

1. **저작분이 규모를 정한다.** 계획서의 목록이 아니라 실제 자산 건수를 세고, 0건이면
   착수하지 않는다 — 관리 측이 컴포넌트를 붙일 수 없으므로 죽은 표면이 된다
2. **값 왕복 단정만으로 통과시키지 않는다.** 컴포넌트 필드는 바뀌므로 되읽으면 항상
   새 값이 나온다. 그 값이 소비자(렌더·직렬화·이벤트)까지 가는 축을 따로 잰다
3. **컴포넌트마다 검증할 사슬이 다르다.** Light는 dirty 사슬이 있고 Camera는 없다.
   같은 형태의 게이트를 복사하면 한쪽은 아무것도 재지 않는다
4. **못 잡는 것을 적는 데까지가 증명이다.** 하네스가 볼 수 없는 칸은 실측치와 함께
   게이트 파일에 남긴다 — 적어 두지 않으면 다음 사람이 같은 자리에서 오진한다
