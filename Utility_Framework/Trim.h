#pragma once
#include <string>

// Trim from the start (left)
std::string ltrim(const std::string& s) {
	std::string result = s;
	result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
		return !std::isspace(ch);
		}));
	return result;
}

// Trim from the end (right)
std::string rtrim(const std::string& s) {
	std::string result = s;
	result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
		}).base(), result.end());
	return result;
}

// Trim from both ends
std::string trim(const std::string& s) {
	return ltrim(rtrim(s));
}
