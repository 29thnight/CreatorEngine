#include "EditorAssetDatabase.h"

#include "Interfaces/AssetAuthoringPort.h"
#include "Assets/ModelAssetAuthoringTransaction.h"
#include "DataSystem.h"
#include "FileDialog.h"
#include "Material.h"
#include "PathFinder.h"
#include "AuthoringParsedDocument.h"
#include "ReflectionYml.h"
#include "RuntimeSettings.h"
#include "StringHelper.h"
#include "VolumeProfile.h"

#include <efsw/efsw.hpp>
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

	FileGuid CreateMetaThroughEditor(const file::path& filepath,
		const FileGuid& preferredGuid) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().CreateMeta(filepath, preferredGuid);
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

	FileGuid WriteTextAssetWithMetaThroughEditor(const file::path& destination,
		std::string_view payload, const FileGuid& preferredGuid) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteTextAssetWithMeta(
				destination, payload, preferredGuid);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor text asset publication failed: "
				+ std::string(exception.what()));
			return {};
		}
		catch (...)
		{
			Debug->LogError(
				"Editor text asset publication failed with an unknown error");
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

	bool WriteFoliageThroughEditor(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result) noexcept
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

	bool WriteCollisionMatrixThroughEditor(
		const UncatalogedAuthoringRequest& request) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteCollisionMatrix(request);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor collision-matrix authoring failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError("Editor collision-matrix authoring failed with an "
				"unknown error");
		}
		return false;
	}

	bool WriteInputActionMapThroughEditor(
		const UncatalogedAuthoringRequest& request) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteInputActionMap(request);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor input-action-map authoring failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError("Editor input-action-map authoring failed with an "
				"unknown error");
		}
		return false;
	}

	bool WriteTagManagerThroughEditor(
		const UncatalogedAuthoringRequest& request) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteTagManager(request);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor tag-manager authoring failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError("Editor tag-manager authoring failed with an "
				"unknown error");
		}
		return false;
	}

	bool WriteBlackBoardThroughEditor(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result) noexcept
	{
		try
		{
			return EditorAssetDatabase::Get().WriteBlackBoard(request, result);
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("Editor blackboard authoring transaction failed: " +
				std::string(exception.what()));
		}
		catch (...)
		{
			Debug->LogError("Editor blackboard authoring transaction failed with "
				"an unknown error");
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

	FileGuid CreateMeta(const file::path& targetFile,
		const FileGuid& preferredGuid = {})
	{
		std::lock_guard lock(m_authoringMutex);
		return CreateMetaLocked(targetFile, preferredGuid);
	}

	FileGuid WriteTextAssetWithMeta(const file::path& requestedDestination,
		std::string_view payload, const FileGuid& preferredGuid)
	{
		std::lock_guard lock(m_authoringMutex);
		std::error_code error;
		const file::path destination =
			file::absolute(requestedDestination, error).lexically_normal();
		if (error) return {};
		error.clear();
		const file::path root = file::absolute(m_root, error).lexically_normal();
		if (error || payload.empty() || !preferredGuid.IsRandomV4()
			|| !IsPathInside(destination, root) || !IsTargetFile(destination)
			|| !IsSafeAssetName(destination.filename().wstring()))
		{
			Debug->LogError("Editor cataloged text publication rejected: "
				+ requestedDestination.string());
			return {};
		}

		const std::span<const std::byte> bytes{
			reinterpret_cast<const std::byte*>(payload.data()), payload.size() };
		if (!WriteBinaryFileLocked(destination, bytes, PublishEncoding::Text))
			return {};
		return CreateMetaLocked(destination, preferredGuid);
	}

	FileGuid RenameAsset(const file::path& source, const file::path& destination)
	{
		std::lock_guard lock(m_authoringMutex);

		std::error_code error;
		const file::path sourcePath = file::absolute(source, error).lexically_normal();
		if (error) return {};
		error.clear();
		const file::path destinationPath =
			file::absolute(destination, error).lexically_normal();
		if (error) return {};
		error.clear();
		const file::path rootPath = file::absolute(m_root, error).lexically_normal();
		if (error || !IsPathInside(sourcePath, rootPath)
			|| !IsPathInside(destinationPath, rootPath)
			|| sourcePath == destinationPath
			|| !file::is_regular_file(sourcePath, error) || error
			|| file::exists(destinationPath)
			|| !IsTargetFile(sourcePath) || !IsTargetFile(destinationPath)
			|| ToLower(sourcePath.extension().string())
				!= ToLower(destinationPath.extension().string())
			|| !file::is_directory(destinationPath.parent_path()))
		{
			Debug->LogError("Editor asset rename rejected: " + source.string()
				+ " -> " + destination.string());
			return {};
		}

		const file::path sourceMeta = sourcePath.string() + ".meta";
		const file::path destinationMeta = destinationPath.string() + ".meta";
		if (!file::is_regular_file(sourceMeta, error) || error
			|| file::exists(destinationMeta))
		{
			Debug->LogError("Editor asset rename requires exactly one source sidecar: "
				+ sourceMeta.string());
			return {};
		}

		const FileGuid guid = LoadGuidFromMeta(sourceMeta);
		if (guid == FileGuid{})
		{
			Debug->LogError("Editor asset rename rejected invalid source sidecar: "
				+ sourceMeta.string());
			return {};
		}

		// sidecar를 먼저 옮긴다. target을 먼저 옮기면 watcher가 그 짧은 틈에
		// missing meta로 판단해 새 UUID를 발급할 수 있다.
		error.clear();
		file::rename(sourceMeta, destinationMeta, error);
		if (error)
		{
			Debug->LogError("Editor asset sidecar rename failed: " + error.message());
			return {};
		}

		error.clear();
		file::rename(sourcePath, destinationPath, error);
		if (error)
		{
			const std::string moveError = error.message();
			std::error_code rollbackError;
			file::rename(destinationMeta, sourceMeta, rollbackError);
			Debug->LogError("Editor asset rename failed: " + moveError
				+ (rollbackError ? " (sidecar rollback failed: "
					+ rollbackError.message() + ")" : ""));
			return {};
		}

		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
			RuntimeAssetType::Auto, guid, sourcePath });
		DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
			RuntimeAssetType::Auto, guid, destinationPath });
		return guid;
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
			request.height > static_cast<uint32>(std::numeric_limits<int>::max()) ||
			request.layers.size() > 4)
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

		Authoring::WriteDocument descriptor;
		const Authoring::WriteNode descriptorRoot = descriptor.Root();
		descriptorRoot.SetMap();
		descriptorRoot.Child("schemaVersion").SetScalar(1);
		descriptorRoot.Child("name").SetScalar(WstringToString(request.name));
		descriptorRoot.Child("terrainID").SetScalar(request.terrainId);
		descriptorRoot.Child("width").SetScalar(request.width);
		descriptorRoot.Child("height").SetScalar(request.height);
		descriptorRoot.Child("minHeight").SetScalar(request.minHeight);
		descriptorRoot.Child("maxHeight").SetScalar(request.maxHeight);
		descriptorRoot.Child("heightmap").SetScalar(
			DescriptorPath(PublishedPath(stagedHeight)));
		const Authoring::WriteNode splatmaps =
			descriptorRoot.Child("splatmaps");
		splatmaps.SetSequence();
		const Authoring::WriteNode layers = descriptorRoot.Child("layers");
		layers.SetSequence();
		for (size_t index = 0; index < request.layers.size(); ++index)
		{
			splatmaps.Append().SetScalar(
				DescriptorPath(PublishedPath(stagedSplats[index])));
			const TerrainAuthoringLayerSnapshot& layer = request.layers[index];
			const Authoring::WriteNode layerDescriptor = layers.Append();
			layerDescriptor.SetMap();
			layerDescriptor.Child("layerID").SetScalar(layer.layerId);
			layerDescriptor.Child("layerName").SetScalar(layer.name);
			layerDescriptor.Child("diffuseTexturePath").SetScalar(
				DescriptorPath(PublishedPath(stagedTextures[index])));
			layerDescriptor.Child("tiling").SetScalar(layer.tiling);
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
			output << descriptor.Dump();
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

	bool WriteFoliage(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result)
	{
		std::lock_guard lock(m_authoringMutex);
		return PublishTextAssetLocked("Foliage", L"Foliage", L".foliage",
			request, result);
	}

	bool WriteBlackBoard(const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result)
	{
		std::lock_guard lock(m_authoringMutex);
		return PublishTextAssetLocked("BlackBoard", L"BehaviorTree",
			L".blackboard", request, result);
	}

	bool WriteCollisionMatrix(const UncatalogedAuthoringRequest& request)
	{
		std::lock_guard lock(m_authoringMutex);
		return PublishUncatalogedLocked("CollisionMatrix",
			PathFinder::ProjectSettingPath(""), request);
	}

	bool WriteTagManager(const UncatalogedAuthoringRequest& request)
	{
		std::lock_guard lock(m_authoringMutex);
		return PublishUncatalogedLocked("TagManager",
			PathFinder::ProjectSettingPath(""), request);
	}

	bool WriteInputActionMap(const UncatalogedAuthoringRequest& request)
	{
		std::lock_guard lock(m_authoringMutex);
		return PublishUncatalogedLocked("InputActionMap",
			PathFinder::InputMapPath(), request);
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
			case efsw::Actions::Modified:
				HandleModified(filepath);
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
	// 본문 하나와 meta로 끝나는 저작 자산의 공통 게시 경로. 목적지를 authoring
	// root 하위 지정 폴더로 제한하고, `.tmp` staging → 원자적 교체 → meta 생성까지
	// 한 임계 구역에서 끝낸다. 호출자가 m_authoringMutex를 이미 잡고 들어온다.
	bool PublishTextAssetLocked(std::string_view label,
		std::wstring_view rootFolder, std::wstring_view extension,
		const TextAssetAuthoringRequest& request,
		TextAssetAuthoringResult& result)
	{
		result = {};

		if (!IsSafeAssetName(request.name) || request.payload.empty())
		{
			Debug->LogError("Editor " + std::string(label) +
				" request is invalid");
			return false;
		}

		std::error_code error;
		const file::path authoringRoot =
			file::absolute(m_root / rootFolder, error).lexically_normal();
		if (error) return false;
		error.clear();
		const file::path destinationDirectory =
			file::absolute(request.destinationDirectory, error).lexically_normal();
		if (error || !IsPathInside(destinationDirectory, authoringRoot))
		{
			Debug->LogError("Editor " + std::string(label) +
				" destination escaped its authoring root: " +
				request.destinationDirectory.string());
			return false;
		}

		const file::path assetPath =
			destinationDirectory / (request.name + std::wstring(extension));
		const std::span<const std::byte> payload{
			reinterpret_cast<const std::byte*>(request.payload.data()),
			request.payload.size() };
		if (!WriteBinaryFileLocked(assetPath, payload, PublishEncoding::Text))
			return false;

		const FileGuid guid = CreateMetaLocked(assetPath);
		if (guid == FileGuid{}) return false;

		result.assetPath = assetPath;
		result.guid = guid;
		return true;
	}

	// 카탈로그에 등록되지 않는 자산의 게시 경로. 저작 자산과 달리 meta를 만들지 않고
	// catalog에도 넣지 않는다 — 이 파일들은 GUID로 참조되지 않으며 `.meta`가 하나도
	// 없다. 목적지는 호출자가 지정한 루트 바로 아래 한 칸으로 못 박는다. 루트는 요청이
	// 아니라 handler가 정하므로 요청으로는 벗어날 수 없다.
	bool PublishUncatalogedLocked(std::string_view label,
		const file::path& authoringRoot,
		const UncatalogedAuthoringRequest& request)
	{
		if (request.payload.empty())
		{
			Debug->LogError("Editor " + std::string(label) + " payload is empty");
			return false;
		}

		std::error_code error;
		// PathFinder의 디렉터리 접근자는 후행 구분자가 붙은 경로를 돌려줄 수 있다.
		// 그대로 두면 parent_path() 비교가 항상 어긋나 정상 요청까지 거부된다.
		file::path root = file::absolute(authoringRoot, error).lexically_normal();
		if (error) return false;
		if (root.filename().empty())
		{
			root = root.parent_path();
		}
		error.clear();
		const file::path destination =
			file::absolute(request.destinationPath, error).lexically_normal();
		if (error || destination.parent_path() != root ||
			!IsSafeAssetName(destination.filename().wstring()))
		{
			Debug->LogError("Editor " + std::string(label) +
				" destination is not directly under its authoring root: " +
				request.destinationPath.string());
			return false;
		}

		const std::span<const std::byte> payload{
			reinterpret_cast<const std::byte*>(request.payload.data()),
			request.payload.size() };
		return WriteBinaryFileLocked(destination, payload, PublishEncoding::Text);
	}

	// 텍스트 자산은 텍스트 모드로 쓴다. 이진 모드는 줄바꿈을 LF로 남기는데, 추적
	// 자산은 CRLF로 체크아웃되므로 저장할 때마다 워킹트리가 더러워진다. 기존
	// text-mode ofstream writer들이 지켜 온 규약이다.
	enum class PublishEncoding { Binary, Text };

	bool WriteBinaryFileLocked(const file::path& destination,
		std::span<const std::byte> bytes,
		PublishEncoding encoding = PublishEncoding::Binary)
	{
		if (destination.empty() || bytes.empty() ||
			bytes.size() > static_cast<size_t>(
				std::numeric_limits<std::streamsize>::max()))
		{
			return false;
		}
		std::error_code error;
		file::create_directories(destination.parent_path(), error);
		if (error)
		{
			Debug->LogError("Editor asset directory creation failed: " +
				destination.parent_path().string() + " (" + error.message() + ")");
			return false;
		}

		// The watcher ignores .tmp paths. Publish only after the complete payload
		// is flushed so runtime readers never observe a partial cache/image.
		const file::path temporary = destination.string() + ".tmp";
		// D3-b: 두 인코딩 모두 binary 모드로 연다. 여기서 텍스트 모드를 쓰면
		// Windows가 개행을 CRLF로 바꿔, 같은 내용을 저장할 때마다 개행이 뒤집힌다.
		// 인코딩 구분은 **무엇을 쓰는가**이지 **개행을 어떻게 쓰는가**가 아니다.
		const std::ios::openmode mode = std::ios::binary | std::ios::trunc;
		(void)encoding;
		std::ofstream output(temporary, mode);
		if (!output.is_open())
		{
			Debug->LogError("Editor asset staging file could not be opened: " +
				temporary.string());
			return false;
		}
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		output.flush();
		const bool complete = output.good();
		output.close();
		if (!complete)
		{
			Debug->LogError("Editor asset staging write was incomplete: " +
				temporary.string());
			file::remove(temporary, error);
			return false;
		}

		if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			// 잠금·권한·경로 길이가 여기서 갈린다. 실패 이유를 여기서 남기지 않으면
			// 호출부는 자산 이름밖에 모른 채 실패한다.
			Debug->LogError("Editor asset publish failed: " + destination.string() +
				" (GetLastError=" + std::to_string(::GetLastError()) + ")");
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
		std::string error;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseFile(metaPath.string(), error);
		if (!document) return {};
		Authoring::ReadNode guid = document.Root()["assetId"];
		if (!guid || !guid.IsScalar()) guid = document.Root()["guid"];
		return guid && guid.IsScalar() ? FileGuid(guid.AsString()) : FileGuid{};
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

	FileGuid LoadInitialIdentityHint(const file::path& targetFile) const
	{
		const std::string extension = ToLower(targetFile.extension().string());
		const bool prefab = extension == ".prefab";
		const bool material = extension == ".asset"
			&& ToLower(targetFile.parent_path().filename().string()) == "materials";
		if (!prefab && !material) return {};

		std::string error;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseFile(targetFile.string(), error);
		if (!document) return {};
		const Authoring::ReadNode identity = document.Root()["m_fileGuid"];
		if (!identity || !identity.IsScalar()) return {};
		const FileGuid hint(identity.AsString());
		return hint.IsRandomV4() ? hint : FileGuid{};
	}

	FileGuid ResolveOrCreateGuid(const file::path& targetFile,
		Authoring::WriteNode root,
		const FileGuid& preferredGuid) const
	{
		// sidecar가 생긴 뒤에는 이 값만 정본이다. payload의 m_fileGuid를
		// 다시 읽어 sidecar를 덮는 경로는 두지 않는다.
		const Authoring::ReadNode existingGuid = root.Read()["guid"];
		if (existingGuid && existingGuid.IsScalar())
		{
			const FileGuid existing(existingGuid.AsString());
			return existing;
		}

		if (preferredGuid != FileGuid{} && !preferredGuid.IsRandomV4()) return {};

		// 새 sidecar의 첫 생성만 예외다. SavePrefab/SaveMaterial이 본문에 먼저
		// 기록한 UUIDv4 mirror를 watcher도 같은 authoring handshake로 채택한다.
		// 이 분기가 없으면 본문 close와 명시 CreateMeta 사이에서 watcher가 이겨
		// 서로 다른 UUIDv4 두 개가 생긴다. sidecar가 생긴 뒤에는 위 분기만 탄다.
		const FileGuid payloadHint = LoadInitialIdentityHint(targetFile);
		const FileGuid generated = preferredGuid != FileGuid{} ? preferredGuid
			: payloadHint != FileGuid{} ? payloadHint : FileGuid::CreateRandomV4();
		root.Child("guid").SetScalar(generated.ToString());
		return generated;
	}

	FileGuid CreateMetaLocked(const file::path& targetFile,
		const FileGuid& preferredGuid = {})
	{
		if (targetFile.empty() || !file::exists(targetFile)) return {};
		if (assets::IsModelAuthoringSource(targetFile))
		{
			if (preferredGuid != FileGuid{})
			{
				Debug->LogError("Model authoring rejects caller-supplied legacy identity: "
					+ targetFile.string());
				return {};
			}
			assets::ModelAssetAuthoringRequest request;
			request.assetRoot = m_root;
			request.sourcePath = targetFile;
			request.identityHeaderPath = m_root.parent_path()
				/ "ProjectSetting" / "AssetIdentity.asset";
			request.generationRoot = m_root.parent_path()
				/ "Library" / "ModelAssetGenerations";
			const assets::ModelAssetAuthoringResult result =
				assets::AuthorModelAsset(request);
			if (!result.Succeeded())
			{
				for (const assets::ModelAssetAuthoringIssue& issue : result.issues)
					Debug->LogError("Model authoring failed [" + issue.stage + "]: "
						+ issue.message + " (" + targetFile.string() + ")");
				return {};
			}
			const FileGuid guid(result.modelAssetId);
			DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
				RuntimeAssetType::Model, guid, targetFile });
			return guid;
		}

		const file::path metaPath = targetFile.string() + ".meta";
		Authoring::WriteDocument document;
		if (file::exists(metaPath))
		{
			std::string parseError;
			std::optional<Authoring::WriteDocument> parsed =
				Authoring::WriteDocument::ParseFile(metaPath, &parseError);
			if (!parsed || !parsed->Root().Read().IsMap())
			{
				Debug->LogError("Editor meta parse failed: " + metaPath.string()
					+ " — " + parseError);
				return {};
			}
			document = std::move(*parsed);
		}
		const Authoring::WriteNode root = document.Root();
		root.SetMap();

		const FileGuid guid = ResolveOrCreateGuid(targetFile, root, preferredGuid);
		if (guid == FileGuid{}) return {};
		if (preferredGuid != FileGuid{} && guid != preferredGuid)
		{
			Debug->LogError("Editor asset identity disagrees with canonical sidecar: "
				+ targetFile.string() + " requested=" + preferredGuid.ToString()
				+ " canonical=" + guid.ToString());
			return guid;
		}
		const Authoring::WriteNode importSettings = root.Child("importSettings");
		importSettings.Child("extension").SetScalar(
			targetFile.extension().string());
		std::error_code timestampError;
		const auto timestamp = file::last_write_time(targetFile, timestampError);
		importSettings.Child("timestamp").SetScalar(timestampError
			? 0 : timestamp.time_since_epoch().count());

		const std::string extension = ToLower(targetFile.extension().string());
		if (extension == ".cpp")
		{
			root.RemoveChild("reflectionFlag");
			file::path header = targetFile;
			header.replace_extension(".h");
			root.Child("reflectionFlag").SetScalar(file::exists(header) &&
				HasScriptReflectionFieldAttribute(header));
			root.RemoveChild("eventRegisterSetting");
			Authoring::WriteNode eventSettings;
			for (const std::string& function : ExtractFunctionNames(targetFile))
			{
				if (!eventSettings)
				{
					eventSettings = root.Child("eventRegisterSetting");
					eventSettings.SetSequence();
				}
				eventSettings.Append().SetScalar(function);
			}
		}
		// D3-b: 저작 텍스트는 LF로 쓴다. Windows의 텍스트 모드는 개행을 CRLF로 바꾼다.
		std::ofstream output(metaPath, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) return {};
		output << document.Dump();
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

		if (assets::IsModelAuthoringSource(newPath)) CreateMeta(newPath);
		else if (file::exists(newMeta)) RegisterMetaFile(newMeta);
		else CreateMeta(newPath);
	}

	// 본문이 아직 살아 있으면 이 Delete 알림은 삭제가 아니라 **게시**다.
	//
	// PublishTextAssetLocked는 `.tmp` staging -> 원자적 교체로 끝나는데, 그 교체를
	// 워처가 목적지 경로의 Delete로 받는다(2026-08-30 실측: 정상 실행 한 판에서
	// DupProbe.prefab.meta가 스스로 두 번, 각각 ~26ms 사라졌다 — 본문은 내내
	// 존재했다). 그때 catalog 항목을 떨어뜨리면 그 창에 GetFileGuid가 널을
	// 돌려주고, 그 널이 다음으로 번진다.
	//
	//   LoadPrefab이 살아 있는 identity를 널로 덮는다
	//     -> SavePrefab이 널을 보고 CreateRandomV4()로 새 GUID를 발급한다
	//       -> UpdateInstances가 그 새 키로 조회해 **조용히 0건 적용**한다
	//
	// 에러도 로그도 없이 인스턴스 값만 옛것으로 남는다. verify-prefab-duplicate가
	// 2026-08-30에 한 번 그 창에 걸렸고, 창이 좁아 재현되지 않았다.
	// verify-prefab-identity-injection이 그 교란을 확정적으로 재현한다.
	bool TargetStillExists(const file::path& targetPath) const
	{
		std::error_code error;
		return file::exists(targetPath, error) && !error;
	}

	void HandleDeleted(const file::path& deletedPath)
	{
		if (deletedPath.extension() == ".meta")
		{
			// sidecar만 사라졌다면 본문의 identity를 버릴 이유가 없다. 다음
			// 저작 게시가 sidecar를 같은 GUID로 다시 쓴다.
			const file::path targetPath = RemoveMetaExtension(deletedPath);
			if (TargetStillExists(targetPath)) return;

			DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::Removed,
				RuntimeAssetType::Auto, {}, targetPath });
			return;
		}

		if (TargetStillExists(deletedPath)) return;

		// ★ ApplyAssetChange가 아니라 QueueAssetChange다. 이 함수는 efsw I/O
		// 스레드에서 돈다 — DataSystem.h의 계약("Watcher I/O thread는 cache/catalog를
		// 직접 바꾸지 않고 이 큐에 게시한다")대로 프레임 경계로 넘긴다.
		// HandleModified는 이미 그렇게 하고 있었고 여기만 어긋나 있었다.
		DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::Removed,
			RuntimeAssetType::Auto, {}, deletedPath });
		const file::path metaPath = deletedPath.string() + ".meta";
		std::error_code error;
		file::remove(metaPath, error);
		if (error)
			Debug->LogWarning("Failed to remove deleted asset meta: " +
				error.message());
	}

	void HandleModified(const file::path& filepath)
	{
		if (assets::IsModelAuthoringSource(filepath))
		{
			CreateMeta(filepath);
			return;
		}
		// M5-C3a는 generation 계약이 이미 있는 ShaderMeta만 연다. HLSL include
		// dependency와 다른 asset cache의 reload 정책은 같은 이벤트라는 이유로
		// 추측해 넓히지 않는다.
		if (ToLower(filepath.extension().string()) != ".shadermeta") return;

		FileGuid guid = DataSystems->GetFileGuid(filepath);
		if (guid == FileGuid{})
		{
			const file::path metaPath = filepath.string() + ".meta";
			if (file::exists(metaPath)) guid = LoadGuidFromMeta(metaPath);
		}
		if (guid == FileGuid{}) return;

		DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::ContentReload,
			RuntimeAssetType::ShaderMeta, guid, filepath });
	}

	file::path m_root;
	std::mutex m_authoringMutex;
	std::unique_ptr<efsw::FileWatcher> m_watcher;
	const std::unordered_set<std::string> m_registeredFiles{
		".fbx", ".gltf", ".obj", ".glb",
		".png", ".dds", ".jpg", ".jpeg", ".hdr",
		".hlsl", ".shadermeta", ".shader", ".cpp", ".cs",
		".wav", ".mp3", ".ogg", ".spritefont",
		".terrain", ".bt", ".blackboard", ".prefab", ".volume",
		// ★ `.creator`(씬)가 빠져 있었다. `.prefab` 은 있는데 씬만 없어서
		//   씬 14개가 sidecar 를 하나도 갖지 못했고, 그래서 **asset identity
		//   자체가 없었다** — 지금은 경로로만 참조된다. D5-c 의 "Player 가
		//   `.meta` 나 source path 탐색 없이 scene 을 해석한다"는 조회할 GUID 가
		//   없어 성립할 수 없었다(D5-b2c-4 실측).
		//
		//   `Tools/migration/Repair-AssetSidecarIdentities.ps1` 이 기존 14개를
		//   백필했고, 여기 등록으로 새 씬도 저장 시 sidecar 를 받는다. 두 곳
		//   중 하나만 고치면 백필이 되거나 신규가 되거나 둘 중 하나만 산다.
		".creator",
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
	AssetAuthoringPort::InstallTextAssetWriter(
		&WriteTextAssetWithMetaThroughEditor);
	AssetAuthoringPort::InstallModelCacheWriter(&WriteModelCacheThroughEditor);
	AssetAuthoringPort::InstallEmbeddedTextureWriter(
		&WriteEmbeddedTextureThroughEditor);
	AssetAuthoringPort::InstallTerrainWriter(&WriteTerrainThroughEditor);
	AssetAuthoringPort::InstallFoliageWriter(&WriteFoliageThroughEditor);
	AssetAuthoringPort::InstallBlackBoardWriter(&WriteBlackBoardThroughEditor);
	AssetAuthoringPort::InstallCollisionMatrixWriter(
		&WriteCollisionMatrixThroughEditor);
	AssetAuthoringPort::InstallTagManagerWriter(&WriteTagManagerThroughEditor);
	AssetAuthoringPort::InstallInputActionMapWriter(
		&WriteInputActionMapThroughEditor);
	return true;
}

