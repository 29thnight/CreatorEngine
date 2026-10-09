#pragma once

#include "Core.Minimal.h"
#include "Ownership.h"
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
		SceneRenderProfile,
		Font,
		End,
	};

	struct FilePresentation
	{
		FileType type{ FileType::Unknown };
		const char* type_icon{};
		const Texture* type_image{}; // Borrowed from this presentation service.
	};

	static EditorAssetPresentation& Get() noexcept;

	void Initialize();
	void Shutdown() noexcept;

	void QueueTextureImport(const file::path& source);
	void OpenPendingTextureImportSelector();
	void OpenMaterialPicker();
	own::shared_owner<const Material> TakeSelectedMaterial() noexcept;

	FilePresentation ResolveFilePresentation(std::string_view extension) const;
	ImFont* GetSmallFont() const noexcept { return m_smallFont; }
	ImFont* GetExtraSmallFont() const noexcept { return m_extraSmallFont; }
    const Texture* GetDirectoryIcon(bool expanded) const noexcept
    {
        const auto& icon = m_directoryIcons[expanded ? 1 : 0];
        return icon ? &*icon.borrow() : nullptr;
    }
    const Texture* GetEngineIcon() const noexcept { return m_engineIcon ? &*m_engineIcon.borrow() : nullptr; }
    const Texture* GetEntityIcon(std::string_view preset) const noexcept
    {
        const auto& icon = m_entityIcons[editor::EntityIconIndex(preset)];
        return icon ? &*icon.borrow() : nullptr;
    }
    const Texture* GetProjectIcon() const noexcept { return m_projectIcon ? &*m_projectIcon.borrow() : nullptr; }
    const Texture* GetFileIcon(FileType type) const noexcept
    {
        const auto index = static_cast<size_t>(type);
        const auto& icon = m_fileIcons[index < m_fileIcons.size() ? index : 0];
        return icon ? &*icon.borrow() : nullptr;
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
    std::array<own::shared_owner<const Texture>, 2> m_directoryIcons;
    own::shared_owner<const Texture> m_engineIcon;
    std::array<own::shared_owner<const Texture>, editor::EntityIconPresets.size()> m_entityIcons;
    own::shared_owner<const Texture> m_projectIcon;
    std::array<own::shared_owner<const Texture>, static_cast<size_t>(FileType::End)> m_fileIcons;
	own::shared_owner<const Material> m_previewedMaterial;
	own::shared_owner<const Material> m_selectedMaterial;
	ImFont* m_smallFont{};
	ImFont* m_extraSmallFont{};
	bool m_initialized{};

	// 두 창 본문의 수명(PHASE 21 W3). 둘 다 `this` 를 캡처하므로
	// 이 객체보다 오래 살면 안 된다 — 그 규약을 타입이 든다.
	::editor::windows::window_body_binding m_textureImportBody;
	::editor::windows::window_body_binding m_materialPickerBody;
};
