#include "Interfaces/AssetAuthoringPort.h"

#include <atomic>

namespace
{
	std::atomic<AssetAuthoringPort::CreateMetaHandler> g_createMetaHandler{};
	std::atomic<AssetAuthoringPort::WriteModelCacheHandler> g_writeModelCacheHandler{};
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
	std::atomic<AssetAuthoringPort::WriteInputActionMapHandler>
		g_writeInputActionMapHandler{};
	std::atomic<AssetAuthoringPort::WriteAnimatorControllerHandler>
		g_writeAnimatorControllerHandler{};
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

FileGuid AssetAuthoringPort::CreateMeta(const file::path& filepath) noexcept
{
	const CreateMetaHandler handler =
		g_createMetaHandler.load(std::memory_order_acquire);
	return handler ? handler(filepath) : FileGuid{};
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

void AssetAuthoringPort::InstallInputActionMapWriter(
	WriteInputActionMapHandler handler) noexcept
{
	g_writeInputActionMapHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallInputActionMapWriter(
	WriteInputActionMapHandler handler) noexcept
{
	g_writeInputActionMapHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteInputActionMap(
	const UncatalogedAuthoringRequest& request) noexcept
{
	const WriteInputActionMapHandler handler =
		g_writeInputActionMapHandler.load(std::memory_order_acquire);
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

void AssetAuthoringPort::InstallAnimatorControllerWriter(
	WriteAnimatorControllerHandler handler) noexcept
{
	g_writeAnimatorControllerHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallAnimatorControllerWriter(
	WriteAnimatorControllerHandler handler) noexcept
{
	g_writeAnimatorControllerHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::WriteAnimatorController(
	const UncatalogedAuthoringRequest& request) noexcept
{
	const WriteAnimatorControllerHandler handler =
		g_writeAnimatorControllerHandler.load(std::memory_order_acquire);
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
