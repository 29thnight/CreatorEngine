#include "UI_DataSystem.h"
#include "ImGuiRegister.h"
#include "Core.Memory.hpp"
#include "DataSystem.h"

DirectX11::UIDataSystem::~UIDataSystem()
{
	Finalize();
}

void DirectX11::UIDataSystem::Initialize(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources)
{
	m_DeviceResources = deviceResources;
	m_LoadThread = std::thread(&UIDataSystem::MonitorFiles, this);
	m_LoadThread.detach();

	icon = Texture::LoadFormPath(PathFinder::Relative("Icon\\Model.png"));

	RenderForEditer();
}

void DirectX11::UIDataSystem::Finalize()
{
	for (auto& [key, bitmap] : m_BitmapMap)
	{
		decltype(auto) bitmap = m_BitmapMap[key];
		Memory::SafeDelete(bitmap);
	}

	for (auto& [key, animationClip] : m_AnimationMap)
	{
		for (auto& bitmap : animationClip)
		{
			Memory::SafeDelete(bitmap);
		}
	}

	m_BitmapMap.clear();
	m_AnimationMap.clear();
}

void DirectX11::UIDataSystem::RenderForEditer()
{
	static std::string selectedBitmap{};

	ImGui::ContextRegister("Bitmaps", [&]()
	{
		constexpr float tileSize = 128.0f;
		constexpr float padding = 10.0f;
		constexpr int tilesPerRow = 4;

		int count = 0;
		for (const auto& [key, bitmap] : m_BitmapMap)
		{
			if (!AssetsSystem->filter.PassFilter(key.c_str()))
				continue;

			ImGui::BeginGroup();
			if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon->m_pSRV, ImVec2(tileSize, tileSize)))
			{
				std::cout << "Selected Bitmap: " << key << std::endl;
				selectedBitmap = key;
			}

			if (ImGui::IsItemHovered())
			{
				selectedBitmap = key;
			}
			if (selectedBitmap == key)
			{
				ImGui::TextWrapped("[Selected]");
			}
			ImGui::TextWrapped("%s", key.c_str());
			ImGui::EndGroup();
			count++;
			if (count % tilesPerRow != 0)
			{
				ImGui::SameLine(0.0f, padding);
			}

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("Drag to Scene : %s", selectedBitmap.c_str());
				std::cout << selectedBitmap.c_str() << std::endl;
				dragDropBitmap = m_BitmapMap[selectedBitmap];
				dragDropKey = selectedBitmap;
				ImGui::EndDragDropSource();
			}
		}
	});

	ImGui::ContextRegister("Animations", [&]()
	{
		constexpr float tileSize = 128.0f;
		constexpr float padding = 10.0f;
		constexpr int tilesPerRow = 4;
		int count = 0;
		for (const auto& [key, animationClip] : m_AnimationMap)
		{
			if (!AssetsSystem->filter.PassFilter(key.c_str()))
				continue;

			ImGui::BeginGroup();
			if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon->m_pSRV, ImVec2(tileSize, tileSize)))
			{
				std::cout << "Selected Bitmap: " << key << std::endl;
				selectedBitmap = key;
			}
			if (ImGui::IsItemHovered())
			{
				selectedBitmap = key;
			}
			if (selectedBitmap == key)
			{
				ImGui::TextWrapped("[Selected]");
			}
			ImGui::TextWrapped("%s", key.c_str());
			ImGui::EndGroup();
			count++;
			if (count % tilesPerRow != 0)
			{
				ImGui::SameLine(0.0f, padding);
			}
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("Drag to Scene : %s", selectedBitmap.c_str());
				std::cout << selectedBitmap.c_str() << std::endl;
				dragDropBitmaps = m_AnimationMap[selectedBitmap];
				dragDropKey = selectedBitmap;
				ImGui::EndDragDropSource();
			}
		}
	});
}

