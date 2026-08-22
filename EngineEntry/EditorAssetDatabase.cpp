#include "EditorAssetDatabase.h"

#include "Interfaces/AssetAuthoringPort.h"
#include "DataSystem.h"
#include "FileDialog.h"
#include "Material.h"
#include "PathFinder.h"
#include "ReflectionYml.h"
#include "RuntimeSettings.h"
#include "StringHelper.h"
#include "VolumeProfile.h"

#include <efsw/efsw.hpp>
#include <yaml-cpp/yaml.h>
#include <DirectXTex.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <regex>
#include <unordered_set>

namespace
{
	file::path RemoveMetaExtension(const file::path& metaPath)
	{
		file::path target = metaPath;
		target.replace_extension();
		return target;
	}

	FileGuid CreateMetaThroughEditor(const file::path& filepath) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().CreateMeta(filepath);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor asset meta creation failed: " +
				std::string(exception.what()));
			return {};
		}
		catch (...)
		{
			Debug->LogError("Editor asset meta creation failed with an unknown error");
			return {};
		}
	}

	bool WriteModelCacheThroughEditor(const file::path& destination,
		std::span<const std::byte> bytes) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteModelCache(destination, bytes);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor model-cache write failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError("Editor model-cache write failed with an unknown error");
		}
		return false;
	}

	bool WriteEmbeddedTextureThroughEditor(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteEmbeddedTexture(
				destination, bytes, width, height);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor embedded-texture write failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError(
				"Editor embedded-texture write failed with an unknown error");
		}
		return false;
	}

	bool WriteTerrainThroughEditor(const TerrainAuthoringRequest& request,
		TerrainAuthoringResult& result) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteTerrain(request, result);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor terrain authoring transaction failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError(
				"Editor terrain authoring transaction failed with an unknown error");
		}
		return false;
	}

	bool WriteFoliageThroughEditor(const FoliageAuthoringRequest& request,
		FoliageAuthoringResult& result) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteFoliage(request, result);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor foliage authoring transaction failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError(
				"Editor foliage authoring transaction failed with an unknown error");
		}
		return false;
	}

	const char* ImportDirectory(EditorAssetDatabase::ImportKind kind) noexcept
	{
		switch (kind)
		{
		case EditorAssetDatabase::ImportKind::Model:
			return "Models";
		case EditorAssetDatabase::ImportKind::Texture:
			return "Textures";
		case EditorAssetDatabase::ImportKind::MaterialTexture:
			return "Materials";
		case EditorAssetDatabase::ImportKind::TerrainTexture:
			return "Terrain\\Texture";
		case EditorAssetDatabase::ImportKind::HDR:
			return "HDR";
		case EditorAssetDatabase::ImportKind::UITexture:
			return "UI";
		case EditorAssetDatabase::ImportKind::SpriteSheet:
			return "SpriteSheets";
		}
		return nullptr;
	}

	RuntimeAssetType ToRuntimeAssetType(
		EditorAssetDatabase::ImportKind kind) noexcept
	{
		switch (kind)
		{
		case EditorAssetDatabase::ImportKind::Model:
			return RuntimeAssetType::Model;
		case EditorAssetDatabase::ImportKind::UITexture:
			return RuntimeAssetType::UITexture;
		case EditorAssetDatabase::ImportKind::SpriteSheet:
			return RuntimeAssetType::SpriteSheet;
		default:
			return RuntimeAssetType::Texture;
		}
	}

	bool IsAllowedImportExtension(EditorAssetDatabase::ImportKind kind,
		std::string extension)
	{
		extension = ToLower(std::move(extension));
		if (kind == EditorAssetDatabase::ImportKind::Model)
		{
			return extension == ".fbx" || extension == ".gltf" ||
				extension == ".glb" || extension == ".obj";
		}

		if (kind == EditorAssetDatabase::ImportKind::HDR)
			return extension == ".hdr";

		return extension == ".png" || extension == ".dds" ||
			extension == ".jpg" || extension == ".jpeg";
	}

	std::wstring MakeTerrainGenerationId()
	{
		GUID guid{};
		if (FAILED(::CoCreateGuid(&guid))) return {};

		wchar_t text[40]{};
		if (0 == ::StringFromGUID2(guid, text, static_cast<int>(std::size(text))))
			return {};
		std::wstring result(text);
		result.erase(std::remove(result.begin(), result.end(), L'{'), result.end());
		result.erase(std::remove(result.begin(), result.end(), L'}'), result.end());
		result.erase(std::remove(result.begin(), result.end(), L'-'), result.end());
		return result;
	}

	bool IsSafeAssetName(const std::wstring& name)
	{
		if (name.empty() || name == L"." || name == L"..") return false;

		// std::filesystem은 "Foo:hidden"을 평범한 파일명으로 본다. NTFS는 그것을
		// 대체 데이터 스트림으로 열기 때문에 filename() 비교만으로는 부족하다.
		if (name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos) return false;
		for (const wchar_t character : name)
		{
			if (character < 0x20) return false;
		}
		// 후행 점·공백은 Win32가 조용히 잘라내므로 목적 경로가 의도와 어긋난다.
		if (name.back() == L'.' || name.back() == L' ') return false;

		static constexpr std::wstring_view kReservedDeviceNames[] = {
			L"CON", L"PRN", L"AUX", L"NUL",
			L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",
			L"COM6", L"COM7", L"COM8", L"COM9",
			L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5",
			L"LPT6", L"LPT7", L"LPT8", L"LPT9",
		};
		std::wstring stem = name.substr(0, name.find(L'.'));
		for (wchar_t& character : stem)
		{
			if (character >= L'a' && character <= L'z')
				character = static_cast<wchar_t>(character - (L'a' - L'A'));
		}
		for (const std::wstring_view reserved : kReservedDeviceNames)
		{
			if (stem == reserved) return false;
		}

		const file::path path(name);
		return path == path.filename() && !path.has_root_path();
	}

	bool IsPathInside(const file::path& path, const file::path& root)
	{
		std::error_code error;
		const file::path relative = file::relative(path, root, error);
		if (error || relative.empty() || relative.is_absolute()) return false;
		for (const file::path& part : relative)
		{
			if (part == "..") return false;
		}
		return true;
	}

	struct ScopedPathCleanup
	{
		file::path target;

		~ScopedPathCleanup()
		{
			if (target.empty()) return;
			std::error_code ignored;
			file::remove_all(target, ignored);
		}

		void Release() noexcept { target.clear(); }
	};
}

