#include "Interfaces/AssetAuthoringPort.h"

#include <atomic>

namespace
{
	std::atomic<AssetAuthoringPort::CreateMetaHandler> g_createMetaHandler{};
	std::atomic<AssetAuthoringPort::WriteModelCacheHandler> g_writeModelCacheHandler{};
	std::atomic<AssetAuthoringPort::WriteEmbeddedTextureHandler>
		g_writeEmbeddedTextureHandler{};
	std::atomic<AssetAuthoringPort::CopyTerrainTextureHandler>
		g_copyTerrainTextureHandler{};
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

void AssetAuthoringPort::InstallTerrainTextureCopier(
	CopyTerrainTextureHandler handler) noexcept
{
	g_copyTerrainTextureHandler.store(handler, std::memory_order_release);
}

void AssetAuthoringPort::UninstallTerrainTextureCopier(
	CopyTerrainTextureHandler handler) noexcept
{
	g_copyTerrainTextureHandler.compare_exchange_strong(
		handler, nullptr, std::memory_order_acq_rel);
}

bool AssetAuthoringPort::CopyTerrainTexture(const file::path& source,
	const file::path& destination) noexcept
{
	const CopyTerrainTextureHandler handler =
		g_copyTerrainTextureHandler.load(std::memory_order_acquire);
	if (!handler) return false;
	try
	{
		return handler(source, destination);
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
