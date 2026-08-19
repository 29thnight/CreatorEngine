#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

enum class BlackBoardType
{
	None = 0,
	Bool,
	Int,
	Float,
	String,
	Vector2,
	Vector3,
	Vector4,
	Entity, // 게임 오브젝트 타입
	Transform,  // 트랜스폼 타입
};

inline constexpr std::string BlackBoardTypeToString(BlackBoardType type)
{
	switch (type)
	{
	case BlackBoardType::None:     return "None";
	case BlackBoardType::Bool:     return "Bool";
	case BlackBoardType::Int:      return "Int";
	case BlackBoardType::Float:    return "Float";
	case BlackBoardType::String:   return "String";
	case BlackBoardType::Vector2:  return "Vector2";
	case BlackBoardType::Vector3:  return "Vector3";
	case BlackBoardType::Vector4:  return "Vector4";
	case BlackBoardType::Entity: return "Entity";
	case BlackBoardType::Transform:  return "Transform";
	default:                       return "Unknown";
	}
}