struct EditorAssetDatabase::Impl final : efsw::FileWatchListener
{
	explicit Impl(file::path root) : m_root(std::move(root)) {}

	bool Start()
	{
		if (!file::exists(m_root) || !file::is_directory(m_root))
		{
			Debug->LogError("Editor asset root is missing: " + m_root.string());
			return false;
		}

		ScanAndCleanupInvalidMeta();
		ScanAndRegisterMeta();

		m_watcher = std::make_unique<efsw::FileWatcher>();
		const efsw::WatchID watchId =
			m_watcher->addWatch(m_root.string(), this, true);
		if (watchId < 0)
		{
			Debug->LogError("Editor asset watcher registration failed: " +
				m_root.string());
			m_watcher.reset();
			return false;
		}
		m_watcher->watch();
		return true;
	}

	void Stop() noexcept
	{
		// FileWatcher owns the callback thread. Destroy it while this listener is
		// still alive, then let the rest of Impl be released.
		m_watcher.reset();
	}

	bool IsSupportExtension(std::string_view extension) const
	{
		return m_registeredFiles.contains(ToLower(std::string(extension)));
	}

	FileGuid CreateMeta(const file::path& targetFile)
	{
		std::lock_guard lock(m_authoringMutex);
		return CreateMetaLocked(targetFile);
	}

	bool WriteModelCache(const file::path& destination,
		std::span<const std::byte> bytes)
	{
		std::lock_guard lock(m_authoringMutex);
		return WriteBinaryFileLocked(destination, bytes);
	}

