#include "TemporalMotionFixture.h"
#if CE_DEVELOPMENT && !CE_SHIPPING
#include "TemporalMotionFixtureAssets.h"
#include "SceneManager.h"
#include "Scene.h"
#include "CameraComponent.h"
#include "MeshRenderer.h"
#include "Animator.h"
#include "FoliageComponent.h"
#include "DecalComponent.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "UIComponent.h"
#include "LightComponent.h"
#include "Terrain.h"
#include "SpriteRenderer.h"
#include "RectTransformComponent.h"
#include "Transform.h"
#include "DataSystem.h"
#include "Material.h"
#include "Texture.h"
#include "PathFinder.h"
#include "ContentAbi.h"
#include "JobScheduler.h"
#include "Assets/ModelMeshDescriptor.h"
#include "Assets/ModelAnimationDescriptor.h"
#include "Assets/ModelSkeletonPayload.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Render/Temporal/TemporalRuntimeControl.h"
#include "AuthoringRymlErrorPolicy.h"
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/yml/emit.hpp>
#include <mathematics/transform.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace
{
    namespace ck = experiment::cooked;
    constexpr std::array<const char*, 7> kRoutes{
        "static", "skinned", "instanced", "decal", "alpha", "sprite", "meshlet" };
    constexpr math::vector3 kPosition{ 0.f, 0.f, 4.f };
    constexpr std::array<math::vector3, 7> kDisplacements{
        math::vector3{ .25f, .125f, 0.f }, {}, {},
        math::vector3{ -.5f, .25f, 0.f }, { .1875f, -.375f, 0.f },
        { .125f, .0625f, 0.f }, { -.125f, -.25f, 0.f } };

    enum class Phase
    {
        Assets, Bindings, Baseline, Current, Control,
    };
    struct BuildResult
    {
        TemporalMotionFixtureAssets assets;
        std::vector<std::pair<std::string, std::string>> inputAssets;
        std::string error;
        bool succeeded{};
    };
    struct Session
    {
        TemporalMotionFixtureStatus status;
        TemporalMotionFixtureState pendingTerminal{ TemporalMotionFixtureState::Idle };
        Phase phase{ Phase::Assets };
        std::uint64_t id{}, sceneEpoch{};
        std::size_t route{};
        std::uint32_t width{}, height{};
        bool serializationGuard{};
        gc::root_ref<Scene> scene;
        gc::root_ref<Entity> root, moving, receiver;
        gc::root_ref<Component> routeComponent;
        gc::root_ref<CameraComponent> camera;
        gc::root_ref<MeshRenderer> mesh;
        gc::root_ref<Animator> animator;
        gc::root_ref<FoliageComponent> foliage;
        gc::root_ref<DecalComponent> decal;
        gc::root_ref<RectTransformComponent> spriteRect;
        std::array<std::uint64_t, 2> instances{};
        std::vector<std::pair<gc::weak_ref<Component>, bool>> previousComponents;
        std::optional<TemporalNativeCaptureExclusion> exclusion;
        std::shared_ptr<BuildResult> build;
        job_handle buildJob;
        AssetDepot::AssetMountId mount;
        AssetDepot::AssetRequest<assets::ModelMeshDescriptor> staticMesh, skinnedMesh, meshletMesh;
        AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> model;
        AssetDepot::AssetRequest<assets::ModelSkeletonPayload> skeleton;
        AssetDepot::AssetRequest<Material> opaqueMaterial, alphaMaterial, receiverMaterial;
        AssetDepot::AssetRequest<Texture> opaqueTexture, alphaTexture;
        ryml::Tree manifest;
        std::vector<std::pair<std::string, std::string>> inputAssets;
        std::string inputHash;
        EnhancedTemporalFixtureStamp armedCapture;
        std::chrono::steady_clock::time_point deadline;
    };
    std::unique_ptr<Session> g_session;
    std::uint64_t g_nextSession{ 1 };

    std::string Hash(std::span<const std::byte> bytes)
    {
        ck::Sha256Digest digest{};
        std::string error;
        if (!ck::ComputeSha256(bytes, digest, error))
        {
            throw std::runtime_error(error);
        }
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (const auto byte : digest)
        {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
        return result;
    }

    std::string WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output)
        {
            throw std::runtime_error("Cannot write fixture input: " + path.string());
        }
        return Hash(bytes);
    }

    std::string WriteJson(const std::filesystem::path& path, const ryml::Tree& tree)
    {
        const auto bytes = ryml::emitrs_json<std::string>(tree);
        return WriteBytes(path, std::as_bytes(std::span(bytes.data(), bytes.size())));
    }

    std::string HashFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Missing completed capture manifest: " + path.string());
        }
        const std::string bytes{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        return Hash(std::as_bytes(std::span(bytes.data(), bytes.size())));
    }

    void Vector(ryml::NodeRef node, const math::vector3& value)
    {
        node |= ryml::SEQ;
        node.append_child() << value.x;
        node.append_child() << value.y;
        node.append_child() << value.z;
    }

    template<class T>
    bool Ready(const AssetDepot::AssetRequest<T>& request)
    {
        const auto snapshot = request.Snapshot();
        if (snapshot.status == AssetDepot::AssetRequestStatus::Pending)
        {
            return false;
        }
        if (snapshot.status != AssetDepot::AssetRequestStatus::Ready || !snapshot.asset)
        {
            throw std::runtime_error("Fixture asset preparation failed: " + snapshot.message);
        }
        return true;
    }

    bool Active(const Session& session)
    {
        return session.status.state == TemporalMotionFixtureState::Preparing
            || session.status.state == TemporalMotionFixtureState::Running;
    }

    bool Cleanup(Session& session)
    {
        EnhancedSceneRenderer::ClearTemporalFixtureSession(session.id);
        // A replaced/retired scene owns its own teardown. Never mutate borrowed
        // components after that lifetime boundary, even while a job is finishing.
        if (session.scene && session.scene.get() == SceneManagers->GetActiveScene()
            && !session.scene->IsManagedRetiringOrRetired())
        {
            if (session.animator && !session.animator->IsDestroyMark())
            {
                session.animator->GetInstance().diagnosticPoseOverride = false;
            }
            if (session.root && !session.root->IsDestroyMark())
            {
                session.scene->DestroyEntity(session.root.get());
            }
            for (const auto& [reference, enabled] : session.previousComponents)
            {
                const auto component = reference.lock();
                if (component && !component->IsDestroyMark() && !component->IsEnabled())
                {
                    component->SetEnabled(enabled);
                }
            }
        }
        session.previousComponents.clear();
        if (session.serializationGuard && session.scene && !session.scene->IsManagedRetiringOrRetired())
        {
            session.scene->EndIncrementalConstruction();
        }
        session.serializationGuard = false;
        session.staticMesh.Cancel();
        session.skinnedMesh.Cancel();
        session.meshletMesh.Cancel();
        session.model.Cancel();
        session.skeleton.Cancel();
        session.opaqueMaterial.Cancel();
        session.alphaMaterial.Cancel();
        session.receiverMaterial.Cancel();
        session.opaqueTexture.Cancel();
        session.alphaTexture.Cancel();
        session.staticMesh = {};
        session.skinnedMesh = {};
        session.meshletMesh = {};
        session.model = {};
        session.skeleton = {};
        session.opaqueMaterial = {};
        session.alphaMaterial = {};
        session.receiverMaterial = {};
        session.opaqueTexture = {};
        session.alphaTexture = {};
        if (session.mount.IsValid())
        {
            std::vector<ck::AssetManifestIssue> issues;
            if (!DataSystems->UnmountAssetSet(session.mount, issues))
            {
                session.status.error += " Fixture asset mount cleanup failed.";
                session.pendingTerminal = TemporalMotionFixtureState::Failed;
            }
            session.mount = {};
        }
        session.root = nullptr;
        session.animator = nullptr;
        session.routeComponent = nullptr;
        session.mesh = nullptr;
        session.foliage = nullptr;
        session.decal = nullptr;
        session.spriteRect = nullptr;
        session.moving = nullptr;
        session.receiver = nullptr;
        session.scene = nullptr;
        session.camera = nullptr;
        // Restoration cannot be deferred past a scene switch. RT cancellation
        // can: immutable submitted packets retain their assets, and the session
        // keeps native exclusion until this nonblocking admission succeeds.
        if (session.armedCapture.IsValid()
            && !EnhancedSceneRenderer::TryCancelLivePbrCapture(session.armedCapture))
        {
            return false;
        }
        session.exclusion.reset();
        return true;
    }

    void Fail(Session& session, std::string error)
    {
        session.status.error = std::move(error);
        session.pendingTerminal = TemporalMotionFixtureState::Failed;
        try
        {
            if (Cleanup(session))
            {
                session.status.state = session.pendingTerminal;
            }
        }
        catch (const std::exception& exception)
        {
            session.status.error += " Cleanup is pending: " + std::string(exception.what());
        }
        catch (...)
        {
            session.status.error += " Cleanup is pending after an unknown exception";
        }
    }

    Entity* Create(Session& session, const char* name, GameObjectType type = GameObjectType::Empty,
        Entity* parent = nullptr)
    {
        auto* entity = session.scene->CreateEntity(name, type,
            parent ? parent->m_index : session.root->m_index);
        if (!entity)
        {
            throw std::runtime_error("Cannot create temporal fixture entity");
        }
        return entity;
    }

    MeshRenderer* BindMesh(Entity& entity, const own::shared_owner<const assets::ModelMeshDescriptor>& mesh,
        const own::shared_owner<const Material>& material)
    {
        auto* renderer = entity.AddComponent<MeshRenderer>();
        if (!renderer || !renderer->BindMeshDescriptor(mesh))
        {
            throw std::runtime_error("Cannot bind real fixture geometry");
        }
        renderer->SetMaterial(Material::InstantiateShared(material.get()));
        renderer->SetLODEnabled(false);
        return renderer;
    }

    EnhancedTemporalFixtureStamp Stamp(const Session& session, std::uint64_t offset)
    {
        const auto baseline = static_cast<std::uint64_t>(session.route * 3 + 1);
        EnhancedTemporalFixtureStamp stamp;
        stamp.sessionId = session.id;
        stamp.stepId = baseline + offset;
        stamp.predecessorStepId = offset == 0 ? 0 : baseline + offset - 1;
        stamp.inputSha256 = session.inputHash;
        stamp.predecessorInputSha256 = offset == 0 ? std::string{} : session.inputHash;
        stamp.allowMeshlets = session.route == 6;
        return stamp;
    }

    void Pose(Session& session, bool current)
    {
        session.moving->Transform_().SetPosition(session.route == 2 ? math::vector3{}
            : kPosition + (current && session.route != 5 ? kDisplacements[session.route] : math::vector3{}));
        if (session.route == 5)
        {
            // Exercise the public rect-layout publication path rather than
            // manually refreshing the retained sprite proxy. UI Y is down.
            const auto displacement = current ? kDisplacements[session.route] : math::vector3{};
            session.spriteRect->SetAnchoredPosition({ displacement.x, -displacement.y });
        }
        if (session.animator)
        {
            auto& instance = session.animator->GetInstance();
            instance.diagnosticPoseOverride = true;
            instance.finalTransforms = {
                math::translation_matrix(current ? math::vector3{ .125f, -.0625f, 0.f } : math::vector3{}),
                math::translation_matrix(current ? math::vector3{ .625f, -.3125f, 0.f } : math::vector3{}) };
            session.mesh->PublishRenderProxyDirty(ProxyDirty::Payload);
        }
        if (session.foliage)
        {
            const std::array<math::vector3, 2> positions{
                math::vector3{ -1.5f, 0.f, 4.f } + (current ? math::vector3{ -.375f, .1875f, 0.f } : math::vector3{}),
                math::vector3{ 1.5f, 0.f, 4.f } + (current ? math::vector3{ .25f, -.125f, 0.f } : math::vector3{}) };
            for (std::size_t index = 0; index < positions.size(); ++index)
            {
                if (!session.foliage->UpdateFoliageInstance(session.instances[index], positions[index], {}, { 1.f, 1.f, 1.f }))
                {
                    throw std::runtime_error("Fixture foliage identity changed");
                }
            }
        }
    }

    void StartRoute(Session& session)
    {
        session.status.route = kRoutes[session.route];
        session.moving = Create(session, "__TemporalMotionRoute",
            session.route == 5 ? GameObjectType::Canvas : GameObjectType::Empty)->root_from_this();
        session.mesh = nullptr;
        session.animator = nullptr;
        session.foliage = nullptr;
        session.decal = nullptr;
        session.spriteRect = nullptr;
        session.receiver = nullptr;
        const auto opaque = session.opaqueMaterial.Snapshot().asset;
        if (session.route == 2)
        {
            session.foliage = session.moving->AddComponent<FoliageComponent>()->root_from_this();
            FoliageType type;
            type.m_modelGuid = FileGuid(session.build->assets.model.identity.assetId.value);
            type.m_meshAssetId = FileGuid(session.build->assets.staticMesh.identity.assetId.value);
            type.m_materialAssetId = FileGuid(session.build->assets.opaqueMaterial.identity.assetId.value);
            session.foliage->AddFoliageType(type);
            for (const float x : { -1.5f, 1.5f })
            {
                FoliageInstance instance;
                instance.m_position = { x, 0.f, 4.f };
                session.foliage->AddFoliageInstance(instance);
            }
            const auto& instances = session.foliage->GetFoliageInstances();
            session.instances = { instances[0].m_temporalIdentity, instances[1].m_temporalIdentity };
            session.routeComponent = session.foliage;
        }
        else if (session.route == 3)
        {
            session.receiver = Create(session, "__TemporalMotionReceiver")->root_from_this();
            session.receiver->Transform_().SetPosition(kPosition);
            session.receiver->Transform_().SetScale({ 3.f, 3.f, 1.f });
            BindMesh(*session.receiver, session.staticMesh.Snapshot().asset, session.receiverMaterial.Snapshot().asset);
            session.decal = session.moving->AddComponent<DecalComponent>()->root_from_this();
            session.moving->Transform_().SetScale({ 2.f, 2.f, 2.f });
            session.decal->SetDecalTexture(FileGuid(session.build->assets.opaqueTexture.identity.assetId.value));
            session.routeComponent = session.decal;
        }
        else if (session.route == 5)
        {
            // Canvas has a real world-space transform; only the child rect uses
            // UI coordinates. Moving it exercises the world sprite writer.
            auto* canvas = session.moving->AddComponent<Canvas>();
            canvas->RenderMode = CanvasRenderMode::WorldSpace;
            canvas->ScaleMode = CanvasScaleMode::ConstantPixelSize;
            canvas->ScaleFactor = 1.f;
            auto* canvasRect = session.moving->GetComponent<RectTransformComponent>();
            canvasRect->SetSizeDelta({ 2.f, 2.f });
            canvasRect->DriveAsWorldCanvasRoot();
            auto* imageEntity = Create(session, "__TemporalMotionImage", GameObjectType::UI, session.moving.get());
            auto* rect = imageEntity->GetComponent<RectTransformComponent>();
            if (!rect)
            {
                rect = imageEntity->AddComponent<RectTransformComponent>();
            }
            rect->SetAnchorMin({ .5f, .5f });
            rect->SetAnchorMax({ .5f, .5f });
            rect->SetPivot({ .5f, .5f });
            rect->SetAnchoredPosition({ 0.f, 0.f });
            rect->SetSizeDelta({ 2.f, 2.f });
            session.spriteRect = rect->root_from_this();
            auto* image = imageEntity->AddComponent<ImageComponent>();
            image->Load(session.opaqueTexture.Snapshot().asset);
            image->color = { .75f, .125f, .25f, 1.f };
            canvas->AddUIObject(imageEntity);
            rect->UpdateLayout(canvasRect->GetWorldRect());
            image->RefreshTransformFromRect();
            session.routeComponent = image->root_from_this();
        }
        else
        {
            auto* meshEntity = session.route == 1
                ? Create(session, "__TemporalWeightedMesh", GameObjectType::Empty, session.moving.get())
                : session.moving.get();
            session.mesh = BindMesh(*meshEntity,
                session.route == 1 ? session.skinnedMesh.Snapshot().asset
                    : session.route == 6 ? session.meshletMesh.Snapshot().asset : session.staticMesh.Snapshot().asset,
                session.route == 4 ? session.alphaMaterial.Snapshot().asset : opaque)->root_from_this();
            session.routeComponent = session.mesh;
            if (session.route == 1)
            {
                session.animator = session.moving->AddComponent<Animator>()->root_from_this();
                session.animator->m_Motion = FileGuid(session.build->assets.model.identity.assetId.value);
                if (!session.animator->BindModelDescriptor(session.model.Snapshot().asset, session.skeleton.Snapshot().asset))
                {
                    throw std::runtime_error("Cannot bind fixture two-bone skeleton");
                }
            }
        }
        Pose(session, false);
        session.phase = Phase::Bindings;
    }

    bool RouteReady(Session& session)
    {
        if (!session.camera->HasLifecycleState(Component::State_Initialized)
            || !session.routeComponent->HasLifecycleState(Component::State_Initialized)
            || (session.animator && !session.animator->HasLifecycleState(Component::State_Initialized)))
        {
            return false;
        }
        if (session.foliage)
        {
            session.foliage->PollAssetBindings();
            const auto status = session.foliage->GetAssetBindingStatus(0);
            if (status == AssetDepot::AssetRequestStatus::Pending)
            {
                return false;
            }
            if (status != AssetDepot::AssetRequestStatus::Ready)
            {
                throw std::runtime_error("Fixture foliage typed binding failed");
            }
        }
        if (session.decal)
        {
            session.decal->PollTextureRequests();
            return session.decal->GetDecalTexture() != nullptr;
        }
        return true;
    }

    bool Capture(Session& session, const char* name, std::uint64_t offset)
    {
        std::string error;
        bool busy{};
        const auto directory = std::filesystem::u8path(session.status.directory) / kRoutes[session.route] / name;
        const auto encoded = directory.u8string();
        const std::string utf8(encoded.begin(), encoded.end());
        if (!EnhancedSceneRenderer::RequestLivePbrCapture(utf8, EnhancedLiveDisplayTarget::Game,
                error, false, {}, {}, {}, false, false, true, Stamp(session, offset), &busy))
        {
            if (busy)
            {
                return false;
            }
            throw std::runtime_error(error);
        }
        session.armedCapture = Stamp(session, offset);
        return true;
    }

    void DescribeCamera(ryml::NodeRef node)
    {
        node |= ryml::MAP;
        node["projection"] << "orthographic";
        Vector(node["position"], {});
        node["width"] << 8;
        node["height"] << 8;
        node["near"] << .1f;
        node["far"] << 20;
        node["depthConvention"] << "forward-z";
    }

    void DescribeInputs(ryml::NodeRef root, const Session& session)
    {
        root["extent"] |= ryml::SEQ;
        root["extent"].append_child() << session.width;
        root["extent"].append_child() << session.height;
        DescribeCamera(root["camera"]);
        auto assets = root["inputAssets"];
        assets |= ryml::SEQ;
        for (const auto& [path, hash] : session.inputAssets)
        {
            auto asset = assets.append_child();
            asset |= ryml::MAP;
            asset["path"] << path;
            asset["sha256"] << hash;
        }
    }

    void DescribeCase(ryml::NodeRef node, const Session& session)
    {
        node |= ryml::MAP;
        node["name"] << kRoutes[session.route];
        node["route"] << kRoutes[session.route];
        node["allowMeshlets"] << (session.route == 6);
        node["sessionId"] << session.id;
        node["baselineStepId"] << session.route * 3 + 1;
        node["currentStepId"] << session.route * 3 + 2;
        node["controlStepId"] << session.route * 3 + 3;
        auto geometry = node["geometry"];
        geometry |= ryml::MAP;
        geometry["vertices"] |= ryml::SEQ;
        const std::array<math::vector3, 4> positions{
            math::vector3{ -1.f, -1.f, 0.f }, { 1.f, -1.f, 0.f },
            { 1.f, 1.f, 0.f }, { -1.f, 1.f, 0.f } };
        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            auto vertex = geometry["vertices"].append_child();
            vertex |= ryml::MAP;
            Vector(vertex["position"], positions[index]);
            vertex["uv"] |= ryml::SEQ;
            vertex["uv"].append_child() << (index == 1 || index == 2 ? 1 : 0);
            vertex["uv"].append_child() << (index < 2 ? 1 : 0);
            if (session.route == 1)
            {
                vertex["weights"] |= ryml::SEQ;
                vertex["weights"].append_child() << (index < 2 ? .9 : .1);
                vertex["weights"].append_child() << (index < 2 ? .1 : .9);
            }
        }
        geometry["indices"] |= ryml::SEQ;
        for (const auto index : { 0, 2, 1, 0, 3, 2 })
        {
            geometry["indices"].append_child() << index;
        }
        for (const bool current : { false, true })
        {
            auto endpoint = node[current ? "current" : "previous"];
            endpoint |= ryml::MAP;
            Vector(endpoint["worldTranslation"], session.route == 2 ? math::vector3{}
                : kPosition + (current ? kDisplacements[session.route] : math::vector3{}));
            if (session.route == 1)
            {
                endpoint["boneTranslations"] |= ryml::SEQ;
                Vector(endpoint["boneTranslations"].append_child(),
                    current ? math::vector3{ .125f, -.0625f, 0.f } : math::vector3{});
                Vector(endpoint["boneTranslations"].append_child(),
                    current ? math::vector3{ .625f, -.3125f, 0.f } : math::vector3{});
            }
            if (session.route == 2)
            {
                endpoint["instanceTranslations"] |= ryml::SEQ;
                Vector(endpoint["instanceTranslations"].append_child(), math::vector3{ -1.5f, 0.f, 4.f }
                    + (current ? math::vector3{ -.375f, .1875f, 0.f } : math::vector3{}));
                Vector(endpoint["instanceTranslations"].append_child(), math::vector3{ 1.5f, 0.f, 4.f }
                    + (current ? math::vector3{ .25f, -.125f, 0.f } : math::vector3{}));
            }
        }
        auto ownership = node["ownership"];
        ownership |= ryml::MAP;
        ownership["attachment"] << (session.route == 5 ? "postSpriteHdr" : "baseColor");
        Vector(ownership["expectedColor"], session.route == 3
            ? math::vector3{ 1.f, 1.f, 1.f } : math::vector3{ .75f, .125f, .25f });
        ownership["colorTolerance"] << .04f;
        ownership["minimumContrast"] << .1f;
        if (session.route == 4)
        {
            // Match a real exported typed texture by its exact artifact path.
            for (const auto& [path, hash] : session.inputAssets)
            {
                if (path.find("alpha.cetex") != std::string::npos)
                {
                    ownership["alphaTexture"] |= ryml::MAP;
                    ownership["alphaTexture"]["path"] << path;
                    ownership["alphaTexture"]["sha256"] << hash;
                }
            }
        }
    }

    void SealCaseInput(Session& session)
    {
        ryml::Tree input;
        auto root = input.rootref();
        root |= ryml::MAP;
        DescribeInputs(root, session);
        DescribeCase(root, session);
        const auto relative = std::string("input-assets/") + kRoutes[session.route] + "-case.json";
        session.inputHash = WriteJson(std::filesystem::u8path(session.status.directory) / relative, input);
        auto node = session.manifest.rootref()["cases"].append_child();
        DescribeCase(node, session);
        node["input"] |= ryml::MAP;
        node["input"]["path"] << relative;
        node["input"]["sha256"] << session.inputHash;
        node["captures"] |= ryml::MAP;
    }

    void RecordCapture(Session& session, const char* name)
    {
        const auto relative = std::string(kRoutes[session.route]) + "/" + name;
        auto node = session.manifest.rootref()["cases"][session.route]["captures"][name];
        node |= ryml::MAP;
        node["path"] << relative;
        node["manifestSha256"] << HashFile(std::filesystem::u8path(session.status.directory) / relative / "manifest.json");
    }

    void Mount(Session& session)
    {
        const auto& assets = session.build->assets;
        session.inputAssets = session.build->inputAssets;
        std::vector<ck::AssetManifestIssue> issues;
        session.mount = DataSystems->MountAssetSet(assets.manifest, assets.source, assets.mountOptions, issues);
        if (!session.mount.IsValid())
        {
            throw std::runtime_error("Cannot mount fixture asset set");
        }
        session.staticMesh = DataSystems->RequestAsync(assets.staticMesh);
        session.skinnedMesh = DataSystems->RequestAsync(assets.skinnedMesh);
        session.meshletMesh = DataSystems->RequestAsync(assets.meshletMesh);
        session.model = DataSystems->RequestAsync(assets.model);
        session.skeleton = DataSystems->RequestAsync(assets.skeleton);
        session.opaqueMaterial = DataSystems->RequestAsync(assets.opaqueMaterial);
        session.alphaMaterial = DataSystems->RequestAsync(assets.alphaMaterial);
        session.receiverMaterial = DataSystems->RequestAsync(assets.receiverMaterial);
        session.opaqueTexture = DataSystems->RequestAsync(assets.opaqueTexture);
        session.alphaTexture = DataSystems->RequestAsync(assets.alphaTexture);
    }

    void Isolate(Session& session)
    {
        // Reuse the existing asynchronous-construction serialization barrier:
        // neither hidden originals nor transient fixture assets may be saved.
        session.scene->BeginIncrementalConstruction();
        session.serializationGuard = true;
        for (const auto& entity : session.scene->m_Entities)
        {
            if (!entity || entity->IsDestroyMark())
            {
                continue;
            }
            for (auto* component : entity->GetComponents<Component>())
            {
                // Do not dispatch ScriptComponent OnDisable/OnEnable or touch
                // unrelated gameplay/audio state merely to isolate rendering.
                if (!dynamic_cast<MeshRenderer*>(component) && !dynamic_cast<FoliageComponent*>(component)
                    && !dynamic_cast<DecalComponent*>(component) && !dynamic_cast<SpriteRenderer*>(component)
                    && !dynamic_cast<TerrainComponent*>(component) && !dynamic_cast<LightComponent*>(component)
                    && !dynamic_cast<UIComponent*>(component) && !dynamic_cast<Canvas*>(component)
                    && !dynamic_cast<CameraComponent*>(component) && !dynamic_cast<Animator*>(component))
                {
                    continue;
                }
                session.previousComponents.emplace_back(component->weak_from_this(), component->IsEnabled());
            }
        }
        for (const auto& [reference, enabled] : session.previousComponents)
        {
            if (const auto component = reference.lock())
            {
                component->SetEnabled(false);
            }
        }
        auto* fixtureRoot = session.scene->CreateEntity("__TemporalMotionFixture");
        if (!fixtureRoot)
        {
            throw std::runtime_error("Cannot create transient fixture root");
        }
        session.root = fixtureRoot->root_from_this();
        fixtureRoot->Transform_().SetWorldPosition({});
        fixtureRoot->Transform_().SetWorldRotation({ 0.f, 0.f, 0.f, 1.f });
        fixtureRoot->Transform_().SetWorldScale({ 1.f, 1.f, 1.f });
        session.scene->ProtectTransientDiagnosticRoot(session.scene->HandleOf(fixtureRoot->m_index));
        auto* cameraEntity = Create(session, "__TemporalMotionCamera");
        auto* camera = cameraEntity->AddComponent<CameraComponent>();
        session.camera = camera->root_from_this();
        camera->SetPrimary(true);
        camera->GetCamera()->m_isOrthographic = true;
        camera->GetCamera()->m_viewWidth = 8.f;
        camera->GetCamera()->m_viewHeight = 8.f;
        camera->GetCamera()->m_nearPlane = .1f;
        camera->GetCamera()->m_farPlane = 20.f;
        camera->NotifyCameraCut();
        auto root = session.manifest.rootref();
        root |= ryml::MAP;
        root["schema"] << "temporal.motion.executed-fixture.v2";
        root["acceptance"] << "unverified-requires-independent-oracle";
        DescribeInputs(root, session);
        root["cases"] |= ryml::SEQ;
        session.status.state = TemporalMotionFixtureState::Running;
        StartRoute(session);
    }
}

