#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>

//lua script 함수 등록 및 관리
//우선 behavior tree에서 사용
#include "BTHeader.h"
#include "Blackboard.h"

using ActionFunc = std::function<NodeStatus(float, BlackBoard&)>;

class FunctionRegistry
{
public :
	
	//등록
	static void RegisterCondition(const std::string& key, ConditionFunc func)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		Get()._conds[key] = std::move(func);
	}
	static void RegisterAction(const std::string& key, ActionFunc func)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		Get()._actions[key] = std::move(func);
	}

	//해제
	static void UnregisterCondition(const std::string& key)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		Get()._conds.erase(key);
	}
	static void UnregisterAction(const std::string& key)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		Get()._actions.erase(key);
	}

	//조회
	static ConditionFunc GetCondition(const std::string& key)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		auto it = Get()._conds.find(key);
		if (it != Get()._conds.end())
		{
			return it->second;
		}
		else {
			ConditionFunc{};
		}
	}
	
	static ActionFunc GetAction(const std::string& key)
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		auto it = Get()._actions.find(key);
		if (it != Get()._actions.end())
		{
			return it->second;
		}
		else {
			ActionFunc{};
		}
	}

	// 키 리스트
	static std::vector<std::string> ListConditions()
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		std::vector<std::string> keys;
		keys.reserve(Get()._conds.size());
		for (const auto& pair : Get()._conds)
		{
			keys.push_back(pair.first);
		}
		return keys;
	}
	static std::vector<std::string> ListActions()
	{
		std::unique_lock<std::mutex> guard(Get()._mtx);
		std::vector<std::string> keys;
		keys.reserve(Get()._actions.size());
		for (const auto& pair : Get()._actions)
		{
			keys.push_back(pair.first);
		}
		return keys;
	}


private : 
	std::unordered_map<std::string, ConditionFunc> _conds; //condition 함수 등록용
	std::unordered_map<std::string, ActionFunc> _actions; // action 함수 등록용
	std::mutex _mtx;
	static FunctionRegistry& Get()
	{
		static FunctionRegistry instance;
		return instance;
	}
};