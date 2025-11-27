# CreatorEngine API 문서 허브

> [!TIP]
> **Notion 템플릿 활용 팁**
> 
> 아래 레이아웃을 그대로 Notion 페이지에 붙여 넣으면 왼쪽에 목차, 오른쪽에 선택된 API 문서를 노출하는 2단 문서를 만들 수 있습니다. 엔드포인트 데이터베이스나 추가 문서를 위한 템플릿도 자유롭게 연결할 수 있습니다.

이 페이지는 `API_DOCS` 폴더에 있는 개별 API 문서를 한 화면에서 탐색할 수 있도록 간단한 목차와 뷰어를 제공합니다. 목차의 링크는 모두 같은 탭(`api-viewer`)을 겨냥하기 때문에 오른쪽 영역에서 문서를 전환할 수 있습니다.

> [!NOTE]
> **렌더링 주의**
> 
> 일부 뷰어(예: GitHub)에서는 보안 정책으로 인해 `iframe`이 차단될 수 있습니다. 그런 경우 왼쪽 링크를 클릭하면 브라우저가 해당 문서를 새 탭에서 직접 여니, 문서 자체는 여전히 접근 가능합니다. 정적 사이트 생성기나 Notion에 임베드할 때는 그대로 두면 오른쪽 영역에서 문서가 바뀝니다.

