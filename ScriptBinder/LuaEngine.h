#pragma once
#include <sol\sol.hpp>
#include <string>
#include <functional>
#include "Blackboard.h"
#include "BTHeader.h"


class Blackboard; // Forward declaration of Blackboard class
class LuaEngine
{
public :
	static LuaEngine& Get()
	{
		static LuaEngine instance;
		return instance;
	}
	void Initialize()
	{
		luaState.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math,sol::lib::table,sol::lib::string);
	}

	bool LoadScript(const std::string& code) 
	{
		sol::protected_function_result result = luaState.safe_script(code);
		if (!result.valid()) {
			sol::error err = result;
			std::cerr << "Error loading script: " << err.what() << std::endl;
			return false;
		}
		return true;
	}

	std::function<void(BlackBoard&)> GetFunction(const std::string& functionName)
	{
		sol::function func = luaState[functionName];
		if (!func.valid()) {
			throw std::runtime_error("Function not found: " + functionName);
		}
		return [func](BlackBoard& blackboard) {
			func(blackboard);
		};
	}

	std::function<bool(const BlackBoard&)> GetConditionFunction(const std::string& functionName)
	{
		sol::function func = luaState[functionName];
		if (!func.valid()) {
			throw std::runtime_error("Condition function not found: " + functionName);
		}
		return [func](const BlackBoard& blackboard) -> bool {
			return func(blackboard);
		};
	}

	std::function<NodeStatus(float, BlackBoard&)> GetActionFunction(const std::string& functionName)
	{
		sol::function func = luaState[functionName];
		if (!func.valid()) {
			throw std::runtime_error("Action function not found: " + functionName);
		}
		return [func](float deltaTime, BlackBoard& blackboard) ->NodeStatus{
			std::string result = func(deltaTime, blackboard);
			if (result == "success") {
				return NodeStatus::Success;
			}
			if (result == "failure")  {
				return NodeStatus::Failure;
			}
			return NodeStatus::Running;
		};
	}


private:
	LuaEngine() = default;
	LuaEngine(const LuaEngine&) = delete;
	LuaEngine& operator=(const LuaEngine&) = delete;
	LuaEngine(LuaEngine&&) = delete;
	LuaEngine& operator=(LuaEngine&&) = delete;

	sol::state luaState;
};