	bool WriteEmbeddedTexture(const file::path& destination,
		std::span<const std::byte> bytes, uint32 width, uint32 height)
	{
		std::lock_guard lock(m_authoringMutex);
		std::error_code error;
		file::create_directories(destination.parent_path(), error);
		if (error)
		{
			Debug->LogError("Editor embedded-texture directory creation failed: " +
				destination.parent_path().string() + " (" + error.message() + ")");
			return false;
		}

		// height == 0 is Assimp's contract for an already encoded payload.
		if (height == 0)
		{
			if (width != bytes.size() || !WriteBinaryFileLocked(destination, bytes))
				return false;
		}
		else
		{
			const size_t rowBytes = static_cast<size_t>(width) * 4;
			if (width == 0 || height > std::numeric_limits<size_t>::max() / rowBytes ||
				bytes.size() != rowBytes * static_cast<size_t>(height))
			{
				Debug->LogError("Editor embedded-texture payload is invalid: " +
					destination.string());
				return false;
			}

			DirectX::ScratchImage image{};
			if (FAILED(image.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM,
				width, height, 1, 1)))
			{
				return false;
			}

			const DirectX::Image* target = image.GetImage(0, 0, 0);
			if (!target) return false;
			for (uint32 row = 0; row < height; ++row)
			{
				std::memcpy(target->pixels + target->rowPitch * row,
					bytes.data() + rowBytes * row, rowBytes);
			}

			if (FAILED(DirectX::SaveToWICFile(*target, DirectX::WIC_FLAGS_NONE,
				DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), destination.c_str())))
			{
				return false;
			}
		}

		const FileGuid guid = CreateMetaLocked(destination);
		if (guid == FileGuid{}) return false;
		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::ContentReload,
			RuntimeAssetType::Texture, guid, destination });
		return true;
	}

	bool WriteTerrain(const TerrainAuthoringRequest& request,
		TerrainAuthoringResult& result)
	{
		std::lock_guard lock(m_authoringMutex);
		result = {};

		if (!IsSafeAssetName(request.name) || request.width == 0 ||
			request.height == 0 || !std::isfinite(request.minHeight) ||
			!std::isfinite(request.maxHeight) ||
			request.minHeight > request.maxHeight ||
			request.width > static_cast<uint32>(
				std::numeric_limits<int>::max() / 4) ||
			request.height > static_cast<uint32>(std::numeric_limits<int>::max()))
		{
			Debug->LogError("Editor Terrain request header is invalid");
			return false;
		}

		const size_t width = request.width;
		const size_t height = request.height;
		if (height > std::numeric_limits<size_t>::max() / width)
			return false;
		const size_t pixelCount = width * height;
		if (pixelCount > std::numeric_limits<size_t>::max() / 4 ||
			request.heightMap.size() != pixelCount)
		{
			Debug->LogError("Editor Terrain height payload is invalid");
			return false;
		}
		for (const float value : request.heightMap)
		{
			if (!std::isfinite(value)) return false;
		}
		for (const TerrainAuthoringLayerSnapshot& layer : request.layers)
		{
			std::error_code sourceError;
			if (layer.splatWeights.size() != pixelCount ||
				!std::isfinite(layer.tiling) ||
				!file::is_regular_file(layer.diffuseTextureSource, sourceError) ||
				sourceError)
			{
				Debug->LogError("Editor Terrain layer payload is invalid: " +
					layer.diffuseTextureSource.string());
				return false;
			}
			for (const float weight : layer.splatWeights)
			{
				if (!std::isfinite(weight)) return false;
			}
		}

		std::error_code error;
		const file::path terrainRoot =
			file::absolute(m_root / "Terrain", error).lexically_normal();
		if (error) return false;
		error.clear();
		const file::path destinationDirectory =
			file::absolute(request.destinationDirectory, error).lexically_normal();
		if (error || !IsPathInside(destinationDirectory, terrainRoot))
		{
			Debug->LogError("Editor Terrain destination escaped the Terrain root: " +
				request.destinationDirectory.string());
			return false;
		}
		file::create_directories(destinationDirectory, error);
		if (error) return false;
		const bool descriptorUsesRelativePaths =
			destinationDirectory == terrainRoot;

		const std::wstring generationId = MakeTerrainGenerationId();
		if (generationId.empty()) return false;
		const file::path generationRoot =
			destinationDirectory / (request.name + L".terrain-data");
		const bool generationRootExisted = file::exists(generationRoot);
		file::create_directories(generationRoot, error);
		if (error) return false;
		ScopedPathCleanup generationRootCleanup{
			generationRootExisted ? file::path{} : generationRoot };

		const file::path stagingDirectory = destinationDirectory /
			(L"." + request.name + L".terrain." + generationId + L".tmp");
		const file::path finalGeneration = generationRoot / generationId;
		file::create_directories(stagingDirectory / "Texture", error);
		if (error) return false;
		ScopedPathCleanup stagingCleanup{ stagingDirectory };
		ScopedPathCleanup generationCleanup{};

		auto WriteHeightPng = [&](const file::path& destination)
		{
			std::vector<uint8_t> bytes(pixelCount * 4);
			for (size_t index = 0; index < pixelCount; ++index)
			{
				uint32_t bits{};
				static_assert(sizeof(float) == sizeof(bits));
				std::memcpy(&bits, &request.heightMap[index], sizeof(bits));
				bytes[index * 4 + 0] = static_cast<uint8_t>(bits >> 24);
				bytes[index * 4 + 1] = static_cast<uint8_t>(bits >> 16);
				bytes[index * 4 + 2] = static_cast<uint8_t>(bits >> 8);
				bytes[index * 4 + 3] = static_cast<uint8_t>(bits);
			}
			const std::string path = WstringToString(destination.wstring());
			return 0 != stbi_write_png(path.c_str(), request.width,
				request.height, 4, bytes.data(), request.width * 4);
		};

		auto WriteSplatPng = [&](const file::path& destination,
			const std::vector<float>& weights)
		{
			std::vector<uint8_t> bytes(pixelCount);
			for (size_t index = 0; index < pixelCount; ++index)
			{
				bytes[index] = static_cast<uint8_t>(
					std::clamp(weights[index], 0.0f, 1.0f) * 255.0f);
			}
			const std::string path = WstringToString(destination.wstring());
			return 0 != stbi_write_png(path.c_str(), request.width,
				request.height, 1, bytes.data(), request.width);
		};

		const file::path stagedHeight = stagingDirectory / "HeightMap.png";
		if (!WriteHeightPng(stagedHeight)) return false;

		std::vector<file::path> stagedSplats;
		std::vector<file::path> stagedTextures;
		stagedSplats.reserve(request.layers.size());
		stagedTextures.reserve(request.layers.size());
		for (size_t index = 0; index < request.layers.size(); ++index)
		{
			const TerrainAuthoringLayerSnapshot& layer = request.layers[index];
			const file::path splat = stagingDirectory /
				(L"Splat_" + std::to_wstring(index) + L".png");
			if (!WriteSplatPng(splat, layer.splatWeights)) return false;
			stagedSplats.push_back(splat);

			const file::path texture = stagingDirectory / "Texture" /
				(std::to_wstring(index) + L"_" +
					layer.diffuseTextureSource.filename().wstring());
			error.clear();
			if (!file::copy_file(layer.diffuseTextureSource, texture,
				file::copy_options::overwrite_existing, error) || error)
			{
				return false;
			}
			stagedTextures.push_back(texture);
		}

		auto PublishedPath = [&](const file::path& staged)
		{
			return finalGeneration / file::relative(staged, stagingDirectory);
		};
		auto DescriptorPath = [&](const file::path& published)
		{
			const file::path stored = descriptorUsesRelativePaths
				? file::relative(published, terrainRoot) : published;
			return WstringToString(stored.generic_wstring());
		};

		json descriptor;
		descriptor["version"] = 2;
		descriptor["name"] = WstringToString(request.name);
		descriptor["terrainID"] = request.terrainId;
		descriptor["width"] = request.width;
		descriptor["height"] = request.height;
		descriptor["minHeight"] = request.minHeight;
		descriptor["maxHeight"] = request.maxHeight;
		descriptor["heightmap"] = DescriptorPath(PublishedPath(stagedHeight));
		descriptor["splatmaps"] = json::array();
		descriptor["layers"] = json::array();
		for (size_t index = 0; index < request.layers.size(); ++index)
		{
			descriptor["splatmaps"].push_back(
				DescriptorPath(PublishedPath(stagedSplats[index])));
			const TerrainAuthoringLayerSnapshot& layer = request.layers[index];
			json layerDescriptor;
			layerDescriptor["layerID"] = layer.layerId;
			layerDescriptor["layerName"] = layer.name;
			layerDescriptor["diffuseTexturePath"] =
				DescriptorPath(PublishedPath(stagedTextures[index]));
			layerDescriptor["tilling"] = layer.tiling;
			descriptor["layers"].push_back(std::move(layerDescriptor));
		}

		const file::path descriptorPath =
			destinationDirectory / (request.name + L".terrain");
		file::path descriptorTemporary = descriptorPath;
		descriptorTemporary += L".tmp";
		ScopedPathCleanup descriptorTemporaryCleanup{ descriptorTemporary };
		{
			std::ofstream output(descriptorTemporary,
				std::ios::binary | std::ios::trunc);
			if (!output.is_open()) return false;
			output << descriptor.dump(4);
			output.flush();
			if (!output.good()) return false;
		}

		file::path descriptorBackup = descriptorPath;
		descriptorBackup += L".rollback.tmp";
		file::path metaPath = descriptorPath;
		metaPath += L".meta";
		file::path metaBackup = metaPath;
		metaBackup += L".rollback.tmp";
		ScopedPathCleanup descriptorBackupCleanup{ descriptorBackup };
		ScopedPathCleanup metaBackupCleanup{ metaBackup };
		const bool hadDescriptor = file::exists(descriptorPath);
		const bool hadMeta = file::exists(metaPath);
		if (hadDescriptor)
		{
			error.clear();
			file::copy_file(descriptorPath, descriptorBackup,
				file::copy_options::overwrite_existing, error);
			if (error) return false;
		}
		if (hadMeta)
		{
			error.clear();
			file::copy_file(metaPath, metaBackup,
				file::copy_options::overwrite_existing, error);
			if (error) return false;
		}

		error.clear();
		file::rename(stagingDirectory, finalGeneration, error);
		if (error) return false;
		stagingCleanup.Release();
		generationCleanup.target = finalGeneration;

		bool descriptorPublished = false;
		auto RollbackDescriptor = [&]()
		{
			std::error_code ignored;
			if (hadDescriptor)
			{
				::MoveFileExW(descriptorBackup.c_str(), descriptorPath.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
				descriptorBackupCleanup.Release();
			}
			else
			{
				file::remove(descriptorPath, ignored);
			}
			if (hadMeta)
			{
				::MoveFileExW(metaBackup.c_str(), metaPath.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
				metaBackupCleanup.Release();
			}
			else
			{
				file::remove(metaPath, ignored);
			}
		};

		try
		{
			if (!::MoveFileExW(descriptorTemporary.c_str(), descriptorPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				return false;
			}
			descriptorTemporaryCleanup.Release();
			descriptorPublished = true;

			const FileGuid guid = CreateMetaLocked(descriptorPath);
			if (guid == FileGuid{})
			{
				RollbackDescriptor();
				return false;
			}

			result.descriptorPath = descriptorPath;
			result.guid = guid;
			generationCleanup.Release();
			generationRootCleanup.Release();
			return true;
		}
		catch (...)
		{
			if (descriptorPublished) RollbackDescriptor();
			throw;
		}
	}

	bool WriteFoliage(const FoliageAuthoringRequest& request,
		FoliageAuthoringResult& result)
	{
		std::lock_guard lock(m_authoringMutex);
		result = {};

		if (!IsSafeAssetName(request.name) || request.payload.empty())
		{
			Debug->LogError("Editor Foliage request is invalid");
			return false;
		}

		std::error_code error;
		const file::path foliageRoot =
			file::absolute(m_root / "Foliage", error).lexically_normal();
		if (error) return false;
		error.clear();
		const file::path destinationDirectory =
			file::absolute(request.destinationDirectory, error).lexically_normal();
		if (error || !IsPathInside(destinationDirectory, foliageRoot))
		{
			Debug->LogError("Editor Foliage destination escaped the Foliage root: " +
				request.destinationDirectory.string());
			return false;
		}

		const file::path assetPath =
			destinationDirectory / (request.name + L".foliage");
		const std::span<const std::byte> payload{
			reinterpret_cast<const std::byte*>(request.payload.data()),
			request.payload.size() };
		if (!WriteBinaryFileLocked(assetPath, payload)) return false;

		const FileGuid guid = CreateMetaLocked(assetPath);
		if (guid == FileGuid{}) return false;

		result.assetPath = assetPath;
		result.guid = guid;
		return true;
	}

	file::path ImportSourceAsset(const file::path& source,
		EditorAssetDatabase::ImportKind kind)
	{
		std::lock_guard lock(m_authoringMutex);

		std::error_code error;
		if (source.empty() || !file::is_regular_file(source, error) || error)
		{
			Debug->LogError("Editor asset import source is missing: " + source.string());
			return {};
		}

		if (!IsAllowedImportExtension(kind, source.extension().string()))
		{
			Debug->LogError("Editor asset import extension does not match its kind: " +
				source.string());
			return {};
		}

		const char* directory = ImportDirectory(kind);
		if (nullptr == directory) return {};

		const file::path destinationDirectory = m_root / directory;
		file::create_directories(destinationDirectory, error);
		if (error)
		{
			Debug->LogError("Editor asset import directory creation failed: " +
				destinationDirectory.string() + " (" + error.message() + ")");
			return {};
		}

		const file::path destination = destinationDirectory / source.filename();
		error.clear();
		const bool sameFile = file::exists(destination, error) && !error &&
			file::equivalent(source, destination, error) && !error;
		if (!sameFile)
		{
			error.clear();
			file::copy_file(source, destination,
				file::copy_options::overwrite_existing, error);
			if (error)
			{
				Debug->LogError("Editor asset import copy failed: " + source.string() +
					" -> " + destination.string() + " (" + error.message() + ")");
				return {};
			}
		}

		const FileGuid guid = CreateMetaLocked(destination);
		if (guid == FileGuid{})
		{
			Debug->LogError("Editor asset import meta creation failed: " +
				destination.string());
			return {};
		}
		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::ContentReload,
			ToRuntimeAssetType(kind), guid, destination });
		return destination;
	}

	void handleFileAction(efsw::WatchID, const std::string& directory,
		const std::string& filename, efsw::Action action,
		const std::string& oldFilename) override
	{
		try
		{
			const file::path directoryPath(directory);
			const file::path filepath = directoryPath / filename;
			if (ContainsTemporaryPath(filepath)) return;

			switch (action)
			{
			case efsw::Actions::Add:
				HandleCreated(filepath);
				break;
			case efsw::Actions::Moved:
				HandleMoved(directoryPath, oldFilename, filename);
				break;
			case efsw::Actions::Delete:
				HandleDeleted(filepath);
				break;
			default:
				break;
			}
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor asset watcher callback failed: " +
				std::string(exception.what()));
		}
	}

