# TerrainComponent

**Header:** `ScriptBinder/Terrain.h`

**Inheritance:** `: public Component, public RegistableEvent<TerrainComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `TerrainComponent();`
- `virtual ~TerrainComponent() = default;`
- `void Initialize();`
- `void Resize(int newWidth, int newHeight);`
- `virtual void Awake() override;`
- `virtual void OnDestroy() override;`
- `void Save(const std::wstring& assetRoot, const std::wstring& name);`
- `bool Load(const std::wstring& filePath);`
- `void BuildOutTrrain(const std::wstring& buildPath, const std::wstring& terrainName);`
- `bool LoadRunTimeTerrain(const std::wstring& filePath);`
- `void ApplyBrush(const TerrainBrush& brush);`
- `void RecalculateNormalsPatch(int minX, int minY, int maxX, int maxY);`
- `void PaintLayer(uint32_t layerId, int x, int y, float strength);`
- `void UpdateLayerDesc();`
- `void AddLayer(const std::wstring& path, const std::wstring& diffuseFile, float tilling);`
- `void RemoveLayer(uint32_t layerID);`
- `void ClearLayers();`
- `void RefreshTexture();`
- `bool LoadBrushMaskTexture(const std::wstring& path, std::vector<uint8_t>& outMask, int& dataWidth, int& dataHeight);`
- `void SetBrushMaskTexture(TerrainBrush* brush, const std::wstring& path);`

## Public Properties
- (none)
