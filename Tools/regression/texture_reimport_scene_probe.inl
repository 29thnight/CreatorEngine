// Included in the Editor asset-command namespace. This isolated product probe
// uses owner-thread polling; it never waits for cook or scene jobs on that thread.
struct TextureReimportSceneProbe final
{
    file::path source;
    file::path saved;
    own::shared_owner<const Texture> oldTexture;
    own::shared_owner<const Texture::CodecImage> oldImage;
    own::shared_owner<const Texture> newTexture;
    own::shared_owner<const Texture> oldSprite;
    own::shared_owner<const Texture::CodecImage> oldSpriteImage;
    own::shared_owner<const Texture> newSprite;
    std::chrono::steady_clock::time_point deadline;
    Scene* original{};
    Scene* replacement{};
    unsigned stage{};
    unsigned checks{};
    std::uint64_t request{};
    std::string originalBytes;
    std::size_t undoDepth{};
    Entity* prefabInstance{};
    Entity* prefabOverrideInstance{};
    RectTransformComponent* prefabEditRect{};
    file::path prefabPath;
    std::string prefabBefore;

    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    void Reimport(std::uint32_t dimension)
    {
        experiment::cooked::TextureImportSettings settings;
        settings.colorSpace = experiment::cooked::TextureColorSpace::Linear;
        settings.maxDimension = dimension;
        std::string failure;
        // Exercise a real sidecar edit: no explicit reimport or scene reload command.
        auto document = Authoring::WriteDocument::ParseFile(source.string() + ".meta", &failure);
        Check(bool(document), "Texture sidecar could not be parsed");
        EditorAssetDatabase::WriteTextureImportSettings(document->Root(), settings);
        std::ofstream output(source.string() + ".meta", std::ios::binary | std::ios::trunc);
        output << document->Dump();
        output.close();
        Check(bool(output), "Texture sidecar edit failed");
    }
    ImageComponent* Image(Scene* scene)
    {
        auto entity = scene ? scene->GetEntity("TextureGate") : nullptr;
        return entity ? entity->GetComponent<ImageComponent>() : nullptr;
    }
    SpriteRenderer* Sprite(Scene* scene)
    {
        auto entity = scene ? scene->GetEntity("TextureGate") : nullptr;
        return entity ? entity->GetComponent<SpriteRenderer>() : nullptr;
    }
    void CheckSprite(Scene* scene, const own::shared_owner<const Texture>& expected)
    {
        auto* sprite = Sprite(scene);
        Check(sprite && sprite->GetSprite() && &*sprite->GetSprite() == &*expected,
            "Sprite component generation was not retained/restored");
    }
    void Restore()
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream.write(originalBytes.data(), static_cast<std::streamsize>(originalBytes.size()));
    }
    std::optional<CommandCore::CommandResult> Poll()
    {
        using namespace CommandCore;
        using LoadState = SceneManager::SceneLoadRequestState;
        try
        {
            Check(std::chrono::steady_clock::now() < deadline, "Reimport/scene probe timed out");
            auto& editor = EditorAssetDatabase::Get();
            if (stage == 0u)
            {
                if (!editor.TextureImportReady(source))
                {
                    return std::nullopt;
                }
                original = SceneManagers->GetActiveScene();
                Check(original != nullptr, "No active fixture scene");
                oldTexture = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::UITexture);
                oldImage = DataSystems->TryAcquire<Texture::CodecImage>(oldTexture);
                if (!oldTexture || !oldImage)
                {
                    return std::nullopt; // UI role admission also completes asynchronously.
                }
                Check(oldTexture && oldImage && oldTexture->GetImageView(oldImage).Width() == 4u,
                    "Initial cooked UI texture is not 4px");
                oldSprite = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::Texture);
                oldSpriteImage = DataSystems->TryAcquire<Texture::CodecImage>(oldSprite);
                if (!oldSprite || !oldSpriteImage)
                {
                    return std::nullopt;
                }
                Check(oldSprite->GetImageView(oldSpriteImage).Width() == 4u, "Initial sprite is not 4px");
                auto* entity = original->CreateEntity("TextureGate");
                entity->AddComponent<RectTransformComponent>();
                auto* image = entity->AddComponent<ImageComponent>();
                image->Load(oldTexture);
                entity->AddComponent<SpriteRenderer>()->SetSprite(oldSprite);
                CheckSprite(original, oldSprite);
                Check(image->m_curtexture && &*image->m_curtexture == &*oldTexture, "Component did not hold initial generation");
                Check(SceneManagers->SaveScene(saved.string()) != nullptr, "Fixture scene save failed");
                auto* rect = entity->GetComponent<RectTransformComponent>();
                const auto beforeSize = rect->GetSizeDelta();
                Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::CustomChangeCommand>(
                    [rect, beforeSize] { rect->SetSizeDelta(beforeSize); },
                    [rect] { rect->SetSizeDelta({123.f, 77.f}); }));
                undoDepth = Meta::UndoManager::GetInstance()->EditUndoDepth();
                experiment::cooked::TextureImportSettings settings;
                settings.colorSpace = experiment::cooked::TextureColorSpace::Linear;
                settings.maxDimension = 2u;
                editor.QueueAutomaticTextureSettings(source, settings);
                stage = 1u;
            }
            else if (stage == 1u)
            {
                if (!editor.TextureImportReady(source))
                {
                    return std::nullopt;
                }
                newTexture = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::UITexture);
                auto newImage = DataSystems->TryAcquire<Texture::CodecImage>(newTexture);
                if (!newTexture || !newImage || &*newTexture == &*oldTexture
                    || newTexture->GetImageView(newImage).Width() != 2u)
                {
                    return std::nullopt;
                }
                Check(newTexture && newImage && newTexture->GetImageView(newImage).Width() == 2u,
                    "Resized cooked UI generation is not available");
                newSprite = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::Texture);
                auto spriteImage = DataSystems->TryAcquire<Texture::CodecImage>(newSprite);
                if (!newSprite || !spriteImage || &*newSprite == &*oldSprite
                    || newSprite->GetImageView(spriteImage).Width() != 2u)
                {
                    return std::nullopt;
                }
                Check(newSprite->GetImageView(spriteImage).Width() == 2u && &*newSprite != &*oldSprite,
                    "Sprite reimport did not publish a new 2px generation");
                auto* liveImage = Image(original);
                auto* liveSprite = Sprite(original);
                if (!liveImage || !liveImage->m_curtexture || &*liveImage->m_curtexture != &*newTexture
                    || !liveSprite || !liveSprite->GetSprite() || &*liveSprite->GetSprite() != &*newSprite)
                {
                    return std::nullopt;
                }
                CheckSprite(original, newSprite);
                Check(liveImage->GetOwner()->GetComponent<RectTransformComponent>()->GetSizeDelta() == math::vector2(123.f, 77.f),
                    "Automatic reload discarded unsaved UI size");
                Check(&*newTexture != &*oldTexture, "Reimport reused initial description");
                auto* image = Image(original);
                Check(SceneManagers->GetActiveScene() == original && image && image->m_curtexture
                    && &*image->m_curtexture == &*newTexture, "Automatic reimport did not update the existing component");
                Check(oldTexture->GetImageView(oldImage).Width() == 4u, "Old generation view changed");
                Check(Meta::UndoManager::GetInstance()->EditUndoDepth() == undoDepth, "Automatic reimport erased Undo history");
                Meta::UndoManager::GetInstance()->Undo();
                Check(liveImage->GetOwner()->GetComponent<RectTransformComponent>()->GetSizeDelta() != math::vector2(123.f, 77.f),
                    "Undo did not restore the scene edit after reimport");
                Meta::UndoManager::GetInstance()->Redo();
                Check(liveImage->GetOwner()->GetComponent<RectTransformComponent>()->GetSizeDelta() == math::vector2(123.f, 77.f),
                    "Redo did not restore the scene edit after reimport");
                request = SceneManagers->QueueSceneLoad(saved.string(), true);
                stage = 2u;
            }
            else if (stage == 2u)
            {
                const auto result = SceneManagers->QuerySceneLoad(request);
                if (result.state == LoadState::Pending || SceneManagers->GetActiveScene() == original)
                {
                    Check(result.state == LoadState::Pending || result.state == LoadState::Ready,
                        "Saved scene reload failed");
                    return std::nullopt;
                }
                Check(result.state == LoadState::Ready, "Reload did not report Ready");
                replacement = SceneManagers->GetActiveScene();
                auto* image = Image(replacement);
                Check(image && image->m_curtexture && &*image->m_curtexture == &*newTexture,
                    "Reloaded component did not consume new cooked generation");
                CheckSprite(replacement, newSprite);
                Check(oldSprite->GetImageView(oldSpriteImage).Width() == 4u, "Scene retirement damaged old sprite owner");
                Check(oldTexture->GetImageView(oldImage).Width() == 4u, "Scene retirement damaged old owner");
                std::ofstream broken(source, std::ios::binary | std::ios::trunc);
                broken << "broken png";
                broken.close();
                Reimport(2u);
                stage = 3u;
            }
            else if (stage == 3u)
            {
                if (!editor.TextureImportStatus(source).starts_with("Import failed; previous generation retained:"))
                {
                    return std::nullopt;
                }
                auto* image = Image(replacement);
                Check(SceneManagers->GetActiveScene() == replacement && image && image->m_curtexture
                    && &*image->m_curtexture == &*newTexture, "Cook failure replaced scene/component generation");
                CheckSprite(replacement, newSprite);
                request = SceneManagers->QueueSceneLoad(saved.string() + ".missing", true);
                stage = 4u;
            }
            else if (stage == 4u)
            {
                const auto result = SceneManagers->QuerySceneLoad(request);
                if (result.state == LoadState::Pending)
                {
                    return std::nullopt;
                }
                Check(result.state == LoadState::Failed, "Missing scene was not rejected");
                auto* image = Image(replacement);
                Check(SceneManagers->GetActiveScene() == replacement && image && image->m_curtexture
                    && &*image->m_curtexture == &*newTexture, "Failed reload damaged current scene generation");
                CheckSprite(replacement, newSprite);
                Restore();
                Reimport(0u);
                stage = 5u;
            }
            else if (stage == 5u)
            {
                if (!editor.TextureImportReady(source))
                {
                    return std::nullopt;
                }
                auto restored = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::UITexture);
                auto restoredImage = DataSystems->TryAcquire<Texture::CodecImage>(restored);
                if (!restored || !restoredImage)
                {
                    return std::nullopt;
                }
                Check(restored && restoredImage && restored->GetImageView(restoredImage).Width() == 4u,
                    "Valid recovery did not publish 4px");
                auto* image = Image(replacement);
                if (!image || !image->m_curtexture || &*image->m_curtexture != &*restored)
                {
                    return std::nullopt;
                }
                Check(image && image->m_curtexture && &*image->m_curtexture == &*restored,
                    "Recovery did not automatically update the active component");
                auto restoredSprite = DataSystems->LoadSharedTexture(source.string(), DataSystem::TextureFileType::Texture);
                auto restoredSpriteImage = DataSystems->TryAcquire<Texture::CodecImage>(restoredSprite);
                if (!restoredSprite || !restoredSpriteImage)
                {
                    return std::nullopt;
                }
                Check(restoredSprite->GetImageView(restoredSpriteImage).Width() == 4u, "Sprite recovery did not publish 4px");
                auto* sprite = Sprite(replacement);
                if (!sprite || !sprite->GetSprite() || &*sprite->GetSprite() != &*restoredSprite)
                {
                    return std::nullopt;
                }
                CheckSprite(replacement, restoredSprite);
                prefabPath = saved.parent_path() / "AutoApply.prefab";
                auto* prefabSource = replacement->CreateEntity("AutoApplySource");
                prefabSource->AddComponent<RectTransformComponent>();
                auto candidate = std::unique_ptr<Prefab>(Prefab::CreateFromGameObject(prefabSource));
                Check(PrefabUtilitys->SavePrefab(candidate.get(), prefabPath.string()), "Prefab fixture save failed");
                auto* prefab = PrefabUtilitys->LoadPrefabFullPath(prefabPath.string());
                prefabInstance = PrefabUtilitys->InstantiatePrefab(prefab, replacement, "AutoApplyInstance");
                Check(prefabInstance != nullptr, "Prefab fixture instantiate failed");
                prefabOverrideInstance = PrefabUtilitys->InstantiatePrefab(prefab, replacement, "AutoApplyOverride");
                Check(prefabOverrideInstance != nullptr, "Prefab override fixture instantiate failed");
                prefabOverrideInstance->GetComponent<RectTransformComponent>()->SetSizeDelta({66.f, 55.f});
                // Match the Inspector commit path: setters alone do not author overrides.
                PrefabUtility::RecordPropertyOverride(*prefabOverrideInstance,
                    *prefabOverrideInstance->GetComponent<RectTransformComponent>(), "m_sizeDelta");
                Check(!prefabOverrideInstance->m_prefabOverrides.empty(), "Prefab override was not authored");
                std::ifstream input(prefabPath, std::ios::binary);
                prefabBefore.assign(std::istreambuf_iterator<char>(input), {});
                PrefabEditors->Open(prefabPath.string());
                Check(PrefabEditors->IsOpened(), "Prefab editor did not open");
                auto* editScene = SceneManagers->GetActiveScene();
                Check(editScene != replacement && editScene->m_Entities.size() > 1, "Prefab editing scene missing");
                prefabEditRect = editScene->m_Entities[1]->GetComponent<RectTransformComponent>();
                Check(prefabEditRect != nullptr, "Prefab editor component missing");
                const auto beforeSize = prefabEditRect->GetSizeDelta();
                Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::CustomChangeCommand>(
                    [rect = prefabEditRect, beforeSize] { rect->SetSizeDelta(beforeSize); },
                    [rect = prefabEditRect] { rect->SetSizeDelta({91.f, 42.f}); }));
                undoDepth = Meta::UndoManager::GetInstance()->EditUndoDepth();
                stage = 6u;
            }
            else if (stage == 6u)
            {
                Check(prefabEditRect->GetSizeDelta() == math::vector2(91.f, 42.f), "Prefab edit value reset before automatic save");
                std::ifstream input(prefabPath, std::ios::binary);
                const std::string disk{ std::istreambuf_iterator<char>(input), {} };
                if (disk == prefabBefore || prefabInstance->GetComponent<RectTransformComponent>()->GetSizeDelta() != math::vector2(91.f, 42.f))
                {
                    return std::nullopt;
                }
                Check(prefabOverrideInstance->GetComponent<RectTransformComponent>()->GetSizeDelta() == math::vector2(66.f, 55.f),
                    "Automatic Apply discarded a prefab instance override");
                Check(PrefabEditors->IsOpened(), "Automatic Apply closed the prefab editor");
                Check(prefabEditRect == SceneManagers->GetActiveScene()->m_Entities[1]->GetComponent<RectTransformComponent>(),
                    "Automatic Apply replaced the editing component");
                Check(Meta::UndoManager::GetInstance()->EditUndoDepth() == undoDepth, "Automatic Apply erased prefab Undo history");
                Meta::UndoManager::GetInstance()->Undo();
                stage = 7u;
            }
            else if (stage == 7u)
            {
                if (prefabInstance->GetComponent<RectTransformComponent>()->GetSizeDelta() == math::vector2(91.f, 42.f))
                {
                    return std::nullopt;
                }
                Meta::UndoManager::GetInstance()->Redo();
                stage = 8u;
            }
            else if (stage == 8u)
            {
                if (prefabInstance->GetComponent<RectTransformComponent>()->GetSizeDelta() != math::vector2(91.f, 42.f))
                {
                    return std::nullopt;
                }
                PrefabEditors->Close(false);
                Check(SceneManagers->GetActiveScene() == replacement, "Prefab close did not restore the original scene");
                auto data = CommandData::Object();
                data.Set("checks", CommandData::Int(checks));
                data.Set("passed", CommandData::Bool(true));
                return Ok("TEXTURE_REIMPORT_SCENE_OK initial=4 published=2 automaticComponent=2 unsavedUi=retained reloadedComponent=2 cookFailure=retained reloadFailure=retained recovery=4 undo=retained prefab=autoApplyUndoRedo consumers=ImageComponent,SpriteRenderer", std::move(data));
            }
            return std::nullopt;
        }
        catch (const std::exception& error)
        {
            Restore();
            return Fail("texture.reimportscene.failed", std::string(error.what()) + " stage=" + std::to_string(stage));
        }
    }
};