<style>
.docs-layout { display: grid; grid-template-columns: 280px 1fr; gap: 24px; align-items: start; }
.docs-nav { position: sticky; top: 12px; padding: 16px; border: 1px solid #3a3a3a; border-radius: 12px; background: linear-gradient(135deg, #10131a, #0c0d11); box-shadow: 0 12px 40px rgba(0,0,0,0.35); }
.docs-nav h2 { margin: 0 0 8px; font-size: 1.05rem; color: #dbe2ff; }
.docs-nav p { margin: 0 0 12px; font-size: 0.9rem; color: #b8bfd3; }
.docs-nav ol { list-style: none; margin: 0; padding: 0; display: grid; gap: 6px; max-height: 70vh; overflow: auto; }
.docs-nav a { color: #dfe6ff; text-decoration: none; padding: 8px 10px; border-radius: 8px; display: block; transition: background 0.15s ease, color 0.15s ease; }
.docs-nav a:hover { background: #1f2636; color: #ffffff; }
.docs-content { min-height: 70vh; border: 1px solid #3a3a3a; border-radius: 12px; padding: 16px; background: radial-gradient(circle at 20% 20%, rgba(107,142,255,0.08), transparent 45%), #0f1118; box-shadow: 0 12px 40px rgba(0,0,0,0.35); }
.docs-content h2 { margin-top: 0; color: #dbe2ff; }
.viewer-frame { width: 100%; height: 70vh; border: 1px solid #2f2f3a; border-radius: 12px; background: #0b0c12; box-shadow: inset 0 0 0 1px rgba(255,255,255,0.02); }
@media (max-width: 960px) { .docs-layout { grid-template-columns: 1fr; } .docs-nav { position: static; max-height: none; } .docs-nav ol { max-height: none; } .viewer-frame { height: 60vh; } }
</style>

<div class="docs-layout">
  <nav class="docs-nav">
    <h2>API 목차</h2>
    <p>아래 항목을 클릭하면 오른쪽 뷰어에서 문서가 열립니다.</p>
    <ol>
      <li><a href="./AIManager.md" target="api-viewer">AIManager</a></li>
      <li><a href="./AIType.md" target="api-viewer">AIType</a></li>
      <li><a href="./ActionMap.md" target="api-viewer">ActionMap</a></li>
      <li><a href="./ActionNode.md" target="api-viewer">ActionNode</a></li>
      <li><a href="./ActionType.md" target="api-viewer">ActionType</a></li>
      <li><a href="./AnchorPreset.md" target="api-viewer">AnchorPreset</a></li>
      <li><a href="./AniBehavior.md" target="api-viewer">AniBehavior</a></li>
      <li><a href="./AniTransition.md" target="api-viewer">AniTransition</a></li>
      <li><a href="./AnimationBehviourFatory.md" target="api-viewer">AnimationBehviourFatory</a></li>
      <li><a href="./AnimationController.md" target="api-viewer">AnimationController</a></li>
      <li><a href="./AnimationState.md" target="api-viewer">AnimationState</a></li>
      <li><a href="./Animator.md" target="api-viewer">Animator</a></li>
      <li><a href="./ArticulationData.md" target="api-viewer">ArticulationData</a></li>
      <li><a href="./ArticulationLoader.md" target="api-viewer">ArticulationLoader</a></li>
      <li><a href="./AvatarMask.md" target="api-viewer">AvatarMask</a></li>
      <li><a href="./BTNode.md" target="api-viewer">BTNode</a></li>
      <li><a href="./BehaviorNodeType.md" target="api-viewer">BehaviorNodeType</a></li>
      <li><a href="./BehaviorTreeComponent.md" target="api-viewer">BehaviorTreeComponent</a></li>
      <li><a href="./BlackBoard.md" target="api-viewer">BlackBoard</a></li>
      <li><a href="./BlackBoardType.md" target="api-viewer">BlackBoardType</a></li>
      <li><a href="./BoneMask.md" target="api-viewer">BoneMask</a></li>
      <li><a href="./BoxColliderComponent.md" target="api-viewer">BoxColliderComponent</a></li>
      <li><a href="./CSharpScriptComponent.md" target="api-viewer">CSharpScriptComponent</a></li>
      <li><a href="./CameraComponent.md" target="api-viewer">CameraComponent</a></li>
      <li><a href="./Canvas.md" target="api-viewer">Canvas</a></li>
      <li><a href="./CapsuleColliderComponent.md" target="api-viewer">CapsuleColliderComponent</a></li>
      <li><a href="./ChannelType.md" target="api-viewer">ChannelType</a></li>
      <li><a href="./CharacterControllerComponent.md" target="api-viewer">CharacterControllerComponent</a></li>
      <li><a href="./ClipDirection.md" target="api-viewer">ClipDirection</a></li>
      <li><a href="./Component.md" target="api-viewer">Component</a></li>
      <li><a href="./ComponentFactory.md" target="api-viewer">ComponentFactory</a></li>
      <li><a href="./CompositeNode.md" target="api-viewer">CompositeNode</a></li>
      <li><a href="./ConditionDecoratorNode.md" target="api-viewer">ConditionDecoratorNode</a></li>
      <li><a href="./ConditionNode.md" target="api-viewer">ConditionNode</a></li>
      <li><a href="./ConditionParameter.md" target="api-viewer">ConditionParameter</a></li>
      <li><a href="./ConditionType.md" target="api-viewer">ConditionType</a></li>
      <li><a href="./DecalComponent.md" target="api-viewer">DecalComponent</a></li>
      <li><a href="./DecoratorNode.md" target="api-viewer">DecoratorNode</a></li>
      <li><a href="./Direction.md" target="api-viewer">Direction</a></li>
      <li><a href="./EBodyType.md" target="api-viewer">EBodyType</a></li>
      <li><a href="./EForceMode.md" target="api-viewer">EForceMode</a></li>
      <li><a href="./EShapeType.md" target="api-viewer">EShapeType</a></li>
      <li><a href="./EffectComponent.md" target="api-viewer">EffectComponent</a></li>
      <li><a href="./FSMState.md" target="api-viewer">FSMState</a></li>
      <li><a href="./FoliageComponent.md" target="api-viewer">FoliageComponent</a></li>
      <li><a href="./FoliageMode.md" target="api-viewer">FoliageMode</a></li>
      <li><a href="./FunctionRegistry.md" target="api-viewer">FunctionRegistry</a></li>
      <li><a href="./GameObject.md" target="api-viewer">GameObject</a></li>
      <li><a href="./GameObjectType.md" target="api-viewer">GameObjectType</a></li>
      <li><a href="./HotLoadSystem.md" target="api-viewer">HotLoadSystem</a></li>
      <li><a href="./IAIComponent.md" target="api-viewer">IAIComponent</a></li>
      <li><a href="./IRegistableEvent.md" target="api-viewer">IRegistableEvent</a></li>
      <li><a href="./IScriptedFSM.md" target="api-viewer">IScriptedFSM</a></li>
      <li><a href="./ISerializable.md" target="api-viewer">ISerializable</a></li>
      <li><a href="./ImageComponent.md" target="api-viewer">ImageComponent</a></li>
      <li><a href="./InputAction.md" target="api-viewer">InputAction</a></li>
      <li><a href="./InputActionManager.md" target="api-viewer">InputActionManager</a></li>
      <li><a href="./InputManager.md" target="api-viewer">InputManager</a></li>
      <li><a href="./InputType.md" target="api-viewer">InputType</a></li>
      <li><a href="./InputValueType.md" target="api-viewer">InputValueType</a></li>
      <li><a href="./InvalidScriptComponent.md" target="api-viewer">InvalidScriptComponent</a></li>
      <li><a href="./InverterNode.md" target="api-viewer">InverterNode</a></li>
      <li><a href="./KeyState.md" target="api-viewer">KeyState</a></li>
      <li><a href="./KeyboardState.md" target="api-viewer">KeyboardState</a></li>
      <li><a href="./LightComponent.md" target="api-viewer">LightComponent</a></li>
      <li><a href="./LinkData.md" target="api-viewer">LinkData</a></li>
      <li><a href="./MeshColliderComponent.md" target="api-viewer">MeshColliderComponent</a></li>
      <li><a href="./MeshRenderer.md" target="api-viewer">MeshRenderer</a></li>
      <li><a href="./Mode.md" target="api-viewer">Mode</a></li>
      <li><a href="./ModuleBehavior.md" target="api-viewer">ModuleBehavior</a></li>
      <li><a href="./MonoBehaviorEvent.md" target="api-viewer">MonoBehaviorEvent</a></li>
      <li><a href="./MonoManager.md" target="api-viewer">MonoManager</a></li>
      <li><a href="./MouseState.md" target="api-viewer">MouseState</a></li>
      <li><a href="./NodeFactory.md" target="api-viewer">NodeFactory</a></li>
      <li><a href="./NodeStatus.md" target="api-viewer">NodeStatus</a></li>
      <li><a href="./Object.md" target="api-viewer">Object</a></li>
      <li><a href="./PadState.md" target="api-viewer">PadState</a></li>
      <li><a href="./ParallelNode.md" target="api-viewer">ParallelNode</a></li>
      <li><a href="./ParallelPolicy.md" target="api-viewer">ParallelPolicy</a></li>
      <li><a href="./PhysicsManager.md" target="api-viewer">PhysicsManager</a></li>
      <li><a href="./PlayerInputComponent.md" target="api-viewer">PlayerInputComponent</a></li>
      <li><a href="./Prefab.md" target="api-viewer">Prefab</a></li>
      <li><a href="./PrefabEditor.md" target="api-viewer">PrefabEditor</a></li>
      <li><a href="./PrefabUtility.md" target="api-viewer">PrefabUtility</a></li>
      <li><a href="./RagdollComponent.md" target="api-viewer">RagdollComponent</a></li>
      <li><a href="./RectTransformComponent.md" target="api-viewer">RectTransformComponent</a></li>
      <li><a href="./RegistableEvent.md" target="api-viewer">RegistableEvent</a></li>
      <li><a href="./RigidBodyComponent.md" target="api-viewer">RigidBodyComponent</a></li>
      <li><a href="./Rolloff.md" target="api-viewer">Rolloff</a></li>
      <li><a href="./Scene.md" target="api-viewer">Scene</a></li>
      <li><a href="./SceneManager.md" target="api-viewer">SceneManager</a></li>
      <li><a href="./SelectorNode.md" target="api-viewer">SelectorNode</a></li>
      <li><a href="./SequenceNode.md" target="api-viewer">SequenceNode</a></li>
      <li><a href="./SoundComponent.md" target="api-viewer">SoundComponent</a></li>
      <li><a href="./SoundManager.md" target="api-viewer">SoundManager</a></li>
      <li><a href="./SphereColliderComponent.md" target="api-viewer">SphereColliderComponent</a></li>
      <li><a href="./SpriteRenderer.md" target="api-viewer">SpriteRenderer</a></li>
      <li><a href="./SpriteSheetComponent.md" target="api-viewer">SpriteSheetComponent</a></li>
      <li><a href="./StateMachineComponent.md" target="api-viewer">StateMachineComponent</a></li>
      <li><a href="./StaticGameObjectType.md" target="api-viewer">StaticGameObjectType</a></li>
      <li><a href="./StealPolicy.md" target="api-viewer">StealPolicy</a></li>
      <li><a href="./TagManager.md" target="api-viewer">TagManager</a></li>
      <li><a href="./TerrainColliderComponent.md" target="api-viewer">TerrainColliderComponent</a></li>
      <li><a href="./TerrainComponent.md" target="api-viewer">TerrainComponent</a></li>
      <li><a href="./TextAlignment.md" target="api-viewer">TextAlignment</a></li>
      <li><a href="./TextComponent.md" target="api-viewer">TextComponent</a></li>
      <li><a href="./TransCondition.md" target="api-viewer">TransCondition</a></li>
      <li><a href="./Transition.md" target="api-viewer">Transition</a></li>
      <li><a href="./Type.md" target="api-viewer">Type</a></li>
      <li><a href="./UIButton.md" target="api-viewer">UIButton</a></li>
      <li><a href="./UIColliderType.md" target="api-viewer">UIColliderType</a></li>
      <li><a href="./UIComponent.md" target="api-viewer">UIComponent</a></li>
      <li><a href="./UIEffects.md" target="api-viewer">UIEffects</a></li>
      <li><a href="./UIManager.md" target="api-viewer">UIManager</a></li>
      <li><a href="./UItype.md" target="api-viewer">UItype</a></li>
      <li><a href="./ValueType.md" target="api-viewer">ValueType</a></li>
      <li><a href="./VolumeComponent.md" target="api-viewer">VolumeComponent</a></li>
      <li><a href="./WeightedSelectorNode.md" target="api-viewer">WeightedSelectorNode</a></li>
    </ol>
  </nav>
  <section class="docs-content">
    <h2>API 문서 뷰어</h2>
    <p>기본으로 <code>AIManager.md</code>가 로드됩니다. 목차의 다른 항목을 클릭하면 이 영역에 바로 표시됩니다.</p>
    <iframe class="viewer-frame" name="api-viewer" src="./AIManager.md" title="API 문서 뷰어"></iframe>
    <details>
      <summary>Notion API 문서 템플릿 예시 펼치기</summary>
      <p>Notion API와 같이 길이가 긴 참고 문서를 정리할 때 사용할 수 있는 기본 구조입니다.</p>
      <aside>
        <strong>❗ Notion 팁:</strong> 이 템플릿을 활용해 Notion에서 API 레퍼런스를 작성하고 호스팅해 보세요. 엔드포인트 데이터베이스에는 추가 문서를 위한 엔드포인트 템플릿도 포함되어 있습니다.
      </aside>
      <h3>기본 정보</h3>
      <h4>시작하기</h4>
      <p>이 문서는 Notion API에 대한 전반적 이해를 돕는 것을 목적으로 합니다.</p>
      <aside>
        <strong>❗ Notion API를 사용하려면 토큰이 필요합니다.</strong> 토큰은 연결 설정 페이지에서 연결을 생성한 후 받을 수 있습니다. Notion API를 처음 사용한다면, <a href="https://developers.notion.com/docs/getting-started">시작하기 가이드</a>를 통해 연결 생성 방법을 알아보세요.
      </aside>
      <h4>규칙</h4>
      <ul>
        <li>기본 URL은 <code>https://api.notion.com</code>이며 모든 요청에는 HTTPS가 필요합니다.</li>
        <li>Notion API는 가능한 한 RESTful 규칙을 따르며, 요청과 응답 본문은 JSON으로 인코딩됩니다.</li>
        <li>JSON 규칙, 페이지네이션, 요청 제한(속도·사이즈), 상태 코드, 버전 관리 등 세부 사항을 문서화하세요.</li>
      </ul>
      <h3>페이지네이션·요청 제한 예시 테이블</h3>
      <p>API별 지원 여부나 응답 필드를 표 형태로 정리해 두면 문서 일관성을 유지할 수 있습니다.</p>
      <h3>버전 헤더 예시</h3>
      <pre><code>curl https://api.notion.com/v1/users/01da9b00-e400-4959-91ce-af55307647e5 \
  -H "Authorization: Bearer &lt;token&gt;" \
  -H "Notion-Version: 2022-06-28"</code></pre>
      <p>이 블록을 필요한 API 예제에 맞게 변경하여 사용하세요.</p>
    </details>
  </section>
</div>
