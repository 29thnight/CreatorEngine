#include "../../Editor/EngineEntry/EditorAssetDatabase.h"
#include "../../Engine/Utility_Framework/WindowsApartment.h"
#include "../../Editor/EngineEntry/ConditionalAssetPublish.h"
#include "DataSystem.h"
#include "TextureCodecImage.h"
#include "Experiment/Cooked/TextureImportSettings.h"
#include "PathFinder.h"
#include "SceneRenderProfile.h"
#include "ReflectionYml.h"
#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>
#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace reflgen::generated
{
    void register_RenderEngine(reflgen::registry& target);
}

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
    bool WriteExternal(const file::path& path, std::string_view text, std::atomic<bool>& blocked)
    {
        Editor::AuthoringPublish::FileHandle output(::CreateFileW(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.Get() == INVALID_HANDLE_VALUE)
        {
            blocked = ::GetLastError() == ERROR_SHARING_VIOLATION;
            return false;
        }
        DWORD written{};
        return ::WriteFile(output.Get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
            && written == text.size() && ::FlushFileBuffers(output.Get());
    }

    void RunConditionalPublisher(const file::path& directory)
    {
        const auto path = directory / "conditional.fixture";
        const file::path staging = path.string() + ".tmp";
        const auto backup = Editor::AuthoringPublish::RecoveryPath(path);
        std::string failure;
        Write(path, "original");
        Write(staging, "draft");
        bool blocked{};
        const bool published = Editor::AuthoringPublish::Publish(path, staging, "original", failure, [&]
        {
            Editor::AuthoringPublish::FileHandle attempt(::CreateFileW(backup.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr));
            blocked = attempt.Get() == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_SHARING_VIOLATION;
            Write(path, "foreign");
        });
        Require(blocked, "retired original was writable during the final publication boundary");
        Require(!published && Read(path) == "foreign", "final-boundary foreign creation overwritten");
        Require(!file::exists(backup), "completed conflict recovery copy retained");
        Write(path, "original");
        Require(Editor::AuthoringPublish::Publish(path, staging, "original", failure),
            "conditional normal publication failed");
        Require(Read(path) == "draft" && !file::exists(staging) && !file::exists(backup),
            "conditional publication did not commit the complete payload");
        Write(path, "original");
        Require(!Editor::AuthoringPublish::Publish(path, staging, "original", failure),
            "missing staged file publication accepted");
        Require(Read(path) == "original" && !file::exists(backup), "publish failure did not restore original");
        Write(staging, "draft");
        bool threw{};
        try
        {
            Editor::AuthoringPublish::Publish(path, staging, "original", failure, []
            {
                throw std::runtime_error("controlled boundary exception");
            });
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        Require(threw && Read(path) == "original" && !file::exists(backup),
            "boundary exception lost original");
        file::remove(staging);
        file::rename(path, backup);
        Require(Editor::AuthoringPublish::Recover(path, failure) && Read(path) == "original",
            "missing-path recovery failed");
        Write(backup, "old journal");
        Require(Editor::AuthoringPublish::Recover(path, failure) && Read(path) == "original"
            && !file::exists(backup), "recovery overwrote an existing destination");
        std::cout << "CONDITIONAL_PUBLISH_BOUNDARY_OK\n";
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
    void RunRenderProfile(EditorAssetDatabase& editor, DataSystem& data, const file::path& directory)
    {
        reflgen::generated::register_RenderEngine(Meta::Types());
        const auto path = directory / "fixture.renderprofile";
        const file::path meta = path.string() + ".meta";
        const file::path staging = path.string() + ".tmp";
        const FileGuid identity("55555555-5555-4555-8555-555555555555");
        SceneRenderProfile profile;
        const auto baseline = Meta::SerializeDocument(&profile).Dump();
        Require(editor.WriteTextAssetWithMeta(path, baseline, identity) == identity,
            "RenderProfile fixture publication failed");
        data.DrainQueuedAssetChanges();
        Require(data.GetFilePath(identity).lexically_normal() == path.lexically_normal(),
            "RenderProfile identity did not resolve its path");
        profile.settings.m_windStrength = 0.4f;
        const auto recovered = Meta::SerializeDocument(&profile).Dump();
        for (const bool staged : { false, true })
        {
            for (const int fault : { 0, 1, 2 })
            {
                Write(path, baseline);
                Require(!file::exists(staging), "RenderProfile stale staging file");
                auto draft = profile;
                if (staged)
                {
                    draft.settings.skyboxTextureName = std::string(16u * 1024u * 1024u, 'x');
                }
                const std::string external = fault == 2 ? "settings: [\n" : recovered;
                std::atomic<bool> observed{}, injected{}, blocked{};
                const auto inject = [&]
                {
                    if (fault == 1)
                    {
                        injected = ::DeleteFileW(path.c_str()) != FALSE;
                        if (!injected)
                        {
                            const auto error = ::GetLastError();
                            blocked = error == ERROR_SHARING_VIOLATION || (staged && error == ERROR_FILE_NOT_FOUND);
                        }
                    }
                    else
                    {
                        injected = WriteExternal(path, external, blocked);
                    }
                };
                editor.QueueAutomaticRenderProfile(identity, draft);
                Require(editor.PendingAutomaticSaveAge(path).has_value(), "RenderProfile save was not queued");
                std::jthread writer;
                if (staged)
                {
                    writer = std::jthread([&](std::stop_token stop)
                    {
                        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                        while (!stop.stop_requested() && std::chrono::steady_clock::now() < timeout)
                        {
                            std::error_code error;
                            if (file::exists(staging, error))
                            {
                                observed = true;
                                inject();
                                return;
                            }
                            std::this_thread::yield();
                        }
                    });
                }
                else
                {
                    observed = true;
                    inject();
                }
                editor.FlushAutomaticSaves();
                if (writer.joinable())
                {
                    writer.request_stop();
                    writer.join();
                }
                Require(observed && (injected || blocked), "RenderProfile fault boundary was not observed");
                Require(injected ? (fault == 1 ? !file::exists(path) : Read(path) == external)
                    : Read(path) == Meta::SerializeDocument(&draft).Dump(),
                    "RenderProfile external state overwritten");
                Require(injected ? editor.AutomaticSaveError(path).find("changed on disk") != std::string::npos
                    : editor.AutomaticSaveError(path).empty(),
                    "RenderProfile conflict diagnostic missing");
                Require(!editor.PendingAutomaticSaveAge(path), "RenderProfile pending save retained");
                Require(!file::exists(staging), "RenderProfile rejected staging retained");
                if (fault == 1 && injected)
                {
                    // Deleting the asset body intentionally removes its sidecar.
                    // Finish that watcher event before explicitly restoring the pair.
                    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    bool removed{};
                    while (std::chrono::steady_clock::now() < timeout)
                    {
                        std::error_code error;
                        if (!file::exists(meta, error) && !error)
                        {
                            removed = true;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    Require(removed, "RenderProfile deleted asset sidecar retained");
                    data.DrainQueuedAssetChanges();
                }
                else if (fault == 1)
                {
                    Require(file::exists(meta), "rejected deletion removed GUID metadata");
                }
                const auto metadata = Authoring::WriteDocument::ParseFile(meta);
                Require((fault == 1 && injected) || (metadata && metadata->Root()["guid"].AsString() == identity.ToString()),
                    "RenderProfile GUID changed");
                Require(editor.WriteTextAssetWithMeta(path, baseline, identity) == identity,
                    "RenderProfile explicit pair restore failed");
                editor.QueueAutomaticRenderProfile(identity, profile);
                editor.FlushAutomaticSaves();
                Require(editor.AutomaticSaveError(path).empty() && Read(path) == recovered,
                    "RenderProfile new edit did not recover");
                std::cout << "RENDER_PROFILE_CONFLICT_OK staged=" << staged << " fault=" << fault
                    << " externalBlocked=" << blocked << '\n';
            }
        }
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

        const auto sidecarBeforeFailures = Read(sidecar);
        const auto lockedSidecar = CreateFileW(sidecar.c_str(), GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(lockedSidecar != INVALID_HANDLE_VALUE, "sidecar lock fixture failed");
        const bool lockedAccepted = editor.SetTextureImportSettingsAndReimport(source, settings, failure);
        CloseHandle(lockedSidecar);
        Require(!lockedAccepted, "locked sidecar accepted");
        Require(failure.find("failed to open authoring document") != std::string::npos,
            "locked sidecar read error replaced by identity error");
        Require(Read(sidecar) == sidecarBeforeFailures, "locked sidecar was changed");

        const auto rejectSidecar = [&](std::string_view text, std::string_view diagnostic)
        {
            Write(sidecar, text);
            Require(!editor.SetTextureImportSettingsAndReimport(source, settings, failure),
                "invalid sidecar accepted");
            Require(failure.find(diagnostic) != std::string::npos, "incorrect sidecar diagnostic");
            Require(Read(sidecar) == text, "rejected sidecar overwritten");
            Require(data.TryAcquire<Texture>(link) && &*data.TryAcquire<Texture>(link) == &*second,
                "sidecar rejection displaced accepted generation");
        };
        rejectSidecar("guid: 22222222-2222-4222-8222-222222222222\nimportSettings: [\n", "[parse]");
        rejectSidecar("[]\n", "must contain a mapping");
        rejectSidecar("guid: invalid\n", "canonical UUIDv4 identity");
        file::remove(sidecar);
        Require(!editor.SetTextureImportSettingsAndReimport(source, settings, failure),
            "missing sidecar accepted");
        Require(failure.find("failed to open authoring document") != std::string::npos,
            "missing sidecar read error replaced by identity error");
        Require(!file::exists(sidecar), "missing sidecar silently recreated");
        Write(sidecar, sidecarBeforeFailures);

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

        // Observe creation of the staging file: the queue's initial comparison
        // and document parsing have already completed at this boundary. A large
        // unknown field keeps the staged interval observable without a product
        // test hook. Observing .tmp does not locate the final CPU comparison.
        const auto raceBaseline = sidecarBeforeFailures + "racePadding: " + std::string(16u * 1024u * 1024u, 'x') + "\n";
        const std::string external = "guid: 22222222-2222-4222-8222-222222222222\nexternalField: retained\n"
            "importSettings:\n  maxDimension: 1\n";
        const file::path staging = sidecar.string() + ".tmp";
        for (const bool genericMetadata : { false, true })
        {
            const auto raceOwner = data.TryAcquire<Texture>(link);
            Write(sidecar, raceBaseline);
            Require(!file::exists(staging), "stale staging file before race");
            std::atomic<bool> observed{}, injected{}, blocked{};
            std::jthread writer([&](std::stop_token stop)
            {
                const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < timeout)
                {
                    std::error_code error;
                    if (file::exists(staging, error))
                    {
                        observed = true;
                        injected = WriteExternal(sidecar, external, blocked);
                        return;
                    }
                    std::this_thread::yield();
                }
            });
            if (genericMetadata)
            {
                editor.QueueAutomaticImportMetadata(source, raceBaseline);
            }
            else
            {
                editor.QueueAutomaticTextureSettings(source, settings);
            }
            editor.FlushAutomaticSaves();
            writer.request_stop();
            writer.join();
            Require(observed && (injected || blocked), "external write did not observe the staging file");
            Require(injected ? Read(sidecar) == external : Read(sidecar).size() >= 16u * 1024u * 1024u,
                "staging race overwrote external metadata");
            Require(injected ? editor.AutomaticSaveError(sidecar).find("changed on disk") != std::string::npos
                : editor.AutomaticSaveError(sidecar).empty(),
                "staging conflict diagnostic missing");
            Require(!file::exists(staging), "rejected staging file retained");
            Require(Read(source) == original, "staging conflict changed PNG");
            Require(data.TryAcquire<Texture>(link) && &*data.TryAcquire<Texture>(link) == &*raceOwner,
                "staging conflict displaced accepted generation");
            Write(sidecar, sidecarBeforeFailures);
            editor.QueueAutomaticTextureSettings(source, settings);
            editor.FlushAutomaticSaves();
            Require(editor.AutomaticSaveError(sidecar).empty(), "new edit retained staging conflict error");
            Await(editor, data, source, true);
            std::cout << "TEXTURE_STAGING_RACE_OK kind=" << (genericMetadata ? "metadata" : "texture") << '\n';
        }

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

        RunRenderProfile(editor, data, moved.parent_path());
        RunConditionalPublisher(moved.parent_path());
    }

    void RunCacheRecovery(const file::path& root)
    {
        auto& data = *DataSystem::GetInstance();
        auto& editor = EditorAssetDatabase::Get();
        const auto moved = root / "Assets" / "Textures" / "moved.png";
        const FileGuid guid("22222222-2222-4222-8222-222222222222");
        const AssetDepot::AssetLink<Texture> link{ { experiment::AssetId{ guid.m_guid }, {} } };
        experiment::cooked::TextureImportSettings settings;
        settings.colorSpace = experiment::cooked::TextureColorSpace::Linear;
        const auto firstWidth = 4u;
        std::string failure;
        // Exercise disk-cache recovery through the real Editor importer. Both
        // recipes already have nonempty content-addressed directories, so a
        // plain staging rename cannot repair either a record or artifact fault.
        const auto generations = root / "Library" / "TextureAssetGenerations";
        std::vector<std::pair<file::path, std::string>> records;
        std::vector<std::pair<file::path, std::string>> artifacts;
        for (const auto& entry : file::directory_iterator(generations))
        {
            const auto recordPath = entry.path() / "generation.record";
            if (!file::exists(recordPath)) continue;
            records.emplace_back(recordPath, Read(recordPath));
            std::ifstream record(recordPath);
            std::string line;
            std::getline(record, line); // magic
            std::getline(record, line); // texture identity
            std::getline(record, line); // artifact path
            const auto artifactPath = entry.path() / line;
            artifacts.emplace_back(artifactPath, Read(artifactPath));
        }
        Require(records.size() == 2u, "expected two immutable recipe generations");
        for (const auto* faults : {&records, &artifacts})
        {
            for (const auto& [path, bytes] : *faults) Write(path, "damaged cache");
            data.Initialize();
            Require(editor.Initialize(), "cache recovery editor initialization failed");
            Await(editor, data, moved, true);
            for (const auto dimension : {0u, 2u})
            {
                settings.maxDimension = dimension;
                Require(editor.SetTextureImportSettingsAndReimport(moved, settings, failure),
                    "damaged cache recovery request rejected");
                Await(editor, data, moved, true);
                const auto texture = data.TryAcquire<Texture>(link);
                const auto image = data.TryAcquire<Texture::CodecImage>(texture);
                Require(texture && image && texture->GetImageView(image).Width() ==
                    (dimension ? dimension : firstWidth), "damaged cache recovery pixels differ");
            }
            for (const auto& [path, bytes] : records)
                Require(Read(path) == bytes, "damaged record was not restored exactly");
            for (const auto& [path, bytes] : artifacts)
                Require(Read(path) == bytes, "damaged artifact was not restored exactly");
            for (const auto& entry : file::directory_iterator(generations))
                Require(!entry.path().filename().string().starts_with("stage-"),
                    "repair left a staging or quarantine directory");
            editor.Shutdown();
            data.Finalize();
        }
    }

    void InterruptConditional(const file::path& root)
    {
        const auto directory = root / "Assets" / "Textures";
        file::create_directories(directory);
        const auto path = directory / "interrupted.renderprofile";
        const file::path staging = path.string() + ".tmp";
        Write(path, "settings: {}\n");
        Write(path.string() + ".meta", "guid: 66666666-6666-4666-8666-666666666666\n");
        Write(staging, "settings: { skyboxTextureName: uncommitted }\n");
        std::string failure;
        Editor::AuthoringPublish::Publish(path, staging, "settings: {}\n", failure, []
        {
            ::ExitProcess(73u);
        });
        throw std::runtime_error("controlled interruption did not reach retired boundary: " + failure);
    }

    void RecoverConditional(const file::path& root)
    {
        EnginePaths paths;
        paths.executableRoot = root;
        paths.projectRoot = root;
        paths.runtimeContentRoot = root;
        paths.runtimeDataRoot = root / "RuntimeData";
        paths.assetsRoot = root / "Assets";
        paths.enableAssetAuthoring = true;
        Require(InternalPath::GetInstance()->Initialize(paths), "recovery path initialization failed");
        const auto path = paths.assetsRoot / "Textures" / "interrupted.renderprofile";
        Require(!file::exists(path) && file::exists(Editor::AuthoringPublish::RecoveryPath(path)),
            "interruption did not leave the expected recovery journal");
        auto& data = *DataSystem::GetInstance();
        data.Initialize();
        Require(EditorAssetDatabase::Get().Initialize(), "recovery Editor startup failed");
        Require(Read(path) == "settings: {}\n", "startup recovery did not restore original bytes");
        Require(!file::exists(Editor::AuthoringPublish::RecoveryPath(path)), "startup recovery journal retained");
        const FileGuid identity("66666666-6666-4666-8666-666666666666");
        Require(Read(path.string() + ".meta").find(identity.ToString()) != std::string::npos
            && data.GetFileGuid(path) == identity, "startup recovery lost GUID or catalog identity");
        std::cout << "CONDITIONAL_RECOVERY_OK checks=" << checks << '\n';
    }
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        return 2;
    }
    const ce::platform::windows::apartment_scope apartmentScope{ ce::platform::windows::apartment_model::mta };
    const HRESULT com = apartmentScope.result;
    if (FAILED(com))
    {
        return 3;
    }
    int result{};
    try
    {
        ce::get_job_scheduler().start(2u);
        if (argc == 3)
        {
            Run(file::absolute(argv[1]), file::absolute(argv[2]));
            EditorAssetDatabase::Get().Shutdown();
            DataSystem::GetInstance()->Finalize();
            RunCacheRecovery(file::absolute(argv[1]));
            std::cout << "TEXTURE_IMPORT_LIFECYCLE_OK checks=" << checks << '\n';
        }
        else if (std::string_view(argv[3]) == "--interrupt-conditional")
        {
            InterruptConditional(file::absolute(argv[1]));
        }
        else if (std::string_view(argv[3]) == "--recover-conditional")
        {
            RecoverConditional(file::absolute(argv[1]));
        }
        else
        {
            throw std::runtime_error("unknown lifecycle probe mode");
        }
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
    return result;
}
