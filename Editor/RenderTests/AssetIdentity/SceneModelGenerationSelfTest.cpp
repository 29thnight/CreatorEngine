#include "AssetIdentity/SceneModelGenerationSelfTest.h"

#include "Assets/ModelAssetGeneration.h"
#include "DataSystem.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "RHI/IRenderDeviceServices.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Texture.h"
#include "ModelSceneInstantiation.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>

namespace RenderTest
{
    bool RunIncrementalModelCancellationSelfTest(const std::string& guardPath)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        const auto generation = DataSystems->FindModelAssetGenerationByStem("Gunner_F_Mythic");
        if (!scene || !generation) return false;
        auto pending = ModelSceneInstantiation::PendingInstance::Prepare(generation, {});
        if (!pending) return false;
        const auto status = pending->Advance(*scene, 1, std::chrono::milliseconds(2), 0);
        const auto handle = pending->Root();
        const bool building = status == ModelSceneInstantiation::PendingInstance::Status::Building
            && handle.IsValid() && scene->Resolve(handle);
        const bool saveRejected = SceneManagers->SaveScene(guardPath) == nullptr;
        pending->Cancel(*scene);
        scene->EndFramePass();
        const bool removed = scene->Resolve(handle) == nullptr;
        bool guardReleased = true;
        try { scene->OnBeforeSerialize(); }
        catch (...) { guardReleased = false; }