private:
	bool WriteBinaryFileLocked(const file::path& destination,
		std::span<const std::byte> bytes)
	{
		if (destination.empty() || bytes.empty() ||
			bytes.size() > static_cast<size_t>(
				std::numeric_limits<std::streamsize>::max()))
		{
			return false;
		}
		std::error_code error;
		file::create_directories(destination.parent_path(), error);
		if (error) return false;

		// The watcher ignores .tmp paths. Publish only after the complete payload
		// is flushed so runtime readers never observe a partial cache/image.
		const file::path temporary = destination.string() + ".tmp";
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) return false;
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		output.flush();
		const bool complete = output.good();
		output.close();
		if (!complete)
		{
			file::remove(temporary, error);
			return false;
		}

		if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			file::remove(temporary, error);
			return false;
		}
		return true;
	}

	bool ContainsTemporaryPath(const file::path& path) const
	{
		const std::string lower = ToLower(path.string());
		return lower.find(".tmp") != std::string::npos ||
			lower.find("~$") != std::string::npos;
	}

	bool IsTargetFile(const file::path& path) const
	{
		const std::string filename = path.filename().string();
		if (filename.find('~') != std::string::npos || ContainsTemporaryPath(path))
			return false;
		return IsSupportExtension(path.extension().string());
	}

	FileGuid LoadGuidFromMeta(const file::path& metaPath) const
	{
		const YAML::Node node = YAML::LoadFile(metaPath.string());
		if (!node["guid"] || !node["guid"].IsScalar()) return {};
		return FileGuid(node["guid"].as<std::string>());
	}

	void RegisterMetaFile(const file::path& metaPath)
	{
		std::lock_guard lock(m_authoringMutex);
		const file::path targetFile = RemoveMetaExtension(metaPath);
		if (!file::exists(targetFile)) return;
		const FileGuid guid = LoadGuidFromMeta(metaPath);
		if (guid != FileGuid{})
		{
			DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
				RuntimeAssetType::Auto, guid, targetFile });
		}
	}

	void ScanAndRegisterMeta()
	{
		for (const auto& entry : file::recursive_directory_iterator(
			m_root, file::directory_options::skip_permission_denied))
		{
			if (!entry.is_regular_file() || !IsTargetFile(entry.path())) continue;
			const file::path metaPath = entry.path().string() + ".meta";
			if (file::exists(metaPath))
				RegisterMetaFile(metaPath);
			else
				CreateMeta(entry.path());
		}
	}

	void ScanAndCleanupInvalidMeta()
	{
		for (const auto& entry : file::recursive_directory_iterator(
			m_root, file::directory_options::skip_permission_denied))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".meta")
				continue;

			const file::path& metaPath = entry.path();
			const std::string filename = metaPath.filename().string();
			const file::path targetPath = RemoveMetaExtension(metaPath);
			if (filename.ends_with(".meta.meta") ||
				filename.find('~') != std::string::npos ||
				!file::exists(targetPath))
			{
				DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
					RuntimeAssetType::Auto, {}, targetPath });
				std::error_code error;
				file::remove(metaPath, error);
				if (error)
					Debug->LogWarning("Failed to remove invalid meta: " +
						metaPath.string() + " (" + error.message() + ")");
			}
		}
	}

	std::vector<std::string> ExtractFunctionNames(const file::path& source) const
	{
		std::vector<std::string> functions;
		std::ifstream input(source);
		std::string line;
		const std::regex functionRegex(
			R"((?:[\w:&<>\*\s]+)\s+[\w:]+::([\w_]+)\s*\([^)]*\)\s*\{?)");
		while (std::getline(input, line))
		{
			std::smatch match;
			if (std::regex_search(line, match, functionRegex) && match.size() >= 2)
				functions.push_back(match[1].str());
		}
		return functions;
	}

	bool HasScriptReflectionFieldAttribute(const file::path& header) const
	{
		std::ifstream input(header);
		std::string line;
		const std::regex attributeRegex(
			R"(^\s*\[\[ScriptReflectionField(\(.*\))?\]\])");
		while (std::getline(input, line))
		{
			if (std::regex_search(line, attributeRegex)) return true;
		}
		return false;
	}

	FileGuid ResolveOrCreateGuid(const file::path& targetFile, YAML::Node& root) const
	{
		if (targetFile.extension() == ".prefab")
		{
			const YAML::Node prefabRoot = YAML::LoadFile(targetFile.string());
			// SavePrefab가 발급하는 prefab 자체의 정체성이 정본이다. PrefabNode는
			// Map 또는 중첩 Sequence일 수 있으므로 루트 값을 먼저 사용한다.
			const YAML::Node prefabFileGuid = prefabRoot["m_fileGuid"];
			if (prefabFileGuid && prefabFileGuid.IsScalar())
			{
				const FileGuid prefabGuid(prefabFileGuid.as<std::string>());
				if (prefabGuid != FileGuid{})
				{
					root["guid"] = prefabGuid.ToString();
					return prefabGuid;
				}
			}

			// 구형 prefab은 루트 GUID가 없을 수 있으므로 첫 엔티티 각인을
			// 호환 경로로 남긴다.
			const YAML::Node prefabNodes = prefabRoot["PrefabNode"];
			if (prefabNodes && prefabNodes.IsSequence())
			{
				for (const YAML::Node& element : prefabNodes)
				{
					const YAML::Node guidNode = element["m_prefabFileGuid"];
					if (guidNode && guidNode.IsScalar())
					{
						const FileGuid prefabGuid(guidNode.as<std::string>());
						if (prefabGuid != FileGuid{})
						{
							root["guid"] = prefabGuid.ToString();
							return prefabGuid;
						}
					}
				}
			}
		}

		if (root["guid"] && root["guid"].IsScalar())
			return FileGuid(root["guid"].as<std::string>());

		const FileGuid generated =
			GUIDCreator::MakeFileGUID(targetFile.filename().string());
		root["guid"] = generated.ToString();
		return generated;
	}

	FileGuid CreateMetaLocked(const file::path& targetFile)
	{
		if (targetFile.empty() || !file::exists(targetFile)) return {};

		const file::path metaPath = targetFile.string() + ".meta";
		YAML::Node root;
		if (file::exists(metaPath)) root = YAML::LoadFile(metaPath.string());

		const FileGuid guid = ResolveOrCreateGuid(targetFile, root);
		root["importSettings"]["extension"] = targetFile.extension().string();
		std::error_code timestampError;
		const auto timestamp = file::last_write_time(targetFile, timestampError);
		root["importSettings"]["timestamp"] = timestampError
			? 0 : timestamp.time_since_epoch().count();

		const std::string extension = ToLower(targetFile.extension().string());
		if (extension == ".cpp")
		{
			root.remove("reflectionFlag");
			file::path header = targetFile;
			header.replace_extension(".h");
			root["reflectionFlag"] = file::exists(header) &&
				HasScriptReflectionFieldAttribute(header);
			root.remove("eventRegisterSetting");
			for (const std::string& function : ExtractFunctionNames(targetFile))
				root["eventRegisterSetting"].push_back(function);
		}
		else if (extension == ".fbx" || extension == ".gltf" ||
			extension == ".obj" || extension == ".glb")
		{
			root["ModelImporter"]["OptimizeMeshes"] = true;
			root["ModelImporter"]["ImproveCacheLocality"] = true;
			root["ModelImporter"]["CreateMeshCollider"] = false;
		}

		std::ofstream output(metaPath);
		if (!output.is_open()) return {};
		output << root;
		output.flush();
		if (!output.good()) return {};
		output.close();
		if (output.fail()) return {};

		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
			RuntimeAssetType::Auto, guid, targetFile });
		return guid;
	}

	void HandleCreated(const file::path& filepath)
	{
		if (filepath.extension() == ".meta")
		{
			RegisterMetaFile(filepath);
			return;
		}
		if (IsTargetFile(filepath)) CreateMeta(filepath);
	}

	void HandleMoved(const file::path& directory, const std::string& oldName,
		const std::string& newName)
	{
		const file::path oldPath = directory / oldName;
		const file::path newPath = directory / newName;
		if (newPath.extension() == ".meta")
		{
			DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
				RuntimeAssetType::Auto, {}, RemoveMetaExtension(oldPath) });
			RegisterMetaFile(newPath);
			return;
		}

		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
			RuntimeAssetType::Auto, {}, oldPath });
		if (!IsTargetFile(newPath)) return;

		const file::path oldMeta = oldPath.string() + ".meta";
		const file::path newMeta = newPath.string() + ".meta";
		if (file::exists(oldMeta) && !file::exists(newMeta))
		{
			std::error_code error;
			file::rename(oldMeta, newMeta, error);
			if (error)
				Debug->LogWarning("Failed to move asset meta: " + error.message());
		}

		if (file::exists(newMeta)) RegisterMetaFile(newMeta);
		else CreateMeta(newPath);
	}

	void HandleDeleted(const file::path& deletedPath)
	{
		if (deletedPath.extension() == ".meta")
		{
			DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
				RuntimeAssetType::Auto, {}, RemoveMetaExtension(deletedPath) });
			return;
		}

		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
			RuntimeAssetType::Auto, {}, deletedPath });
		const file::path metaPath = deletedPath.string() + ".meta";
		std::error_code error;
		file::remove(metaPath, error);
		if (error)
			Debug->LogWarning("Failed to remove deleted asset meta: " +
				error.message());
	}

	file::path m_root;
	std::mutex m_authoringMutex;
	std::unique_ptr<efsw::FileWatcher> m_watcher;
	const std::unordered_set<std::string> m_registeredFiles{
		".fbx", ".gltf", ".obj", ".glb",
		".png", ".dds", ".jpg", ".jpeg", ".hdr",
		".hlsl", ".shader", ".cpp", ".cs",
		".wav", ".mp3", ".ogg", ".spritefont",
		".terrain", ".bt", ".blackboard", ".prefab", ".volume",
		".foliage", ".asset"
	};
};

