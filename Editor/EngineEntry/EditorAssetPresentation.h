#pragma once

#include "Core.Minimal.h"
#include "concurrent_queue.h"

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
		// ImTextureID는 backend descriptor라 캐시하지 않는다. 그리는 프레임에
		// EditorImGuiTexture가 이 안정 Texture 신원을 해석한다.
		Texture* icon{};
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

	std::array<std::shared_ptr<Texture>, static_cast<size_t>(FileType::End)> m_fileIcons{};
	std::unordered_map<std::string, FileType> m_extensionTypes;
	std::shared_ptr<const EnhancedGizmoIconTextures> m_gizmoIconTextures;
	std::shared_ptr<Material> m_previewedMaterial;
	std::shared_ptr<Material> m_selectedMaterial;
	ImFont* m_smallFont{};
	ImFont* m_extraSmallFont{};
	bool m_initialized{};
};
