# Scene

**Header:** `ScriptBinder/Scene.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `Scene();`
- `~Scene();`
- `std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);`
- `std::shared_ptr<GameObject> CreateGameObject(std::string_view name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = -1);`
- `std::shared_ptr<GameObject> LoadGameObject(size_t instanceID, std::string_view name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = -1);`
- `std::shared_ptr<GameObject> GetGameObject(GameObject::Index index);`
- `std::shared_ptr<GameObject> TryGetGameObject(GameObject::Index index);`
- `void DetachGameObjectHierarchy(GameObject* root);`
- `GameObject::Index AttachExistingGameObject(std::shared_ptr<GameObject> go, GameObject::Index parentIndex);`
- `AttachExistingGameObjectHierarchy(const std::vector<std::shared_ptr<GameObject>>& roots);`
- `std::shared_ptr<GameObject> GetGameObject(std::string_view name);`
- `void AddSelectedSceneObject(GameObject* sceneObject);`
- `void RemoveSelectedSceneObject(GameObject* sceneObject);`
- `void ClearSelectedSceneObjects();`
- `void AddRootGameObject(std::string_view name);`
- `void DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject);`
- `void DestroyGameObject(GameObject::Index index);`
- `void CullMeshData();`
- `void InternalPauseUpdateForUI();`
- `std::vector<std::shared_ptr<GameObject>> CreateGameObjects(size_t createSize, GameObject::Index parentIndex = -1);`
- `void Awake();`
- `void OnEnable();`
- `void Start();`
- `void FixedUpdate(float deltaSecond);`
- `void OnTriggerEnter(const Collision& collider);`
- `void OnTriggerStay(const Collision& collider);`
- `void OnTriggerExit(const Collision& collider);`
- `void OnCollisionEnter(const Collision& collider);`
- `void OnCollisionStay(const Collision& collider);`
- `void OnCollisionExit(const Collision& collider);`
- `void Update(float deltaSecond);`
- `void YieldNull();`
- `void LateUpdate(float deltaSecond);`
- `void OnDisable();`
- `void OnDestroy();`
- `void AllDestroyMark();`
- `void ResetSelectedSceneObject();`
- `void CollectLightComponent(LightComponent* ptr);`
- `void UnCollectLightComponent(LightComponent* ptr);`
- `uint32 UpdateLight(LightProperties& lightProperties) const;`
- `std::pair<size_t, Light&> AddLight();`
- `Light& GetLight(size_t index);`
- `void RemoveLight(size_t index);`
- `void DestroyLight();`
- `void CollectMeshRenderer(MeshRenderer* ptr);`
- `void UnCollectMeshRenderer(MeshRenderer* ptr);`
- `void CollectSpriteRenderer(SpriteRenderer* ptr);`
- `void UnCollectSpriteRenderer(SpriteRenderer* ptr);`
- `void CollectTerrainComponent(TerrainComponent* ptr);`
- `void UnCollectTerrainComponent(TerrainComponent* ptr);`
- `void CollectFoliageComponent(FoliageComponent* ptr);`
- `void UnCollectFoliageComponent(FoliageComponent* ptr);`
- `void CollectDecalComponent(DecalComponent* ptr);`
- `void UnCollectDecalComponent(DecalComponent* ptr);`
- `void CollectRigidBodyComponent(RigidBodyComponent* ptr);`
- `void UnCollectRigidBodyComponent(RigidBodyComponent* ptr);`
- `void CollectColliderComponent(BoxColliderComponent* ptr);`
- `void CollectColliderComponent(SphereColliderComponent* ptr);`
- `void CollectColliderComponent(CapsuleColliderComponent* ptr);`
- `void CollectColliderComponent(MeshColliderComponent* ptr);`
- `void CollectColliderComponent(CharacterControllerComponent* ptr);`
- `void CollectColliderComponent(TerrainColliderComponent* ptr);`
- `void UnCollectColliderComponent(BoxColliderComponent* ptr);`
- `void UnCollectColliderComponent(SphereColliderComponent* ptr);`
- `void UnCollectColliderComponent(CapsuleColliderComponent* ptr);`
- `void UnCollectColliderComponent(MeshColliderComponent* ptr);`
- `void UnCollectColliderComponent(CharacterControllerComponent* ptr);`
- `void UnCollectColliderComponent(TerrainColliderComponent* ptr);`
- `void AddCanvas(const std::shared_ptr<GameObject>& canvas);`
- `void RemoveCanvas(const std::shared_ptr<GameObject>& canvas);`
- `std::shared_ptr<GameObject> FindCanvasName(std::string_view name);`
- `std::shared_ptr<GameObject> FindCanvasIndex(size_t index);`
- `void AllUpdateWorldMatrix();`
- `void AllUIUpdateWorldMatrix();`

## Public Properties
- `std::vector<std::shared_ptr<GameObject>> m_SceneObjects;`
- `std::future<void> m_AIFuture;`
- `HashingString m_sceneName;`
- `GameObject*					m_selectedSceneObject = nullptr;`
- `std::vector<GameObject*>	m_selectedSceneObjects;`
- `std::vector<std::shared_ptr<MeshRenderer>> m_visibleMeshesScratch;`