EditorAssetDatabase& EditorAssetDatabase::Get() noexcept
{
	static EditorAssetDatabase instance;
	return instance;
}

EditorAssetDatabase::~EditorAssetDatabase()
{
	Shutdown();
}

bool EditorAssetDatabase::Initialize()
{
	if (m_impl) return true;
	if (!PathFinder::IsAssetAuthoringEnabled()) return false;

	auto implementation = std::make_unique<Impl>(PathFinder::Relative());
	if (!implementation->Start()) return false;
	m_impl = std::move(implementation);
	AssetAuthoringPort::Install(&CreateMetaThroughEditor);
	AssetAuthoringPort::InstallModelCacheWriter(&WriteModelCacheThroughEditor);
	AssetAuthoringPort::InstallEmbeddedTextureWriter(
		&WriteEmbeddedTextureThroughEditor);
	AssetAuthoringPort::InstallTerrainWriter(&WriteTerrainThroughEditor);
	AssetAuthoringPort::InstallFoliageWriter(&WriteFoliageThroughEditor);
	return true;
}

void EditorAssetDatabase::Shutdown() noexcept
{
	AssetAuthoringPort::UninstallFoliageWriter(&WriteFoliageThroughEditor);
	AssetAuthoringPort::UninstallTerrainWriter(&WriteTerrainThroughEditor);
	AssetAuthoringPort::UninstallEmbeddedTextureWriter(
		&WriteEmbeddedTextureThroughEditor);
	AssetAuthoringPort::UninstallModelCacheWriter(&WriteModelCacheThroughEditor);
	AssetAuthoringPort::Uninstall(&CreateMetaThroughEditor);
	if (m_impl) m_impl->Stop();
	m_impl.reset();
}

