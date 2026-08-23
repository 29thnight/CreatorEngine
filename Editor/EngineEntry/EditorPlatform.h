#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

// Windows shell integration owned by the Editor host.
// Runtime/Core code must not depend on this service.
class EditorPlatform final
{
public:
	using OpenFileOverride =
		std::function<bool(const std::filesystem::path& filepath)>;

	static EditorPlatform& Get() noexcept;

	void SetOpenFileOverride(OpenFileOverride handler);
	bool OpenFile(const std::filesystem::path& filepath) const;
	bool RevealInFileExplorer(const std::filesystem::path& filepath) const;
	bool OpenUrl(std::string_view url) const;

private:
	EditorPlatform() = default;

	OpenFileOverride m_openFileOverride{};
};
