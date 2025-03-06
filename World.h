#pragma once
#include "Utility_Framework/IWorld.h"
#include "Object.h"
#include "RenderEngine/Scene.h"
#include "RenderEngine/Camera.h"
#include "D2DLib/Canvas.h"
#include "D2DLib/D2DBitmapScene.h"
#include "D2DLib/D2DRenderer.h"
#include "Utility_Framework/Core.Minimal.h"

class Object;
class World : public IWorld
{
public:
	World() = default;
	virtual ~World() = default;
	virtual void Initialize() override;
	void Finalize();
	virtual void FixedUpdate(float fixedTick) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;
	virtual void Destroy() override;

	template<typename T = Object>
	T* CreateObject(const std::string& name)
	{
		_objects.emplace_back(std::make_unique<T>(name));
		_objectIds[name] = _objects.size() - 1;
		return static_cast<T*> (_objects.back().get());
	}

	Object* GetNameObject(const std::string& name);
	void CreatePrefab(const std::string& type, const std::string& prefabName, Object* targetObject);
	void LoadPrefab(const std::string& type, const std::string& prefabName, const std::string& name);
	void DestroyObject(Object* object);
	void SetScene(Scene* scene) { _scene = scene; }
	Scene* GetScene() { return _scene; }
	void SetCamera(Camera* camera) { _camera = camera; }
	void AllDestroy();

	bool CreateCanvas();
	bool CreateUIObject(const std::string_view& name);
	bool CreateTextUIObject(const std::string_view& name);
	bool DestroyUIObject(const std::string_view& name);
	bool DestroyTextUIObject(const std::string_view& name);
	bool IsCreateBT() const { return _isCreateBt; }
	bool IsCreateCanvas() const { return _isCreateCanvas; }
	bool IsEditGrid() const { return _isGridEdit; }

	Canvas* GetCurCanvas() { return _currentCanvas; }
	void SetCurCanvas(Canvas* _target) { _currentCanvas = _target; }

	void PreObjectLoad();
	void PreComponentLoad();
	bool OutputJson(const std::string& path);
	bool LoadJson(const std::string& path);
	void LoadCanvas(const std::string& path);
	void Serialize(_inout json& out);
	bool Deserialize(_inout json& out);
	void UnRegisterImGui();

	void SpawnObject(Object* obj) { _temporaryObjects.push_back(obj); }
	void DespawnObject(Object* obj) { 
		auto it = std::find(_temporaryObjects.begin(), _temporaryObjects.end(), obj);
		if (it != _temporaryObjects.end()) {
			_temporaryObjects.erase(it);
		}
	}
	

	void SetGrid(int x, int y, std::vector<std::vector<int>>grid ) { _worldX = x; _worldY = y; _worldGrid = grid; }
	void GetGrid(int& x, int& y, std::vector<std::vector<int>>& grid) { x = _worldX; y = _worldY; grid = _worldGrid; }

	int tempObjectCount() { return _temporaryObjects.size(); }

	int ObjectCount() { return _objects.size(); }
private:
	std::unordered_map<std::string, size_t> _objectIds;
	std::vector<std::unique_ptr<Object>> _objects;
	std::vector<Object*> _temporaryObjects;
	std::string selectedGameObject{};
	std::string selectedUIObject{};
	std::string selectedTextUIObject{};
	Canvas* _currentCanvas;
	Scene* _scene;
	Camera* _camera;

	std::shared_ptr<Scene> _scenePtr;
	bool bgm1Flag{ false };
	bool _isCreateBt{ false };
	bool _isCreateCanvas{ false };
	bool _isGridEdit{ false };

	
	//해당 월드의 그리드 정보
	int _worldX = 15;
	int _worldY = 15;
	std::vector<std::vector<int>> _worldGrid = std::vector<std::vector<int>>(_worldX, std::vector<int>(_worldY, 0));


};


