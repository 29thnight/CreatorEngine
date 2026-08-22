#pragma once

#include "Core.Minimal.h"
#include "TypeTrait.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Runtime Terrain은 저작 파일 형식을 알지 않는다. PresentationThread가 살아 있는
// component를 읽을 수 있는 구간에서 이 값 스냅샷을 만든 뒤 Editor Host에 넘긴다.
struct TerrainAuthoringLayerSnapshot
{
	uint32 layerId{};
	std::string name;
	file::path diffuseTextureSource;
	float tiling{ 1.0f };
	std::vector<float> splatWeights;
};

struct TerrainAuthoringRequest
{
	file::path destinationDirectory;
	std::wstring name;
	uint32 terrainId{};
	uint32 width{};
	uint32 height{};
	float minHeight{};
	float maxHeight{};
	std::vector<float> heightMap;
	std::vector<TerrainAuthoringLayerSnapshot> layers;
};

struct TerrainAuthoringResult
{
	file::path descriptorPath;
	FileGuid guid{};
};

// Runtime Foliage도 저작 파일 위치와 확장자를 알지 않는다. 컴포넌트는 자기
// 상태를 YAML payload로 직렬화하는 데서 멈추고, 목적 경로 결정과 원자적 게시는
// Editor Host가 소유한다. payload 생성은 순수 메모리 작업이라 Core에 남는다.
struct FoliageAuthoringRequest
{
	file::path destinationDirectory;
	std::wstring name;
	std::string payload;
};

struct FoliageAuthoringResult
{
	file::path assetPath;
	FileGuid guid{};
};

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
	using WriteTerrainHandler = bool (*)(const TerrainAuthoringRequest& request,
		TerrainAuthoringResult& result);
	using WriteFoliageHandler = bool (*)(const FoliageAuthoringRequest& request,
		FoliageAuthoringResult& result);

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

	static void InstallTerrainWriter(WriteTerrainHandler handler) noexcept;
	static void UninstallTerrainWriter(WriteTerrainHandler handler) noexcept;
	static bool WriteTerrain(const TerrainAuthoringRequest& request,
		TerrainAuthoringResult& result) noexcept;

	static void InstallFoliageWriter(WriteFoliageHandler handler) noexcept;
	static void UninstallFoliageWriter(WriteFoliageHandler handler) noexcept;
	static bool WriteFoliage(const FoliageAuthoringRequest& request,
		FoliageAuthoringResult& result) noexcept;

	static bool IsInstalled() noexcept;
};