void EditorAssetDatabase::Shutdown() noexcept
{
	AssetAuthoringPort::UninstallInputActionMapWriter(
		&WriteInputActionMapThroughEditor);
	AssetAuthoringPort::UninstallTagManagerWriter(&WriteTagManagerThroughEditor);
	AssetAuthoringPort::UninstallCollisionMatrixWriter(
		&WriteCollisionMatrixThroughEditor);
	AssetAuthoringPort::UninstallBlackBoardWriter(&WriteBlackBoardThroughEditor);
	AssetAuthoringPort::UninstallFoliageWriter(&WriteFoliageThroughEditor);
	AssetAuthoringPort::UninstallTerrainWriter(&WriteTerrainThroughEditor);
	AssetAuthoringPort::UninstallEmbeddedTextureWriter(
		&WriteEmbeddedTextureThroughEditor);
	AssetAuthoringPort::UninstallModelCacheWriter(&WriteModelCacheThroughEditor);
	AssetAuthoringPort::UninstallTextAssetWriter(
		&WriteTextAssetWithMetaThroughEditor);
	AssetAuthoringPort::Uninstall(&CreateMetaThroughEditor);
	if (m_impl) m_impl->Stop();
	m_impl.reset();
}

bool EditorAssetDatabase::IsInitialized() const noexcept
{
	return nullptr != m_impl;
}

