#include "FontManager.h"
#include "PathFinder.h"

void FontManager::Initialize()
{
	HRESULT hresult = S_OK;
	IDWriteTextFormat* _pDefaultFont{ nullptr };

	hresult = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory5),
		reinterpret_cast<IUnknown**>(m_DWriteFactory.ReleaseAndGetAddressOf())
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create DWrite factory");
		throw std::exception("Failed to create DWrite factory");
	}

	hresult = m_DWriteFactory->CreateFontSetBuilder(&m_FontSetBuilder);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create font set builder");
		throw std::exception("Failed to create font set builder");
	}

	hresult = m_DWriteFactory->CreateTextFormat(
		L"讣篮绊雕",
		nullptr,
		DWRITE_FONT_WEIGHT_REGULAR,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		20.0f,
		L"en-us",
		&_pDefaultFont
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create default font");
		throw std::exception("Failed to create default font");
	}
	else
	{
		m_Fonts["DefaultFont"] = _pDefaultFont;
	}

	m_LoadThread = std::thread(&FontManager::MonitorFiles, this);
	m_LoadThread.detach();
}

void FontManager::LoadFonts()
{
	try
	{
		file::path path = PathFinder::Relative("Fonts\\");
		for (auto& dir : file::recursive_directory_iterator(path))
		{
			if (dir.is_directory())
				continue;
			if (dir.path().extension() == ".ttf" || dir.path().extension() == ".otf")
			{
				LoadFontFile(dir.path(), dir.path().filename(), 20.0f);
			}
		}
	}
	catch (const file::filesystem_error& e)
	{
		Log::Warning("Could not load fonts" + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		Log::Warning("Error" + std::string(e.what()));
	}
}

void FontManager::MonitorFiles()
{
	while (true)
	{
		uint32 uiFileCount = 0;
		try
		{
			file::path path = PathFinder::Relative("Fonts\\");
			for (auto& dir : file::recursive_directory_iterator(path))
			{
				if (dir.is_directory())
					continue;
				if (dir.path().extension() == ".ttf" || dir.path().extension() == ".otf")
				{
					uiFileCount++;
				}
			}
		}
		catch (const file::filesystem_error& e)
		{
			Log::Warning("Could not load fonts" + std::string(e.what()));
		}
		catch (const std::exception& e)
		{
			Log::Warning("Error" + std::string(e.what()));
		}

		if (uiFileCount != m_FontCount)
		{
			LoadFonts();
			m_FontCount = uiFileCount;
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

HRESULT FontManager::LoadFontFile(const file::path& fontFilePath, const file::path& fontName, float fontSize)
{
	HRESULT hresult = S_OK;

	IDWriteFontFile* _pFontFile{ nullptr };
	IDWriteFontSet* _pFontSet{ nullptr };
	IDWriteFontCollection1* _pFontCollection{ nullptr };
	IDWriteFontFamily* _pFontFamily{ nullptr };
	IDWriteLocalizedStrings* _pFontFamilyNames{ nullptr };
	IDWriteFontSetBuilder1* _pFontSetBuilder{ nullptr };

	// 货肺款 FontSetBuilder 积己
	hresult = m_DWriteFactory->CreateFontSetBuilder(&_pFontSetBuilder);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create font set builder");
		return hresult;
	}

	hresult = m_DWriteFactory->CreateFontFileReference(
		fontFilePath.c_str(),
		nullptr,
		&_pFontFile
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create font file reference");
		return hresult;
	}

	hresult = _pFontSetBuilder->AddFontFile(_pFontFile);
	if (FAILED(hresult))
	{
		Log::Error("Failed to add font file");
		return hresult;
	}

	BOOL isSupported;
	DWRITE_FONT_FILE_TYPE fileType;
	UINT32 numberOfFonts;
	hresult = _pFontFile->Analyze(&isSupported, &fileType, nullptr, &numberOfFonts);
	if (FAILED(hresult))
	{
		Log::Error("Failed to analyze font file");
		return hresult;
	}

	hresult = _pFontSetBuilder->CreateFontSet(&_pFontSet);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create font set");
		return hresult;
	}

	hresult = m_DWriteFactory->CreateFontCollectionFromFontSet(
		_pFontSet,
		&_pFontCollection
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create font collection from font set");
		return hresult;
	}

	hresult = _pFontCollection->GetFontFamily(
		0,
		&_pFontFamily
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to get font family");
		return hresult;
	}

	hresult = _pFontFamily->GetFamilyNames(&_pFontFamilyNames);
	if (FAILED(hresult))
	{
		Log::Error("Failed to get family names");
		return hresult;
	}

	WCHAR familyName[MAX_PATH];
	hresult = _pFontFamilyNames->GetString(0, familyName, MAX_PATH);
	if (FAILED(hresult))
	{
		Log::Error("Failed to get string");
		return hresult;
	}

	IDWriteTextFormat* _pNewFont{ nullptr };
	hresult = AddFont(familyName, fontSize, _pFontCollection, &_pNewFont);
	if (FAILED(hresult))
	{
		Log::Error("Failed to add font");
		return hresult;
	}
	else
	{
		m_Fonts[fontName.string()] = _pNewFont;
	}

	_pFontFile->Release();
	_pFontSet->Release();
	_pFontCollection->Release();
	_pFontFamily->Release();

	Log::Info("Loaded font: " + fontName.string());

	return hresult;
}

IDWriteTextFormat* FontManager::GetFont(const std::string& fontName)
{
	return m_Fonts[fontName].Get();
}

std::vector<std::string> FontManager::GetFontKeyList()
{
	std::vector<std::string> keys;
	for (auto& font : m_Fonts)
	{
		keys.push_back(font.first);
	}
	return keys;
}

HRESULT FontManager::AddFont(const file::path& fontName, float fontSize, IDWriteFontCollection1* pFontCollection, IDWriteTextFormat** ppTextFormat)
{
	HRESULT hresult = S_OK;

	hresult = m_DWriteFactory->CreateTextFormat(
		fontName.c_str(),
		pFontCollection,
		DWRITE_FONT_WEIGHT_REGULAR,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us",
		ppTextFormat
	);
	if (FAILED(hresult))
	{
		Log::Error("Failed to create text format");
		return hresult;
	}

	return hresult;
}
