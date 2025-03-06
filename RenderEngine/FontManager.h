#pragma once
#include "Core.Minimal.h"

class FontManager : public Singleton<FontManager>
{
private:
	friend class Singleton;

public:
	FontManager() = default;
	~FontManager() = default;

public:
	void Initialize();
	void LoadFonts();
	void MonitorFiles();
	HRESULT LoadFontFile(const file::path& fontFilePath, const file::path& fontName, float fontSize);
	IDWriteTextFormat* GetFont(const std::string& fontName);
	IDWriteFactory5* GetFactory() { return m_DWriteFactory.Get(); }
	std::vector<std::string> GetFontKeyList();

private:
	HRESULT AddFont(const file::path& fontName, float fontSize, IDWriteFontCollection1* pFontCollection, IDWriteTextFormat** ppTextFormat);

	std::unordered_map<std::string, ComPtr<IDWriteTextFormat>> m_Fonts;
	std::thread m_LoadThread;
	uint32 m_FontCount{ 0 };
	ComPtr<IDWriteFactory5> m_DWriteFactory{};
	ComPtr<IDWriteFontSetBuilder1> m_FontSetBuilder{};
};

inline static auto& FontSystem = FontManager::GetInstance();