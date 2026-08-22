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

bool AssetAuthoringPort::WriteFoliage(const FoliageAuthoringRequest& request,
	FoliageAuthoringResult& result) noexcept
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

bool AssetAuthoringPort::IsInstalled() noexcept
{
	return nullptr != g_createMetaHandler.load(std::memory_order_acquire);
}