static CommandCore::CommandResult Cmd_texture_reimport_scene_probe(const ConsoleCommandContext& ctx)
{
    using namespace CommandCore;
    if (ctx.parts.size() != 3u)
    {
        return InvalidArguments("assets.texture.reimportprobe <isolated-source> <isolated-scene>");
    }
    auto state = std::make_shared<TextureReimportSceneProbe>();
    state->source = file::path(ctx.parts[1]);
    state->saved = file::path(ctx.parts[2]);
    const auto project = state->source.parent_path().parent_path().parent_path();
    if (state->source.filename() != "fixture.png"
        || !file::exists(project / ".texture-reimport-probe")
        || state->saved.parent_path() != project / "Assets" / "Scenes")
    {
        return PreconditionFailed("texture.reimportscene.isolation", "Use the marked isolated regression project");
    }
    std::ifstream input(state->source, std::ios::binary);
    state->originalBytes.assign(std::istreambuf_iterator<char>(input), {});
    if (state->originalBytes.empty() || file::exists(state->saved))
    {
        return PreconditionFailed("texture.reimportscene.fixture", "Use fresh isolated source and scene paths");
    }
    state->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    state->Reimport(0u);
    ctx.system.WaitForResult([state] { return state->Poll(); }, true);
    return Ok("Texture reimport/scene product probe scheduled");
}