void DirectX11::UIDataSystem::LoadBitmapFormFile()
{
	file::path path = PathFinder::Relative("Textures\\");
	m_IsLoading = true;
	for (auto& dir : file::recursive_directory_iterator(path))
	{
		if (dir.is_directory())
			continue;
		if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
		{
			IWICBitmapDecoder* pDecoder = nullptr;
			IWICBitmapFrameDecode* pSource = nullptr;
			IWICFormatConverter* pConverter = nullptr;

			HRESULT hr = m_DeviceResources->GetWicImagingFactory()->CreateDecoderFromFilename(
				dir.path().c_str(),
				NULL,
				GENERIC_READ,
				WICDecodeMetadataCacheOnLoad,
				&pDecoder
			);

			hr = pDecoder->GetFrame(0, &pSource);
			hr = m_DeviceResources->GetWicImagingFactory()->CreateFormatConverter(&pConverter);
			hr = pConverter->Initialize(
				pSource,
				GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone,
				NULL,
				0.f,
				WICBitmapPaletteTypeMedianCut
			);
			hr = m_DeviceResources->GetD2DTarget()->CreateBitmapFromWicBitmap(
				pConverter,
				NULL,
				&m_BitmapMap[dir.path().filename().string()]
			);
		}
	}
	m_IsLoading = false;
}

void DirectX11::UIDataSystem::LoadAnimationFormFile()
{
	file::path path = PathFinder::Relative("2DAnimation\\");
	std::string key{};
	m_IsLoading = true;
	for (auto& dir : file::recursive_directory_iterator(path))
	{
		if (dir.is_directory())
		{
			key = dir.path().filename().string();
			m_AnimationMap[key].reserve(50);
			continue;
		}
		if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
		{
			IWICBitmapDecoder* pDecoder = nullptr;
			IWICBitmapFrameDecode* pSource = nullptr;
			IWICFormatConverter* pConverter = nullptr;
			HRESULT hr = m_DeviceResources->GetWicImagingFactory()->CreateDecoderFromFilename(
				dir.path().c_str(),
				NULL,
				GENERIC_READ,
				WICDecodeMetadataCacheOnLoad,
				&pDecoder
			);

			hr = pDecoder->GetFrame(0, &pSource);
			hr = m_DeviceResources->GetWicImagingFactory()->CreateFormatConverter(&pConverter);
			hr = pConverter->Initialize(
				pSource,
				GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone,
				NULL,
				0.f,
				WICBitmapPaletteTypeMedianCut
			);

			ID2D1Bitmap* bitmap{};
			hr = m_DeviceResources->GetD2DTarget()->CreateBitmapFromWicBitmap(
				pConverter,
				NULL,
				&bitmap
			);
			m_AnimationMap[key].push_back(bitmap);
		}
	}
	m_IsLoading = false;
}

std::vector<ID2D1Bitmap*> DirectX11::UIDataSystem::GetAnimation(const std::string& key)
{
	return m_AnimationMap[key];
}

void DirectX11::UIDataSystem::MonitorFiles()
{
	while (true)
	{
		uint32 uiFileCount = 0;
		uint32 anim2DFileCount = 0;
		try
		{
			file::path path = PathFinder::Relative("Textures\\");
			for (auto& dir : file::recursive_directory_iterator(path))
			{
				if (dir.is_directory())
					continue;
				if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
				{
					uiFileCount++;
				}
			}
		}
		catch (const file::filesystem_error& e)
		{
			Log::Warning("Could not load textures" + std::string(e.what()));
		}
		catch (const std::exception& e)
		{
			Log::Warning("Error" + std::string(e.what()));
		}

		if (uiFileCount != currFileCount)
		{
			std::cout << "Texture count changed" << std::endl;
			LoadBitmapFormFile();
			currFileCount = uiFileCount;
		}

		try
		{
			file::path path = PathFinder::Relative("2DAnimation\\");
			for (auto& dir : file::recursive_directory_iterator(path))
			{
				if (dir.is_directory())
					continue;
				if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
				{
					anim2DFileCount++;
				}
			}
		}
		catch (const file::filesystem_error& e)
		{
			Log::Warning("Could not load textures" + std::string(e.what()));
		}
		catch (const std::exception& e)
		{
			Log::Warning("Error" + std::string(e.what()));
		}

		if (anim2DFileCount != currAnim2DFileCount)
		{
			std::cout << "2D Animation count changed" << std::endl;
			LoadAnimationFormFile();
			currAnim2DFileCount = anim2DFileCount;
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

ID2D1Bitmap* DirectX11::UIDataSystem::GetBitmap(const std::string& key)
{
	if (m_BitmapMap.find(key) != m_BitmapMap.end())
	{
		return m_BitmapMap[key];
	}
	return nullptr;
}
