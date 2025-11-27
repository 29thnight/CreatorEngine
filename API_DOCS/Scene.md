# Scene

**Header:** `ScriptBinder/Scene.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Scene` | scene 동작을 수행합니다. |
| `~Scene` | scene 동작을 수행합니다. |
| `AddGameObject` | game object을(를) 추가합니다. |
| `CreateGameObject` | game object을(를) 생성합니다. |
| `LoadGameObject` | game object을(를) 불러옵니다. |
| `GetGameObject` | game object을(를) 가져옵니다. |
| `TryGetGameObject` | try get game object 동작을 수행합니다. |
| `DetachGameObjectHierarchy` | detach game object hierarchy 동작을 수행합니다. |
| `AttachExistingGameObject` | attach existing game object 동작을 수행합니다. |
| `AttachExistingGameObjectHierarchy` | attach existing game object hierarchy 동작을 수행합니다. |
| `AddSelectedSceneObject` | selected scene object을(를) 추가합니다. |
| `RemoveSelectedSceneObject` | selected scene object을(를) 제거합니다. |
| `ClearSelectedSceneObjects` | selected scene objects을(를) 비웁니다. |
| `AddRootGameObject` | root game object을(를) 추가합니다. |
| `DestroyGameObject` | game object을(를) 파괴합니다. |
| `CullMeshData` | cull mesh data 동작을 수행합니다. |
| `InternalPauseUpdateForUI` | internal pause update for ui 동작을 수행합니다. |
| `CreateGameObjects` | game objects을(를) 생성합니다. |
| `Awake` | awake 동작을 수행합니다. |
| `OnEnable` | on enable 동작을 수행합니다. |
| `Start` | start 동작을 수행합니다. |
| `FixedUpdate` | fixed update 동작을 수행합니다. |
| `OnTriggerEnter` | on trigger enter 동작을 수행합니다. |
| `OnTriggerStay` | on trigger stay 동작을 수행합니다. |
| `OnTriggerExit` | on trigger exit 동작을 수행합니다. |
| `OnCollisionEnter` | on collision enter 동작을 수행합니다. |
| `OnCollisionStay` | on collision stay 동작을 수행합니다. |
| `OnCollisionExit` | on collision exit 동작을 수행합니다. |
| `Update` | update을(를) 갱신합니다. |
| `YieldNull` | yield null 동작을 수행합니다. |
| `LateUpdate` | late update 동작을 수행합니다. |
| `OnDisable` | on disable 동작을 수행합니다. |
| `OnDestroy` | on destroy 동작을 수행합니다. |
| `AllDestroyMark` | all destroy mark 동작을 수행합니다. |
| `ResetSelectedSceneObject` | reset selected scene object 동작을 수행합니다. |
| `CollectLightComponent` | collect light component 동작을 수행합니다. |
| `UnCollectLightComponent` | un collect light component 동작을 수행합니다. |
| `UpdateLight` | light을(를) 갱신합니다. |
| `AddLight` | light을(를) 추가합니다. |
| `GetLight` | light을(를) 가져옵니다. |
| `RemoveLight` | light을(를) 제거합니다. |
| `DestroyLight` | light을(를) 파괴합니다. |
| `CollectMeshRenderer` | collect mesh renderer 동작을 수행합니다. |
| `UnCollectMeshRenderer` | un collect mesh renderer 동작을 수행합니다. |
| `CollectSpriteRenderer` | collect sprite renderer 동작을 수행합니다. |
| `UnCollectSpriteRenderer` | un collect sprite renderer 동작을 수행합니다. |
| `CollectTerrainComponent` | collect terrain component 동작을 수행합니다. |
| `UnCollectTerrainComponent` | un collect terrain component 동작을 수행합니다. |
| `CollectFoliageComponent` | collect foliage component 동작을 수행합니다. |
| `UnCollectFoliageComponent` | un collect foliage component 동작을 수행합니다. |
| `CollectDecalComponent` | collect decal component 동작을 수행합니다. |
| `UnCollectDecalComponent` | un collect decal component 동작을 수행합니다. |
| `CollectRigidBodyComponent` | collect rigid body component 동작을 수행합니다. |
| `UnCollectRigidBodyComponent` | un collect rigid body component 동작을 수행합니다. |
| `CollectColliderComponent` | collect collider component 동작을 수행합니다. |
| `UnCollectColliderComponent` | un collect collider component 동작을 수행합니다. |
| `AddCanvas` | canvas을(를) 추가합니다. |
| `RemoveCanvas` | canvas을(를) 제거합니다. |
| `FindCanvasName` | canvas name을(를) 탐색합니다. |
| `FindCanvasIndex` | canvas index을(를) 탐색합니다. |
| `AllUpdateWorldMatrix` | all update world matrix 동작을 수행합니다. |
| `AllUIUpdateWorldMatrix` | all uiupdate world matrix 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `m_SceneObjects` | m scene objects 상태를 보관합니다. |
| `m_AIFuture` | m aifuture 상태를 보관합니다. |
| `m_sceneName` | m scene name 상태를 보관합니다. |
| `m_selectedSceneObject` | m selected scene object 상태를 보관합니다. |
| `m_selectedSceneObjects` | m selected scene objects 상태를 보관합니다. |
| `m_visibleMeshesScratch` | m visible meshes scratch 상태를 보관합니다. |
