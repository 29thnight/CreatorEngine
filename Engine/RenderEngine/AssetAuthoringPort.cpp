#include "Interfaces/AssetAuthoringPort.h"

#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace
{
    // Stateless callbacks point into the authoring executable, whose lifetime
    // exceeds all accepted work. No database lock is required around decoding.
    std::atomic<AssetAuthoringPort::SourceTextureLoader> g_sourceTextureLoader{};
    std::atomic<AssetAuthoringPort::SourceTextureMipGenerator> g_sourceTextureMipGenerator{};
    std::atomic<AssetAuthoringPort::SourceTextureRgbaDecoder> g_sourceTextureRgbaDecoder{};
    std::atomic<AssetAuthoringPort::TerrainSourceImageReader> g_terrainSourceImageReader{};
	std::atomic<AssetAuthoringPort::CreateMetaHandler> g_createMetaHandler{};
	std::atomic<AssetAuthoringPort::WriteTextAssetWithMetaHandler>
		g_writeTextAssetWithMetaHandler{};
	std::atomic<AssetAuthoringPort::WriteModelCacheHandler> g_writeModelCacheHandler{};
	std::shared_mutex g_modelRecoveryMutex;
	AssetAuthoringPort::RecoverModelHandler g_modelRecoveryHandler{};
	std::atomic<AssetAuthoringPort::WriteEmbeddedTextureHandler>
		g_writeEmbeddedTextureHandler{};
	std::atomic<AssetAuthoringPort::WriteTerrainHandler> g_writeTerrainHandler{};
	std::atomic<AssetAuthoringPort::WriteFoliageHandler> g_writeFoliageHandler{};
	std::atomic<AssetAuthoringPort::WriteBlackBoardHandler>
		g_writeBlackBoardHandler{};
	std::atomic<AssetAuthoringPort::WriteCollisionMatrixHandler>
		g_writeCollisionMatrixHandler{};
	std::atomic<AssetAuthoringPort::WriteTagManagerHandler>
		g_writeTagManagerHandler{};
	std::atomic<AssetAuthoringPort::WriteLayerSettingsHandler>
		g_writeLayerSettingsHandler{};
}

