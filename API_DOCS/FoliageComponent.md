# FoliageComponent

**Header:** `ScriptBinder/FoliageComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<FoliageComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Awake() override;`
- `void Update(float deltaTime) override;`
- `void OnDestroy() override;`
- `void SaveFoliageAsset(const file::path& savePath);`
- `void LoadFoliageAsset(FileGuid assetGuid);`
- `void AddFoliageType(const FoliageType& type);`
- `void RemoveFoliageType(uint32 typeID);`
- `void AddFoliageInstance(const FoliageInstance& instance);`
- `void RemoveFoliageInstance(size_t index);`
- `void AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance);`
- `void AddRandomInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush, uint32 typeID, int count);`
- `void RemoveInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush);`
- `void UpdateFoliageCullingData(Camera* camera);`

## Public Properties
- (none)
