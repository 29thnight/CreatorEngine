#include "EditorPlatform.h"

#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <utility>

EditorPlatform& EditorPlatform::Get() noexcept
{
	static EditorPlatform instance;
	return instance;
}

void EditorPlatform::SetOpenFileOverride(OpenFileOverride handler)
{
	m_openFileOverride = std::move(handler);
}

bool EditorPlatform::OpenFile(const std::filesystem::path& filepath) const
{
	if (m_openFileOverride && m_openFileOverride(filepath))
		return true;

	const HINSTANCE result = ShellExecuteW(
		nullptr, L"open", filepath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32)
		return true;

	MessageBoxW(nullptr, L"Failed to open file.", L"Error", MB_OK | MB_ICONERROR);
	return false;
}

bool EditorPlatform::RevealInFileExplorer(
	const std::filesystem::path& filepath) const
{
	const std::wstring arguments = L"/select,\"" + filepath.wstring() + L"\"";
	const HINSTANCE result = ShellExecuteW(
		nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr,
		SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) > 32)
		return true;

	MessageBoxW(nullptr, L"Failed to open file in Explorer.", L"Error",
		MB_OK | MB_ICONERROR);
	return false;
}

bool EditorPlatform::OpenUrl(std::string_view url) const
{
	const std::string nullTerminatedUrl(url);
	const HINSTANCE result = ShellExecuteA(
		nullptr, "open", nullTerminatedUrl.c_str(), nullptr, nullptr,
		SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}