void AssetAuthoringPort::Install(CreateMetaHandler handler) noexcept
{
	g_createMetaHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::Uninstall(CreateMetaHandler handler) noexcept
{
	g_createMetaHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

FileGuid AssetAuthoringPort::CreateMeta(const file::path& filepath,
	const FileGuid& preferredGuid) noexcept
{
	const CreateMetaHandler handler =
		g_createMetaHandler.load(std::memory_order_acquire);
	return handler ? handler(filepath, preferredGuid) : FileGuid{};
}

void AssetAuthoringPort::InstallTextAssetWriter(
	WriteTextAssetWithMetaHandler handler) noexcept
{
	g_writeTextAssetWithMetaHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallTextAssetWriter(
	WriteTextAssetWithMetaHandler handler) noexcept
{
	g_writeTextAssetWithMetaHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

FileGuid AssetAuthoringPort::WriteTextAssetWithMeta(
	const file::path& destination, std::string_view payload,
	const FileGuid& preferredGuid) noexcept
{
	const WriteTextAssetWithMetaHandler handler =
		g_writeTextAssetWithMetaHandler.load(std::memory_order_acquire);
	if (!handler) return {};
	try
	{
		return handler(destination, payload, preferredGuid);
	}
	catch (...)
	{
		return {};
	}
}

void AssetAuthoringPort::InstallModelRecovery(RecoverModelHandler handler) noexcept
{
	std::unique_lock lock(g_modelRecoveryMutex);
	g_modelRecoveryHandler = handler;
}

void AssetAuthoringPort::UninstallModelRecovery(RecoverModelHandler handler) noexcept
{
	// Wait for in-flight worker loads before the Editor-owned database is destroyed.
	std::unique_lock lock(g_modelRecoveryMutex);
	if (g_modelRecoveryHandler == handler) g_modelRecoveryHandler = nullptr;
}

bool AssetAuthoringPort::RecoverModel(const file::path& source, FileGuid expectedId) noexcept
{
	std::shared_lock lock(g_modelRecoveryMutex);
	if (!g_modelRecoveryHandler) return false;
	try { return g_modelRecoveryHandler(source, expectedId); }
	catch (...) { return false; }
}

void AssetAuthoringPort::InstallModelCacheWriter(
	WriteModelCacheHandler handler) noexcept
{
	g_writeModelCacheHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallModelCacheWriter(
	WriteModelCacheHandler handler) noexcept
{
	g_writeModelCacheHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteModelCache(const file::path& destination,
	std::span<const std::byte> bytes) noexcept
{
	const WriteModelCacheHandler handler =
		g_writeModelCacheHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(destination, bytes);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallEmbeddedTextureWriter(
	WriteEmbeddedTextureHandler handler) noexcept
{
	g_writeEmbeddedTextureHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallEmbeddedTextureWriter(
	WriteEmbeddedTextureHandler handler) noexcept
{
	g_writeEmbeddedTextureHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteEmbeddedTexture(const file::path& destination,
	std::span<const std::byte> bytes, uint32 width, uint32 height) noexcept
{
	const WriteEmbeddedTextureHandler handler =
		g_writeEmbeddedTextureHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(destination, bytes, width, height);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallTerrainWriter(
	WriteTerrainHandler handler) noexcept
{
	g_writeTerrainHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallTerrainWriter(
	WriteTerrainHandler handler) noexcept
{
	g_writeTerrainHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteTerrain(const TerrainAuthoringRequest& request,
	TerrainAuthoringResult& result) noexcept
{
	const WriteTerrainHandler handler =
		g_writeTerrainHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request, result);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallFoliageWriter(
	WriteFoliageHandler handler) noexcept
{
	g_writeFoliageHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallFoliageWriter(
	WriteFoliageHandler handler) noexcept
{
	g_writeFoliageHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteFoliage(const TextAssetAuthoringRequest& request,
	TextAssetAuthoringResult& result) noexcept
{
	const WriteFoliageHandler handler =
		g_writeFoliageHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request, result);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallBlackBoardWriter(
	WriteBlackBoardHandler handler) noexcept
{
	g_writeBlackBoardHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallBlackBoardWriter(
	WriteBlackBoardHandler handler) noexcept
{
	g_writeBlackBoardHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteBlackBoard(
	const TextAssetAuthoringRequest& request,
	TextAssetAuthoringResult& result) noexcept
{
	const WriteBlackBoardHandler handler =
		g_writeBlackBoardHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request, result);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallCollisionMatrixWriter(
	WriteCollisionMatrixHandler handler) noexcept
{
	g_writeCollisionMatrixHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallCollisionMatrixWriter(
	WriteCollisionMatrixHandler handler) noexcept
{
	g_writeCollisionMatrixHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteCollisionMatrix(
	const UncatalogedAuthoringRequest& request) noexcept
{
	const WriteCollisionMatrixHandler handler =
		g_writeCollisionMatrixHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallTagManagerWriter(
	WriteTagManagerHandler handler) noexcept
{
	g_writeTagManagerHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallTagManagerWriter(
	WriteTagManagerHandler handler) noexcept
{
	g_writeTagManagerHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteTagManager(
	const UncatalogedAuthoringRequest& request) noexcept
{
	const WriteTagManagerHandler handler =
		g_writeTagManagerHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request);
	}
	catch (...)
	{
		return false;
	}
}

void AssetAuthoringPort::InstallLayerSettingsWriter(
	WriteLayerSettingsHandler handler) noexcept
{
	g_writeLayerSettingsHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallLayerSettingsWriter(
	WriteLayerSettingsHandler handler) noexcept
{
	g_writeLayerSettingsHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteLayerSettings(
	const UncatalogedAuthoringRequest& request) noexcept
{
	const WriteLayerSettingsHandler handler =
		g_writeLayerSettingsHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(request);
	}
	catch (...)
	{
		return false;
	}
}

bool AssetAuthoringPort::IsInstalled() noexcept
{
	return nullptr != g_createMetaHandler.load(std::memory_order_acquire);
}

void AssetAuthoringPort::InstallSourceTextureLoader(SourceTextureLoader handler) noexcept
{
    g_sourceTextureLoader.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallSourceTextureLoader(SourceTextureLoader handler) noexcept
{
    g_sourceTextureLoader.compare_exchange_strong(handler, nullptr, std::memory_order_acq_rel);
}

own::shared_owner<const Texture::CodecImage> AssetAuthoringPort::LoadSourceTexture(
    const file::path& path, std::span<const std::byte> bytes, TextureSourceCompression compression) noexcept
{
    const auto handler = g_sourceTextureLoader.load(std::memory_order_acquire);
    if (!handler)
    {
        return {};
    }
    try
    {
        return handler(path, bytes, compression);
    }
    catch (...)
    {
        return {};
    }
}

void AssetAuthoringPort::InstallSourceTextureMipGenerator(SourceTextureMipGenerator handler) noexcept
{
    g_sourceTextureMipGenerator.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallSourceTextureMipGenerator(SourceTextureMipGenerator handler) noexcept
{
    g_sourceTextureMipGenerator.compare_exchange_strong(handler, nullptr, std::memory_order_acq_rel);
}

own::shared_owner<const Texture::CodecImage> AssetAuthoringPort::GenerateSourceTextureMips(
    const TextureImageView& image, std::string& failure) noexcept
{
    const auto handler = g_sourceTextureMipGenerator.load(std::memory_order_acquire);
    if (!handler)
    {
        failure = "Source texture mip generation requires an authoring host; recook the asset.";
        return {};
    }
    try
    {
        return handler(image, failure);
    }
    catch (...)
    {
        failure = "Authoring image processing failed.";
        return {};
    }
}

void AssetAuthoringPort::InstallSourceTextureRgbaDecoder(SourceTextureRgbaDecoder handler) noexcept
{
    g_sourceTextureRgbaDecoder.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallSourceTextureRgbaDecoder(SourceTextureRgbaDecoder handler) noexcept
{
    g_sourceTextureRgbaDecoder.compare_exchange_strong(handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::DecodeSourceTextureRgba8(
    std::span<const std::byte> bytes, TextureImage& image, std::string& failure) noexcept
{
    const auto handler = g_sourceTextureRgbaDecoder.load(std::memory_order_acquire);
    if (!handler)
    {
        failure = "Source image decoding requires an authoring host; Player accepts cooked textures only.";
        return false;
    }
    try
    {
        return handler(bytes, image, failure);
    }
    catch (...)
    {
        failure = "Authoring image processing failed.";
        return false;
    }
}

void AssetAuthoringPort::InstallTerrainSourceImageReader(TerrainSourceImageReader handler) noexcept
{
    g_terrainSourceImageReader.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallTerrainSourceImageReader(TerrainSourceImageReader handler) noexcept
{
    g_terrainSourceImageReader.compare_exchange_strong(handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::ReadTerrainSourceImage(
    const file::path& path, TerrainSourceImageKind kind, TerrainSourceImage& result) noexcept
{
    const auto handler = g_terrainSourceImageReader.load(std::memory_order_acquire);
    if (!handler)
    {
        return false;
    }
    try
    {
        return handler(path, kind, result);
    }
    catch (...)
    {
        return false;
    }
}