FileGuid EditorAssetDatabase::CreateMeta(const file::path& filepath,
	const FileGuid& preferredGuid)
{
	return m_impl ? m_impl->CreateMeta(filepath, preferredGuid) : FileGuid{};
}

FileGuid EditorAssetDatabase::WriteTextAssetWithMeta(
	const file::path& destination, std::string_view payload,
	const FileGuid& preferredGuid)
{
	return m_impl
		? m_impl->WriteTextAssetWithMeta(destination, payload, preferredGuid)
		: FileGuid{};
}

FileGuid EditorAssetDatabase::RenameAsset(const file::path& source,
	const file::path& destination)
{
	return m_impl ? m_impl->RenameAsset(source, destination) : FileGuid{};
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

bool EditorAssetDatabase::WriteFoliage(const TextAssetAuthoringRequest& request,
	TextAssetAuthoringResult& result)
{
	return m_impl && m_impl->WriteFoliage(request, result);
}

bool EditorAssetDatabase::WriteBlackBoard(
	const TextAssetAuthoringRequest& request, TextAssetAuthoringResult& result)
{
	return m_impl && m_impl->WriteBlackBoard(request, result);
}

bool EditorAssetDatabase::WriteCollisionMatrix(
	const UncatalogedAuthoringRequest& request)
{
	return m_impl && m_impl->WriteCollisionMatrix(request);
}

bool EditorAssetDatabase::WriteTagManager(
	const UncatalogedAuthoringRequest& request)
{
	return m_impl && m_impl->WriteTagManager(request);
}

bool EditorAssetDatabase::WriteInputActionMap(
	const UncatalogedAuthoringRequest& request)
{
	return m_impl && m_impl->WriteInputActionMap(request);
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
	const FileGuid catalogGuid = DataSystems->GetFileGuid(savePath);
	if (catalogGuid != FileGuid{}) material->m_fileGuid = catalogGuid;
	else if (material->m_fileGuid == FileGuid{})
		material->m_fileGuid = FileGuid::CreateRandomV4();
	Authoring::WriteDocument document;
	if (!DataSystems->SerializeMaterialPayload(*material, document.Root()))
		return false;
	return WriteTextAssetWithMeta(savePath, document.Dump(), material->m_fileGuid)
		== material->m_fileGuid;
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

	// D3-b: 저작 텍스트는 LF로 쓴다. Windows의 텍스트 모드는 개행을 CRLF로 바꾼다.
	std::ofstream output(fullPath, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) return false;
	output << Meta::SerializeDocument(&profile).Dump();
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

	// D3-b: 저작 텍스트는 LF로 쓴다. Windows의 텍스트 모드는 개행을 CRLF로 바꾼다.
	std::ofstream output(savePath, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) return false;
	output << Meta::SerializeDocument(volume).Dump();
	output.flush();
	return output.good();
}
