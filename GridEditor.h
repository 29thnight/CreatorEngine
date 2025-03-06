#pragma once
#include "Utility_Framework/Core.Minimal.h"
#include "World.h"

class World;
class GridEditor : public Singleton<GridEditor>
{
public:
	bool show = false;
	GridEditor() = default;
	~GridEditor() = default;

	void Initialize(World* world)
	{
		_world = world;
		_world->GetGrid(gridX, gridY, _worldGrid);
		x = gridX;
		y = gridY;
	}
	void ShowGridEditor();

private:
	void SetEditGrid();



private:
	World* _world;
	
	int x = 1;
	int y = 1;

	int gridX = 15;
	int gridY = 15;
	std::vector<std::vector<int>> _worldGrid;
};

static inline auto& GridEditorSystem = GridEditor::GetInstance();
