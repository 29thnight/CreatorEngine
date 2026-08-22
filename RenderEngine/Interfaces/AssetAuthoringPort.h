#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

// Optional Host adapter for source-asset authoring requests made by runtime
// types. Player never installs a handler; a missing handler returns a null GUID.
class AssetAuthoringPort final
{
public:
	using CreateMetaHandler = FileGuid (*)(const file::path& filepath);

	static void Install(CreateMetaHandler handler) noexcept;
	static void Uninstall(CreateMetaHandler handler) noexcept;
	static FileGuid CreateMeta(const file::path& filepath) noexcept;
	static bool IsInstalled() noexcept;
};
