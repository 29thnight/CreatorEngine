#pragma once
#include <string>
#include <Windows.h>

inline std::string AnsiToUtf8(const std::string& ansiStr)
{
    // ANSI → Wide
    int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, &wide[0], wideLen);

    // Wide → UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Len, nullptr, nullptr);

    return utf8;
}

inline std::string WstringToString(const std::wstring& w) 
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

inline std::wstring StringToWstring(const std::string& s) 
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

// 모든 문자를 소문자로 변환
inline std::string ToLower(std::string_view str)
{
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

inline std::string Trim(std::string s)
{
    auto issp = [](int c) {return std::isspace((unsigned char)c); };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back())) s.pop_back();
    return s;
}

inline std::vector<std::string> Split(const std::string& s, char delim)
{
    std::vector<std::string> out; std::string cur; std::istringstream iss(s);
    while (std::getline(iss, cur, delim)) out.push_back(cur);
    return out;
}

inline std::vector<std::string> TokenizePreserveQuotes(const std::string& s)
{
	std::vector<std::string> toks; std::string cur; bool inq = false; char qc = '\0';
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if ((c == '\'' || c == '\"')) { if (!inq) { inq = true; qc = c; } else if (qc == c) { inq = false; } cur.push_back(c); continue; }
            if (!inq && std::isspace((unsigned char)c)) { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
            else cur.push_back(c);
        }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

inline std::string Unquote(std::string v)
{
    if (v.size() >= 2 && ((v.front() == '\"' && v.back() == '\"') || (v.front() == '\'' && v.back() == '\'')))
        return v.substr(1, v.size() - 2);
    return v;
}