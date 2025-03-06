#pragma once
#include <list>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifndef _in
#define _in
#define _inout
#endif

struct UIObject;
struct TextUIObject;

namespace DirectX11
{
	class D2DRenderer;
}

struct AABB
{
	float x, y, width, height;

	bool Contains(float x, float y)
	{
		if (width == 0 || height == 0)
		{
			return false;
		}

		return x >= this->x && x <= this->x + width && y >= this->y && y <= this->y + height;
	}
};

struct D2DBitmapScene;

class Canvas
{
public:
	Canvas() = default;
	~Canvas()
	{
		for (auto& obj : m_D2DBitSceneList)
		{
			delete obj;
		}

		for (auto& obj : m_D2DTextSceneList)
		{
			delete obj;
		}

		m_D2DBitSceneList.clear();
		m_D2DTextSceneList.clear();
	}
	std::list<UIObject*>& getObjList() { return m_D2DBitSceneList; }
	std::list<TextUIObject*>& getTextList() { return m_D2DTextSceneList; }

	void Update(float deltaTime);

	std::string GetName() { return m_Name; }

	inline void SetName(const std::string& name) { m_Name = name; }
	void DestroyUI();
	void Serialize(_inout json& out);
	void DeSerialize(_in json& in);

private:
	friend class DirectX11::D2DRenderer;
	std::string m_Name;
	std::list<UIObject*>		m_D2DBitSceneList;
	std::list<TextUIObject*>    m_D2DTextSceneList;
};


