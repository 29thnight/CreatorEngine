#pragma once

#include "Core.Minimal.h"
#include "concurrent_queue.h"

#include <vector>

// Editor-only interaction layer for asset authoring. Runtime DataSystem owns
// neither the pending source paths nor the ImGui selector that classifies them.
class EditorAssetPresentation final
{
public:
	static EditorAssetPresentation& Get() noexcept;

	void Initialize();
	void Shutdown() noexcept;
	void QueueTextureImport(const file::path& source);
	void OpenPendingTextureImportSelector();

private:
	EditorAssetPresentation() = default;
	EditorAssetPresentation(const EditorAssetPresentation&) = delete;
	EditorAssetPresentation& operator=(const EditorAssetPresentation&) = delete;

	void RenderTextureImportSelector();

	concurrency::concurrent_queue<file::path> m_textureImportQueue;
	std::vector<file::path> m_pendingTexturePaths;
	int m_selectedTextureKind{};
	bool m_initialized{};
};
