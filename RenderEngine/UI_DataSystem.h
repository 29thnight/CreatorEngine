#pragma once
#include "DeviceResources.h"
#include "Core.Minimal.h"
#include "PathFinder.h"
#include "Texture.h"

namespace DirectX11
{
	class UIDataSystem : public Singleton<UIDataSystem>
	{
	private:
		friend class Singleton;
	private:
		UIDataSystem() = default;
		~UIDataSystem();
	public:
		void Initialize(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);
		void Finalize();
		void RenderForEditer();
		void LoadBitmapFormFile();
		void LoadAnimationFormFile();
		void MonitorFiles();

		ID2D1Bitmap* GetBitmap(const std::string& key);
		std::vector<ID2D1Bitmap*> GetAnimation(const std::string& key);
		ID2D1Bitmap* GetDragDropBitmap() { return dragDropBitmap; }
		std::vector<ID2D1Bitmap*> GetDragDropBitmaps() { return dragDropBitmaps; }
		std::string GetDragDropKey() { return dragDropKey; }
		void ClearDragDropBitmap() { dragDropBitmap = nullptr; }
		void ClearDragDropBitmaps() { dragDropBitmaps.clear(); }

		bool IsLoading() const { return m_IsLoading; }

	private:
		std::thread m_LoadThread;
		std::shared_ptr<DirectX11::DeviceResources> m_DeviceResources;
		std::unordered_map<std::string, ID2D1Bitmap*> m_BitmapMap;
		std::unordered_map<std::string, std::vector<ID2D1Bitmap*>> m_AnimationMap;
		std::atomic_bool m_IsLoading = false;
		ID2D1Bitmap* dragDropBitmap{};
		std::vector<ID2D1Bitmap*> dragDropBitmaps{};
		std::string dragDropKey{};
		uint32 currFileCount = 0;
		uint32 currAnim2DFileCount = 0;
		Texture* icon{};
	};
}

static inline auto& UISystem = DirectX11::UIDataSystem::GetInstance();