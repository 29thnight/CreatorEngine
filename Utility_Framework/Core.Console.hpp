#pragma once
#include <cstdio>
//#include <print>

//Console 클래스 -> 사용법은 C#의 Console 클래스와 유사
//class CoreConsole
//{
//public:
//	template<typename... T>
//	void Write(std::format_string<T...> fmt, T&&... args)
//	{
//        std::print(fmt, static_cast<T&&>(args)...);
//	}
//
//	template<typename... T>
//	void Write(FILE* file, std::format_string<T...> fmt, T&&... args)
//	{
//        std::print(file, fmt, static_cast<T&&>(args)...);
//	}
//
//	template<typename... T>
//	void WriteLine(FILE* file, std::format_string<T...> fmt, T&&... args)
//	{
//        std::println(file, fmt, static_cast<T&&>(args)...);
//	}
//
//	template<typename... T>
//	void WriteLine(std::format_string<T...> fmt, T&&... args)
//	{
//		return WriteLine(stdout, fmt, static_cast<T&&>(args)...);
//	}
//
//};
//
//inline static CoreConsole Console;
