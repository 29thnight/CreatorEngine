#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

#include <cstddef>
#include <span>

// Optional Host adapter for source-asset authoring requests made by runtime
// types. Player never installs handlers; missing handlers return null/false.
class AssetAuthoringPort final
{
public:
	using CreateMetaHandler = FileGuid (*)(const file::path& filepath);
	using WriteModelCacheHandler = bool (*)(const file::path& destination,
		std::span<const std::byte> bytes);
	using WriteEmbeddedTextureHandler = bool (*)(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height);
	using CopyTerrainTextureHandler = bool (*)(const file::path& source,
		const file::path& destination);

	static void Install(CreateMetaHandler handler) noexcept;
	static void Uninstall(CreateMetaHandler handler) noexcept;
	static FileGuid CreateMeta(const file::path& filepath) noexcept;

	static void InstallModelCacheWriter(WriteModelCacheHandler handler) noexcept;
	static void UninstallModelCacheWriter(WriteModelCacheHandler handler) noexcept;
	static bool WriteModelCache(const file::path& destination,
		std::span<const std::byte> bytes) noexcept;

	static void InstallEmbeddedTextureWriter(
		WriteEmbeddedTextureHandler handler) noexcept;
	static void UninstallEmbeddedTextureWriter(
		WriteEmbeddedTextureHandler handler) noexcept;
	static bool WriteEmbeddedTexture(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height) noexcept;

	static void InstallTerrainTextureCopier(
		CopyTerrainTextureHandler handler) noexcept;
	static void UninstallTerrainTextureCopier(
		CopyTerrainTextureHandler handler) noexcept;
	static bool CopyTerrainTexture(const file::path& source,
		const file::path& destination) noexcept;

	static bool IsInstalled() noexcept;
};
