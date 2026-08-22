#include "Interfaces/AssetAuthoringPort.h"

#include <atomic>

namespace
{
	std::atomic<AssetAuthoringPort::CreateMetaHandler> g_createMetaHandler{};
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

bool AssetAuthoringPort::IsInstalled() noexcept
{
	return nullptr != g_createMetaHandler.load(std::memory_order_acquire);
}
