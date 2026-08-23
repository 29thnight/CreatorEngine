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

// 본문 하나와 meta로 끝나는 저작 자산의 공통 요청. runtime 타입은 자기 상태를
// 텍스트 payload로 직렬화하는 데서 멈추고, 확장자 확정·원자적 게시·meta 생성은
// Editor Host가 소유한다. payload 생성은 순수 메모리 작업이라 Core에 남는다.
struct TextAssetAuthoringRequest
{
	file::path destinationDirectory;
	std::wstring name;
	std::string payload;
};

struct TextAssetAuthoringResult
{
	file::path assetPath;
	FileGuid guid{};
};

// 카탈로그에 등록되지 않는 자산은 위 저작 자산과 다르다. GUID로 참조되지 않고
// `.meta`도 없으므로 meta를 만들지 않는다. 프로젝트 설정(`ProjectSetting/*.asset`)과
// 이름으로만 참조되는 프리셋(`Assets/InputMap`, `Assets/AnimatorController`의 json)이
// 여기 속한다 — 셋 다 실측상 `.meta`가 0개다.
// 목적 경로는 Core가 읽기와 같은 규약으로 만들고 Editor가 지정된 루트 바로 아래인지만
// 검증한다 — 이름 왕복을 없애 write/read가 갈라질 여지를 남기지 않는다.
struct UncatalogedAuthoringRequest
{
	file::path destinationPath;
	std::string payload;
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
	using WriteFoliageHandler = bool (*)(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result);
	using WriteBlackBoardHandler = bool (*)(
		const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result);
	using WriteCollisionMatrixHandler = bool (*)(
		const UncatalogedAuthoringRequest& request);
	using WriteTagManagerHandler = bool (*)(
		const UncatalogedAuthoringRequest& request);
	using WriteInputActionMapHandler = bool (*)(
		const UncatalogedAuthoringRequest& request);
	using WriteAnimatorControllerHandler = bool (*)(
		const UncatalogedAuthoringRequest& request);

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
	static bool WriteFoliage(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result) noexcept;

	static void InstallBlackBoardWriter(WriteBlackBoardHandler handler) noexcept;
	static void UninstallBlackBoardWriter(WriteBlackBoardHandler handler) noexcept;
	static bool WriteBlackBoard(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result) noexcept;

	static void InstallCollisionMatrixWriter(
		WriteCollisionMatrixHandler handler) noexcept;
	static void UninstallCollisionMatrixWriter(
		WriteCollisionMatrixHandler handler) noexcept;
	static bool WriteCollisionMatrix(
		const UncatalogedAuthoringRequest& request) noexcept;

	static void InstallTagManagerWriter(
		WriteTagManagerHandler handler) noexcept;
	static void UninstallTagManagerWriter(
		WriteTagManagerHandler handler) noexcept;
	static bool WriteTagManager(
		const UncatalogedAuthoringRequest& request) noexcept;

	static void InstallInputActionMapWriter(
		WriteInputActionMapHandler handler) noexcept;
	static void UninstallInputActionMapWriter(
		WriteInputActionMapHandler handler) noexcept;
	static bool WriteInputActionMap(
		const UncatalogedAuthoringRequest& request) noexcept;

	static void InstallAnimatorControllerWriter(
		WriteAnimatorControllerHandler handler) noexcept;
	static void UninstallAnimatorControllerWriter(
		WriteAnimatorControllerHandler handler) noexcept;
	static bool WriteAnimatorController(
		const UncatalogedAuthoringRequest& request) noexcept;

	static bool IsInstalled() noexcept;
};
