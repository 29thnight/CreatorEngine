#include "../../Editor/EngineEntry/EditorAssetDatabase.h"
#include "DataSystem.h"
#include "TextureCodecImage.h"
#include "Experiment/Cooked/TextureImportSettings.h"
#include "PathFinder.h"
#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

// The Editor archive also contains UI consumers. This host exercises only the
// asset service; reaching presentation would invalidate its headless scope.
class IImGuiHost;
IImGuiHost& GetImGuiHost()
{
    throw std::runtime_error("Headless import probe reached ImGui presentation");
}

namespace
{
    unsigned checks{};
    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    void Write(const file::path& path, std::string_view text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
        Require(bool(stream), "fixture write failed");
    }
    std::string Read(const file::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), {} };
    }
    void Await(EditorAssetDatabase& editor, DataSystem& data, const file::path& source, bool success)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline)
        {
            data.DrainQueuedAssetChanges();
            const auto status = editor.TextureImportStatus(source);
            if (success && editor.TextureImportReady(source))
            {
                return;
            }
            if (!success && status.starts_with("Import failed; previous generation retained:"))
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("texture import timeout: " + editor.TextureImportStatus(source));
    }
    void Run(const file::path& root, const file::path& input)
    {
        EnginePaths paths;
        paths.executableRoot = root;
        paths.projectRoot = root;
        paths.runtimeContentRoot = root;
        paths.runtimeDataRoot = root / "RuntimeData";
        paths.assetsRoot = root / "Assets";
        paths.enableAssetAuthoring = true;
        Require(InternalPath::GetInstance()->Initialize(paths), "path initialization failed");
        const auto source = file::weakly_canonical(paths.assetsRoot) / "Textures" / "fixture.png";
        const file::path sidecar = source.string() + ".meta";
        file::copy_file(input, source);
        const auto original = Read(source);
        const FileGuid guid("22222222-2222-4222-8222-222222222222");
        Write(sidecar, "guid: 22222222-2222-4222-8222-222222222222\ncustomField: retained\n"
            "importSettings:\n  extension: .png\n  wrap: Repeat\n");
        auto& data = *DataSystem::GetInstance();
        data.Initialize();
        auto& editor = EditorAssetDatabase::Get();
        Require(editor.Initialize(), "editor asset database initialization failed");
        const AssetDepot::AssetLink<Texture> link{ { experiment::AssetId{ guid.m_guid }, {} } };
        experiment::cooked::TextureImportSettings settings;
        settings.colorSpace = experiment::cooked::TextureColorSpace::Linear;
        std::string failure;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "initial reimport rejected");
        Await(editor, data, source, true);
        const auto first = data.TryAcquire<Texture>(link);
        Require(bool(first), "initial description unavailable");
        auto firstImage = data.TryAcquire<Texture::CodecImage>(first);
        Require(bool(firstImage), "initial image unavailable");
        const auto firstWidth = first->GetImageView(firstImage).Width();
        Require(firstWidth > 2u, "fixture must be wider than resize target");
        const auto meta = Read(sidecar);
        Require(meta.find("customField: retained") != std::string::npos, "custom metadata lost");
        Require(meta.find(guid.ToString()) != std::string::npos, "GUID changed");
        Require(meta.find("extension: .png") != std::string::npos, "extension metadata lost");
        Require(meta.find("wrap: Repeat") != std::string::npos, "reference sampler metadata lost");

        settings.maxDimension = 2u;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "resize reimport rejected");
        Await(editor, data, source, true);
        const auto second = data.TryAcquire<Texture>(link);
        auto secondImage = data.TryAcquire<Texture::CodecImage>(second);
        Require(second && secondImage && second->GetImageView(secondImage).Width() == 2u, "new recipe not applied");
        Require(&*first != &*second, "new generation reused prior description");
        Require(first->GetImageView(firstImage).Width() == firstWidth, "old strong owner changed");

        const auto beforeInvalid = Read(sidecar);
        auto invalid = settings;
        invalid.compression = static_cast<experiment::cooked::TextureCompression>(255u);
        Require(!editor.SetTextureImportSettingsAndReimport(source, invalid, failure), "invalid enum accepted");
        Require(Read(sidecar) == beforeInvalid, "invalid setting modified sidecar");
        Require(data.TryAcquire<Texture>(link) && &*data.TryAcquire<Texture>(link) == &*second,
            "invalid setting displaced accepted generation");

        Write(source, "broken png");
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "broken source was not scheduled");
        Await(editor, data, source, false);
        const auto retained = data.TryAcquire<Texture>(link);
        Require(retained && &*retained == &*second, "failed cook displaced accepted generation");
        Require(second->GetImageView(secondImage).Width() == 2u, "failed cook damaged old pixels");

        Write(source, original);
        settings.maxDimension = 0u;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "recovery reimport rejected");
        Await(editor, data, source, true);
        const auto recovered = data.TryAcquire<Texture>(link);
        const auto recoveredImage = data.TryAcquire<Texture::CodecImage>(recovered);
        Require(recovered && recoveredImage && recovered->GetImageView(recoveredImage).Width() == firstWidth,
            "valid retry did not recover original size");

        // Supersede a queued recipe on the same game-thread boundary. Worker
        // completion order must not install the intermediate 2px generation.
        settings.maxDimension = 2u;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "superseded request rejected");
        settings.maxDimension = 0u;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "latest request rejected");
        Await(editor, data, source, true);
        const auto latest = data.TryAcquire<Texture>(link);
        const auto latestImage = data.TryAcquire<Texture::CodecImage>(latest);
        Require(latest && latestImage && latest->GetImageView(latestImage).Width() == firstWidth,
            "superseded recipe published");
        Require(first->GetImageView(firstImage).Width() == firstWidth && second->GetImageView(secondImage).Width() == 2u,
            "accepted old owners did not survive later generations");

        data.SetTextureImageCacheBudget(0u);
        firstImage.reset();
        secondImage.reset();
        Require(!data.TryAcquire<Texture::CodecImage>(second),
            "old image remained resident; rehydration would not be exercised");
        auto oldRequest = data.RequestAsync<Texture::CodecImage>(second);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (oldRequest.Snapshot().status == AssetDepot::AssetRequestStatus::Pending
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto oldImage = oldRequest.Snapshot();
        Require(oldImage.status == AssetDepot::AssetRequestStatus::Ready && oldImage.asset
            && second->GetImageView(oldImage.asset).Width() == 2u,
            "evicted old generation could not rehydrate its exact artifact");

        data.SetTextureImageCacheBudget(1024u * 1024u);
        settings.maxDimension = 2u;
        Require(editor.SetTextureImportSettingsAndReimport(source, settings, failure), "pre-rename request rejected");
        const auto moved = source.parent_path() / "moved.png";
        Require(editor.RenameAsset(source, moved) == guid, "rename changed GUID or failed");
        Await(editor, data, moved, true);
        const auto movedTexture = data.TryAcquire<Texture>(link);
        const auto movedImage = data.TryAcquire<Texture::CodecImage>(movedTexture);
        Require(movedTexture && movedImage && movedTexture->GetImageView(movedImage).Width() == 2u,
            "rename did not publish the current recipe");
        Require(!file::exists(source) && !file::exists(sidecar) && file::exists(moved)
            && Read(moved.string() + ".meta").find(guid.ToString()) != std::string::npos,
            "rename did not preserve the exact source/sidecar pair");
        Require(second->GetImageView(oldImage.asset).Width() == 2u,
            "rename invalidated an accepted old owner");
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
    {
        return 3;
    }
    int result{};
    try
    {
        ce::get_job_scheduler().start(2u);
        Run(file::absolute(argv[1]), file::absolute(argv[2]));
        std::cout << "TEXTURE_IMPORT_LIFECYCLE_OK checks=" << checks << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    EditorAssetDatabase::Get().Shutdown();
    ce::get_job_scheduler().shutdown();
    DataSystem::GetInstance()->Finalize();
    DirectX::SetWICFactory(nullptr);
    CoUninitialize();
    return result;
}
