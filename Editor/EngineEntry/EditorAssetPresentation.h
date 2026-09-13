#pragma once

#include "Core.Minimal.h"
#include "concurrent_queue.h"
#include "Windows/EditorWindowBody.h"
#include "EditorEntityIcons.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ImFont;
class Material;
class Texture;
struct EnhancedGizmoIconTextures;

// Editor-only interaction layer for asset authoring. Runtime DataSystem owns
// neither pending UI state nor picker/icon/font presentation resources.
class EditorAssetPresentation final
{
public:
	enum class FileType
	{
		Unknown,
		Model,
		Texture,
		MaterialTexture,
		TerrainTexture,
		Shader,
		CppScript,
		CSharpScript,
		Prefab,
		Sound,
		HDR,
		VolumeProfile,
		Font,
		End,
	};

	struct FilePresentation
	{
		FileType type{ FileType::Unknown };
		const char* type_icon{};
		Texture* type_image{}; // Borrowed from this presentation service.
	};

	static EditorAssetPresentation& Get() noexcept;

	void Initialize();
	void Shutdown() noexcept;

	void QueueTextureImport(const file::path& source);
	void OpenPendingTextureImportSelector();
	void OpenMaterialPicker();
	std::shared_ptr<Material> TakeSelectedMaterial() noexcept;

	FilePresentation ResolveFilePresentation(std::string_view extension) const;
	ImFont* GetSmallFont() const noexcept { return m_smallFont; }
	ImFont* GetExtraSmallFont() const noexcept { return m_extraSmallFont; }
    Texture* GetDirectoryIcon(bool expanded) const noexcept
    { return m_directoryIcons[expanded ? 1 : 0].get(); }
    Texture* GetEngineIcon() const noexcept { return m_engineIcon.get(); }
    Texture* GetEntityIcon(std::string_view preset) const noexcept
    { return m_entityIcons[editor::EntityIconIndex(preset)].get(); }
    Texture* GetProjectIcon() const noexcept { return m_projectIcon.get(); }
    Texture* GetFileIcon(FileType type) const noexcept
    {
        const auto index = static_cast<size_t>(type);
        return m_fileIcons[index < m_fileIcons.size() ? index : 0].get();
    }

private:
	EditorAssetPresentation() = default;
	EditorAssetPresentation(const EditorAssetPresentation&) = delete;
	EditorAssetPresentation& operator=(const EditorAssetPresentation&) = delete;

	void RenderTextureImportSelector();
	void RenderMaterialPicker();
	void LoadPresentationResources();
	void ReleasePresentationResources() noexcept;

	concurrency::concurrent_queue<file::path> m_textureImportQueue;
	std::vector<file::path> m_pendingTexturePaths;
	int m_selectedTextureKind{};

	std::unordered_map<std::string, FileType> m_extensionTypes;
	std::shared_ptr<const EnhancedGizmoIconTextures> m_gizmoIconTextures;
    std::array<std::shared_ptr<Texture>, 2> m_directoryIcons;
    std::shared_ptr<Texture> m_engineIcon;
    std::array<std::shared_ptr<Texture>, editor::EntityIconPresets.size()> m_entityIcons;
    std::shared_ptr<Texture> m_projectIcon;
    std::array<std::shared_ptr<Texture>, static_cast<size_t>(FileType::End)> m_fileIcons;
	std::shared_ptr<Material> m_previewedMaterial;
	std::shared_ptr<Material> m_selectedMaterial;
	ImFont* m_smallFont{};
	ImFont* m_extraSmallFont{};
	bool m_initialized{};

	// 두 창 본문의 수명(PHASE 21 W3). 둘 다 `this` 를 캡처하므로
	// 이 객체보다 오래 살면 안 된다 — 그 규약을 타입이 든다.
	::editor::windows::window_body_binding m_textureImportBody;
	::editor::windows::window_body_binding m_materialPickerBody;
};