bool TemporalMotionFixture::Begin(const std::string& directory, std::string& error)
{
    if (g_session && Active(*g_session))
    {
        error = "A motion fixture session is already active";
        return false;
    }
    if (!SceneManagers->GetActiveScene() || SceneManagers->IsPlayCommitted()
        || !PathFinder::IsAssetAuthoringEnabled())
    {
        error = "The motion fixture requires an idle, paused Editor scene";
        return false;
    }
    EnhancedLivePbrCaptureStatus capture;
    if (!EnhancedSceneRenderer::TryGetLivePbrCaptureStatus(capture))
    {
        error = "Renderer is busy; retry fixture admission after the current capture completes";
        return false;
    }
    if (capture.state == EnhancedPbrCaptureState::Pending || capture.state == EnhancedPbrCaptureState::Recording)
    {
        error = "Another product capture is active";
        return false;
    }
    std::filesystem::path destination;
    try
    {
        destination = std::filesystem::u8path(directory);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
    std::error_code pathError;
    const bool exists = std::filesystem::exists(destination, pathError);
    if (!destination.is_absolute() || exists || pathError)
    {
        error = "The fixture requires a new absolute output directory";
        return false;
    }
    const auto display = EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::Game);
    if (!display.completedWidth || !display.completedHeight)
    {
        error = "A live Game view is required before starting the fixture";
        return false;
    }
    try
    {
        auto session = std::make_unique<Session>();
        session->id = g_nextSession++;
        session->scene = SceneManagers->GetActiveScene()->root_from_this();
        session->sceneEpoch = SceneManagers->SceneContextEpoch();
        session->width = display.completedWidth;
        session->height = display.completedHeight;
        session->status = { TemporalMotionFixtureState::Preparing, directory, {}, {} };
        session->deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        session->exclusion.emplace();
        session->build = std::make_shared<BuildResult>();
        std::filesystem::create_directories(destination.parent_path());
        if (!std::filesystem::create_directory(destination))
        {
            error = "Fixture output directory was created by another writer";
            return false;
        }
        Authoring::EnsureRymlErrorPolicy();
        TemporalMotionFixtureAssetOptions options;
        options.targetPlatform = "win-x64";
        options.targetAbi = CreatorContentAbi::Token;
        options.shaderDirectory = PathFinder::ShaderPath();
        options.cacheDirectory = destination / "compiler-cache";
        const auto result = session->build;
        session->buildJob = ce::get_job_scheduler().submit([result, options, destination]
        {
            result->succeeded = TemporalMotionFixtureAssets::Build(options, result->assets, result->error);
            if (!result->succeeded)
            {
                return;
            }
            // Preserve the exact prepared bytes for independent verification.
            // Shader/blob export and hashing stay off the game thread too.
            for (const auto& artifact : result->assets.artifacts)
            {
                std::vector<std::byte> bytes(static_cast<std::size_t>(artifact.byteSize));
                if (!result->assets.source->ReadAt(artifact.artifactPath, 0, bytes, result->error))
                {
                    result->succeeded = false;
                    return;
                }
                const auto relative = "input-assets/" + artifact.artifactPath;
                result->inputAssets.emplace_back(relative, WriteBytes(destination / relative, bytes));
            }
            result->inputAssets.emplace_back("input-assets/manifest.cemf",
                WriteBytes(destination / "input-assets/manifest.cemf", result->assets.manifest));
        });
        g_session = std::move(session);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

void TemporalMotionFixture::TickAfterAnimation()
{
    if (!g_session || !Active(*g_session))
    {
        return;
    }
    auto& session = *g_session;
    try
    {
        if (session.pendingTerminal != TemporalMotionFixtureState::Idle)
        {
            if (Cleanup(session))
            {
                session.status.state = session.pendingTerminal;
            }
            return;
        }
        if (std::chrono::steady_clock::now() >= session.deadline)
        {
            throw std::runtime_error("Motion fixture timed out waiting for assets or exact submissions");
        }
        if (session.scene.get() != SceneManagers->GetActiveScene()
            || session.sceneEpoch != SceneManagers->SceneContextEpoch() || SceneManagers->IsPlayCommitted()
            || session.scene->IsManagedRetiringOrRetired())
        {
            throw std::runtime_error("Scene ownership changed during motion fixture");
        }
        const auto display = EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::Game);
        if (display.completedWidth != session.width || display.completedHeight != session.height)
        {
            throw std::runtime_error("Game view extent changed during motion fixture");
        }
        for (const auto& [reference, enabled] : session.previousComponents)
        {
            const auto component = reference.lock();
            if (!component || component->IsDestroyMark() || component->IsEnabled())
            {
                throw std::runtime_error("Original component edited during motion fixture; preserving the edit");
            }
        }
        if (session.root && (session.root->IsDestroyMark() || !session.root->IsEnabled()
            || !session.routeComponent || session.routeComponent->IsDestroyMark()
            || (session.animator && session.animator->IsDestroyMark())))
        {
            throw std::runtime_error("Fixture content was modified while its capture was pending");
        }
        if (session.phase == Phase::Assets)
        {
            if (!session.buildJob.is_complete())
            {
                return;
            }
            // Completed-only wait retrieves worker exceptions; never blocks GT.
            session.buildJob.wait();
            if (!session.build->succeeded)
            {
                throw std::runtime_error(session.build->error);
            }
            if (!session.mount.IsValid())
            {
                Mount(session);
            }
            if (!Ready(session.staticMesh) || !Ready(session.skinnedMesh) || !Ready(session.meshletMesh)
                || !Ready(session.model) || !Ready(session.skeleton) || !Ready(session.opaqueMaterial)
                || !Ready(session.alphaMaterial) || !Ready(session.receiverMaterial)
                || !Ready(session.opaqueTexture) || !Ready(session.alphaTexture))
            {
                return;
            }
            Isolate(session);
        }
        if (session.phase == Phase::Bindings)
        {
            if (!RouteReady(session))
            {
                return;
            }
            SealCaseInput(session);
            EnhancedSceneRenderer::PublishTemporalFixtureStep(Stamp(session, 0));
            session.phase = Phase::Baseline;
            return;
        }
        if (session.phase == Phase::Baseline)
        {
            const auto submitted = EnhancedSceneRenderer::GetTemporalFixtureSubmission(EnhancedLiveDisplayTarget::Game);
            if (!submitted.valid || submitted.stamp != Stamp(session, 0))
            {
                return;
            }
            if (!Capture(session, "current", 1))
            {
                return;
            }
            Pose(session, true);
            EnhancedSceneRenderer::PublishTemporalFixtureStep(Stamp(session, 1));
            session.phase = Phase::Current;
            return;
        }
        EnhancedLivePbrCaptureStatus capture;
        if (!EnhancedSceneRenderer::TryGetLivePbrCaptureStatus(capture, session.armedCapture))
        {
            return;
        }
        if (capture.state == EnhancedPbrCaptureState::Failed)
        {
            throw std::runtime_error(capture.error);
        }
        if (capture.state != EnhancedPbrCaptureState::Complete)
        {
            return;
        }
        if (session.phase == Phase::Current)
        {
            RecordCapture(session, "current");
            if (!Capture(session, "control", 2))
            {
                return;
            }
            session.routeComponent->SetEnabled(false);
            EnhancedSceneRenderer::PublishTemporalFixtureStep(Stamp(session, 2));
            session.phase = Phase::Control;
            return;
        }
        RecordCapture(session, "control");
        if (session.animator)
        {
            session.animator->GetInstance().diagnosticPoseOverride = false;
        }
        session.scene->DestroyEntity(session.moving.get());
        if (session.receiver)
        {
            session.scene->DestroyEntity(session.receiver.get());
        }
        if (++session.route < kRoutes.size())
        {
            StartRoute(session);
            return;
        }
        WriteJson(std::filesystem::u8path(session.status.directory) / "fixture.json", session.manifest);
        session.pendingTerminal = TemporalMotionFixtureState::Complete;
        if (Cleanup(session))
        {
            session.status.state = session.pendingTerminal;
        }
    }
    catch (const std::exception& exception)
    {
        Fail(session, exception.what());
    }
}

void TemporalMotionFixture::Cancel(const std::string& reason)
{
    if (g_session && Active(*g_session))
    {
        Fail(*g_session, reason);
    }
}

TemporalMotionFixtureStatus TemporalMotionFixture::GetStatus()
{
    return g_session ? g_session->status : TemporalMotionFixtureStatus{};
}

void TemporalMotionFixture::ShutdownAfterRenderJoin() noexcept
{
    if (!g_session)
    {
        return;
    }
    try
    {
        const auto ownedCapture = g_session->armedCapture;
        Cancel("Editor is shutting down");
        if (ownedCapture.IsValid())
        {
            // This is deliberately the joined teardown API. Normal frame
            // admission/status/cancellation above never waits for this mutex.
            EnhancedSceneRenderer::CancelLivePbrCapture(ownedCapture);
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Temporal fixture shutdown cleanup: %s\n", error.what());
    }
    catch (...)
    {
        std::fprintf(stderr, "Temporal fixture shutdown cleanup failed\n");
    }
    // A worker, if still running, retains only its independent BuildResult.
    // Ready AssetRequests and scene roots cannot escape service/GC lifetime.
    g_session.reset();
}
#else
bool TemporalMotionFixture::Begin(const std::string&, std::string& error)
{
    error = "Temporal motion fixtures require a non-shipping development Editor";
    return false;
}
void TemporalMotionFixture::TickAfterAnimation()
{
}
void TemporalMotionFixture::Cancel(const std::string&)
{
}
void TemporalMotionFixture::ShutdownAfterRenderJoin() noexcept
{
}
TemporalMotionFixtureStatus TemporalMotionFixture::GetStatus()
{
    return {};
}
#endif