bool EditorAssetDatabase::IsInitialized() const noexcept
{
	return nullptr != m_impl;
}

FileGuid EditorAssetDatabase::CreateMeta(const file::path& filepath)
{
	return m_impl ? m_impl->CreateMeta(filepath) : FileGuid{};
}

bool EditorAssetDatabase::WriteModelCache(const file::path& destination,
	std::span<const std::byte> bytes)
{
	return m_impl && m_impl->WriteModelCache(destination, bytes);
}

bool EditorAssetDatabase::WriteEmbeddedTexture(const file::path& destination,
	std::span<const std::byte> bytes, uint32 width, uint32 height)
{
	return m_impl && m_impl->WriteEmbeddedTexture(destination, bytes, width, height);
}

bool EditorAssetDatabase::WriteTerrain(const TerrainAuthoringRequest& request,
	TerrainAuthoringResult& result)
{
	return m_impl && m_impl->WriteTerrain(request, result);
}

bool EditorAssetDatabase::WriteFoliage(const FoliageAuthoringRequest& request,
	FoliageAuthoringResult& result)
{
	return m_impl && m_impl->WriteFoliage(request, result);
}

file::path EditorAssetDatabase::ImportSourceAsset(
	const file::path& source, ImportKind kind)
{
	if (!m_impl) return {};
	try
	{
		return m_impl->ImportSourceAsset(source, kind);
	}
	catch (const std::exception& exception)
	{
		Debug->LogError("Editor asset import failed: " + source.string() +
			" (" + exception.what() + ")");
	}
	catch (...)
	{
		Debug->LogError("Editor asset import failed with an unknown error: " +
			source.string());
	}
	return {};
}

