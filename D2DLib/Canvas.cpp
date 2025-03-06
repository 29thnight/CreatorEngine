#include "pch.h"
#include "Canvas.h"
#include "D2DBitmapScene.h"	
#include "../GameManager.h"

void Canvas::Update(float deltaTime)
{
	m_D2DBitSceneList.sort([](UIObject* a, UIObject* b)
	{
		return (*a) < (*b);
	});

	m_D2DBitSceneList.sort([](UIObject* a, UIObject* b)
	{
		return (*a) < (*b);
	});

	for (auto& obj : m_D2DBitSceneList)
	{
		if(obj->m_DestoryMark)
		{
			continue;
		}
		obj->Update(deltaTime * GameManagement->GetTimeScale());
	}
	for (auto& obj : m_D2DTextSceneList)
	{
		if (obj->m_DestoryMark)
		{
			continue;
		}
		obj->Update(deltaTime * GameManagement->GetTimeScale());
	}
/*
	UIEvent->Execute();
	UIControl->ControlSelectUI(deltaTime)*/;
}

void Canvas::DestroyUI()
{

}

void Canvas::Serialize(_inout json& out)
{
	json data;

	data["Name"] = m_Name;
	data["ObjList"] = json::array();
	for (auto& obj : m_D2DBitSceneList)
	{
		obj->Serialize(data);
	}

	data["TextList"] = json::array();
	for (auto& obj : m_D2DTextSceneList)
	{
		obj->Serialize(data);
	}

	out["Canvas"].push_back(data);
}

void Canvas::DeSerialize(_in json& in)
{
	m_Name = in["Name"].get<std::string>();
	auto& objList = in["ObjList"];
	for (auto& obj : objList)
	{
		UIObject* bitmapScene = new UIObject;
		bitmapScene->DeSerialize(obj);
		m_D2DBitSceneList.push_back(std::move(bitmapScene));
	}

	auto& textList = in["TextList"];
	for (auto& obj : textList)
	{
		TextUIObject* textScene = new TextUIObject;
		textScene->DeSerialize(obj);
		m_D2DTextSceneList.push_back(std::move(textScene));
	}
}
