# GameObject

**Header:** `ScriptBinder/GameObject.h`

**Inheritance:** `: public Object, public std::enable_shared_from_this<GameObject>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `static constexpr GameObject::Index INVALID_INDEX = std::numeric_limits<uint32_t>::max();`
- `GameObject();`
- `GameObject(Scene* scene, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);`
- `GameObject(Scene* scene, size_t instanceID, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);`
- `GameObject(GameObject&) = delete;`
- `GameObject(GameObject&&) noexcept = default;`
- `GameObject& operator=(GameObject&) = delete;`
- `~GameObject() override = default;`
- `const std::string& RemoveSuffixNumberTag() const;`
- `void SetTag(std::string_view tag);`
- `void SetLayer(std::string_view layer);`
- `virtual void Destroy() override final;`
- `std::shared_ptr<Component> AddComponent(const Meta::Type& type);`
- `ModuleBehavior* AddScriptComponent(std::string_view scriptName);`
- `std::shared_ptr<Component> GetComponent(const Meta::Type& type);`
- `std::shared_ptr<Component> GetComponentByTypeID(uint32 id);`
- `void RefreshComponentIdIndices();`
- `void AddChild(GameObject* _objcet);`
- `T* AddComponent();`
- `T* AddComponent(Args&&... args);`
- `T* GetComponent(uint32 id);`
- `T* GetComponent();`
- `T* GetComponentDynamicCast();`
- `std::vector<T*> GetComponentsInChildren();`
- `std::vector<T*> GetComponentsInchildrenDynamicCast();`
- `bool HasComponent();`
- `std::vector<T*> GetComponents();`
- `void RemoveComponent(T* component);`
- `void RemoveComponentIndex(uint32 id);`
- `void RemoveComponentTypeID(uint32 typeID);`
- `void RemoveScriptComponent(std::string_view scriptName);`
- `void RemoveScriptComponent(ModuleBehavior* ptr);`
- `void RemoveComponent(Meta::Type& type);`
- `static GameObject* Find(std::string_view name);`
- `static GameObject* FindIndex(GameObject::Index index);`
- `static GameObject* FindInstanceID(const HashedGuid& guid);`
- `static GameObject* FindAttachedID(const HashedGuid& guid);`
- `GameObject* OwnerSceneFind(std::string_view name);`
- `GameObject* OwnerSceneFindIndex(GameObject::Index index);`
- `GameObject* OwnerSceneFindInstanceID(const HashedGuid& guid);`
- `GameObject* OwnerSceneFindAttachedID(const HashedGuid& guid);`
- `void SetEnabled(bool able) override final;`
- `void SetCollisionType();`

## Public Properties
- `using Index = int;`
- `uint32 m_collisionType = 0;`
- `std::vector<GameObject::Index> m_childrenIndices;`