        Entity* replacement = scene->CreateEntity("AsyncCancellationSlotReuse");
        const auto replacementHandle = scene->HandleOf(replacement->m_index);
        pending->Cancel(*scene); // a stale cancellation must not destroy the reused slot
        scene->EndFramePass();
        const bool reusedSafely = handle.index == replacementHandle.index
            && handle.generation != replacementHandle.generation && scene->Resolve(replacementHandle);
        scene->DestroyEntity(replacementHandle.index);
        scene->EndFramePass();
        const bool passed = building && saveRejected && removed && guardReleased && reusedSafely;
        std::printf("[model.async] cancellation-probe %s building=%d saveRejected=%d removed=%d guardReleased=%d reusedSafely=%d\n",
            passed ? "pass" : "fail", building, saveRejected, removed, guardReleased, reusedSafely);
        return passed;
    }

    namespace
    {
        struct SceneModelTally final
        {
            std::size_t renderers{};
            std::size_t generationBound{};
            std::size_t unbound{};
            std::size_t handleInvalid{};
            std::size_t rhiView{};
            std::size_t meshIdPersisted{};
            std::size_t textureProps{};
            std::size_t embeddedProps{};
            std::size_t generationTextures{};
            std::size_t otherTextures{};
            std::size_t missingTextures{};
            std::size_t gunnerRenderers{};
            std::size_t gunnerEmbedded{};
            std::string firstProblem{};

            void Note(const std::string& text)
            {
                if (firstProblem.empty()) firstProblem = text;
            }
        };

        void TallyRenderer(MeshRenderer& renderer, const std::string& entityName,
            SceneModelTally& tally)
        {
            ++tally.renderers;
            const std::shared_ptr<const assets::ModelAssetGeneration> generation =
                renderer.m_modelGeneration;
            if (!generation)
            {
                // UUIDv8 모델인데 typed 정본이 없다 — 그릴 것이 없는 renderer다(MBC9).
                if (assets::IsUuidV8(renderer.m_modelGuid.m_guid))
                {
                    ++tally.unbound;
                    tally.Note("unbound " + entityName);
                }
                return;
            }
            ++tally.generationBound;

            const assets::ModelMeshHandle handle = renderer.GetModelMeshHandle();
            if (!handle.IsValid())
            {
                ++tally.handleInvalid;
                tally.Note("handleInvalid " + entityName);
            }
            RHIModelMeshView view{};
            if (BuildRHIModelMeshView(*generation, renderer.m_modelMeshIndex, view)
                && view.IsComplete() && view.handle == handle)
            {
                ++tally.rhiView;
            }
            else
            {
                tally.Note("rhiView " + entityName);
            }

            const auto meshes = generation->Meshes();
            if (renderer.m_modelMeshIndex >= meshes.size())
            {
                tally.Note("meshIndex " + entityName);
                return;
            }
            const assets::ModelMeshAsset& mesh = meshes[renderer.m_modelMeshIndex];
            if (FileGuid{} != renderer.m_meshAssetId
                && renderer.m_meshAssetId.m_guid == mesh.meshId)
            {
                ++tally.meshIdPersisted;
            }
            else
            {
                tally.Note("meshAssetId " + entityName);
            }
            const bool isGunner =
                generation->SourcePath().filename().string() == "Gunner_F_Mythic.glb";
            if (isGunner) ++tally.gunnerRenderers;
            if (!renderer.m_Material) return;

            for (const MaterialPropertyValue& value : renderer.m_Material->m_propertyValues)
            {
                if (value.m_name.empty() || FileGuid{} == value.m_textureGuid) continue;
                ++tally.textureProps;
                if (nullptr == generation->FindTexture(value.m_textureGuid.m_guid))
                    continue; // 외부 텍스처 자산 — 이 축 밖(파일 경로 해석)
                ++tally.embeddedProps;
                if (isGunner) ++tally.gunnerEmbedded;

                const std::shared_ptr<Texture> owner =
                    renderer.m_Material->GetTextureMapShared(value.m_name);
                const std::shared_ptr<Texture> expected =
                    DataSystems->ResolveModelGenerationTexture(*generation,
                        value.m_textureGuid.m_guid);
                if (!owner)
                {
                    ++tally.missingTextures;
                    tally.Note("missing " + entityName + "." + value.m_name);
                }
                else if (owner == expected)
                {
                    ++tally.generationTextures;
                }
                else
                {
                    ++tally.otherTextures;
                    tally.Note("other " + entityName + "." + value.m_name);
                }
            }
        }
    }

    bool RunSceneModelGenerationSelfTest(std::string& outLog)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            outLog += "[assets.scenemodel] 활성 씬이 없다\n";
            std::printf("[CLI] assets.scenemodel fail reason=no-scene\n");
            return false;
        }

        SceneModelTally tally;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr == renderer) continue;
            TallyRenderer(*renderer, object->m_name.ToString(), tally);
        }

        const DataSystem::ModelGenerationTextureCacheSnapshot cache =
            DataSystems->SnapshotModelGenerationTextures();
        const assets::ModelAssetGenerationCacheSnapshot generations =
            DataSystems->SnapshotModelAssetGenerations();

        // "0개를 비교해 차이 0"을 통과로 읽지 않는다 — renderer·generation·embedded
        // 계수가 실제로 움직였을 때만 pass다(Gunner 씬은 embedded 6이 정확히 나와야 한다).
        bool passed = tally.renderers > 0 && tally.generationBound > 0
            && 0 == tally.unbound && 0 == tally.handleInvalid
            && tally.rhiView == tally.generationBound
            && tally.meshIdPersisted == tally.generationBound
            && tally.embeddedProps == tally.generationTextures
            && 0 == tally.otherTextures
            && 0 == tally.missingTextures;
        if (tally.gunnerRenderers > 0
            && !(tally.gunnerRenderers >= 2u && 6u == tally.gunnerEmbedded))
        {
            passed = false;
            tally.Note("gunner closure renderers=" + std::to_string(tally.gunnerRenderers)
                + " embedded=" + std::to_string(tally.gunnerEmbedded));
        }

        char line[768]{};
        std::snprintf(line, sizeof(line),
            "[CLI] assets.scenemodel %s renderers=%zu generation=%zu unbound=%zu"
            " handleInvalid=%zu rhiView=%zu meshIdPersisted=%zu"
            " textureProps=%zu embedded=%zu generationTextures=%zu"
            " otherTextures=%zu missing=%zu gunner=%zu/%zu cacheLive=%zu cacheCreated=%llu"
            " cacheHits=%llu generations=%zu%s%s\n",
            passed ? "pass" : "fail", tally.renderers, tally.generationBound,
            tally.unbound, tally.handleInvalid, tally.rhiView, tally.meshIdPersisted,
            tally.textureProps, tally.embeddedProps, tally.generationTextures,
            tally.otherTextures, tally.missingTextures,
            tally.gunnerRenderers, tally.gunnerEmbedded, cache.live,
            static_cast<unsigned long long>(cache.created),
            static_cast<unsigned long long>(cache.hits), generations.currentAssets,
            tally.firstProblem.empty() ? "" : " first=",
            tally.firstProblem.c_str());
        outLog += line;
        std::printf("%s", line);
        return passed;
    }

    bool RunSceneModelGenerationReloadSelfTest(const std::string& modelName,
        std::string& outLog)
    {
        const FileGuid guid = DataSystems->GetStemToGuid(modelName);
        const std::shared_ptr<const assets::ModelAssetGeneration> before =
            FileGuid{} == guid ? nullptr : DataSystems->LoadModelAssetGeneration(guid);
        if (!before)
        {
            outLog += "[assets.scenemodel reload] 모델 generation을 찾지 못했다: " + modelName + "\n";
            std::printf("[CLI] assets.scenemodel reload fail model=%s reason=no-generation\n",
                modelName.c_str());
            return false;
        }

        std::map<Uuid::Uuid16, std::shared_ptr<Texture>> ownersBefore;
        for (const assets::ModelTextureAsset& texture : before->Textures())
        {
            ownersBefore[texture.textureId] =
                DataSystems->ResolveModelGenerationTexture(*before, texture.textureId);
        }
        const DataSystem::ModelGenerationTextureCacheSnapshot statsBefore =
            DataSystems->SnapshotModelGenerationTextures();

        // 에디터의 reimport 게시가 런타임에 도달하는 유일한 경계와 같은 호출이다.
        RuntimeAssetChange change;
        change.kind = RuntimeAssetChangeKind::ContentReload;
        change.assetType = RuntimeAssetType::Model;
        change.guid = guid;
        change.path = DataSystems->GetFilePath(guid);
        DataSystems->ApplyAssetChange(change);

        const std::shared_ptr<const assets::ModelAssetGeneration> after =
            DataSystems->LoadModelAssetGeneration(guid);
        std::size_t reused = 0;
        std::size_t created = 0;
        std::size_t missingAfter = 0;
        if (after)
        {
            for (const assets::ModelTextureAsset& texture : after->Textures())
            {
                const std::shared_ptr<Texture> owner =
                    DataSystems->ResolveModelGenerationTexture(*after, texture.textureId);
                const auto previous = ownersBefore.find(texture.textureId);
                if (!owner) ++missingAfter;
                else if (previous != ownersBefore.end() && previous->second
                    && previous->second == owner) ++reused;
                else ++created;
            }
        }
        const DataSystem::ModelGenerationTextureCacheSnapshot statsAfter =
            DataSystems->SnapshotModelGenerationTextures();
        const std::uint64_t retired = statsAfter.retired - statsBefore.retired;

        const bool passed = static_cast<bool>(after) && after != before
            && 0 == reused && 0 == missingAfter
            && created == before->Textures().size()
            && retired == before->Textures().size();
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "[CLI] assets.scenemodel reload %s model=%s textures=%zu reused=%zu created=%zu"
            " missing=%zu retired=%llu sameAggregate=%d\n",
            passed ? "pass" : "fail", modelName.c_str(), before->Textures().size(),
            reused, created, missingAfter, static_cast<unsigned long long>(retired),
            after == before ? 1 : 0);
        outLog += line;
        std::printf("%s", line);
        return passed;
    }
}