bool EditorAssetDatabase::IsSupportExtension(std::string_view extension) const
{
	return m_impl && m_impl->IsSupportExtension(extension);
}

bool EditorAssetDatabase::SaveMaterial(Material* material)
{
	if (!m_impl || !material) return false;
	const file::path savePath = PathFinder::Relative("Materials\\") /
		(material->m_name + ".asset");
	std::ofstream output(savePath);
	if (!output.is_open()) return false;

	YAML::Node node = Meta::Serialize(material);
	if (!material->m_cbufferValues.empty())
	{
		YAML::Node buffers;
		for (const auto& [name, data] : material->m_cbufferValues)
		{
			YAML::Node entry;
			entry["name"] = name;
			entry["data"] = YAML::Binary(data.data(), data.size());
			buffers.push_back(entry);
		}
		node["constant_buffers"] = buffers;
	}
	output << node;
	output.flush();
	if (!output.good()) return false;
	output.close();
	return CreateMeta(savePath) != FileGuid{};
}

bool EditorAssetDatabase::CreateVolumeProfile(const file::path& directory)
{
	if (!m_impl || directory.empty()) return false;
	const file::path selectedPath = ShowSaveFileDialog(
		L"", L"Save File", PathFinder::VolumeProfilePath());
	if (selectedPath.empty()) return false;

	VolumeProfile profile;
	profile.settings = RuntimeSettings::Get().GetRenderPassSettings();
	const std::string baseName = selectedPath.stem().string();
	file::path fullPath = directory / (baseName + ".volume");
	int suffix = 1;
	while (file::exists(fullPath))
		fullPath = directory / (baseName + std::to_string(suffix++) + ".volume");

	std::ofstream output(fullPath);
	if (!output.is_open()) return false;
	output << Meta::Serialize(&profile);
	output.flush();
	if (!output.good()) return false;
	output.close();
	return CreateMeta(fullPath) != FileGuid{};
}

bool EditorAssetDatabase::SaveExistingVolumeProfile(
	FileGuid guid, VolumeProfile* volume)
{
	if (!m_impl || !volume) return false;
	const file::path savePath = DataSystems->GetFilePath(guid);
	if (savePath.empty())
	{
		Debug->LogError(
			"EditorAssetDatabase::SaveExistingVolumeProfile: path is empty");
		return false;
	}

	std::ofstream output(savePath);
	if (!output.is_open()) return false;
	output << Meta::Serialize(volume);
	output.flush();
	return output.good();
}
