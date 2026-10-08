#include "Animator.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "AnimatorSystem.h"
#include "AnimationScheduler.h"
#include "TransCondition.h"
#include "AniTransition.h"
#include "AnimationState.h"
#include "AvatarMask.h"
#include "ConditionParameter.h"
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "../RenderEngine/BoneRegion.h"
#include "SceneManager.h"
#include "Socket.h"
#include "../RenderEngine/Assets/ModelAssetGeneration.h" // Transitional CEMCv11
#include "../RenderEngine/Assets/ModelAnimationDescriptor.h"
#include "../RenderEngine/Assets/ModelAnimationPayload.h"
#include "../RenderEngine/Assets/ModelMeshDescriptor.h"
#include <algorithm>
#include <cassert>
#include <iterator>

Animator::Animator() : m_instance(AnimatorSystems->CreateInstance()) {}

AnimInstance& Animator::GetInstance() noexcept
{
    AnimInstance* instance = AnimatorSystems->ResolveInstance(m_instance);
    assert(instance);
    return *instance;
}

const AnimInstance& Animator::GetInstance() const noexcept
{
    const AnimInstance* instance = AnimatorSystems->ResolveInstance(m_instance);
    assert(instance);
    return *instance;
}

Animator::~Animator()
{
	AnimatorSystems->DestroyInstance(m_instance);
	m_animationControllers.clear();

	{
		std::unique_lock lock(m_paramMutex);
		for (auto& param : Parameters)
		{
			delete param; // 하나씩 해제
		}
		Parameters.clear(); // 벡터 비우기
	}

	for (auto& socket : socketvec)
	{
		delete socket;
	}
	socketvec.clear();
}

namespace
{
	// I5-D4e-1 — legacy MarkRegionSkeleton 재현. 게시 계약(parent < index) 덕에
	// "이름 매칭이면 자기 리전, 아니면 부모 리전 상속"의 단일 순회가 legacy의
	// 서브트리 전파(+나중 매칭이 덮어씀)와 같은 결과를 낸다 — legacy m_bones
	// 순서가 곧 인덱스 순서(역브리지 1:1)이기 때문이다.
	// PHASE 3.75 MBC8 — typed·experiment 두 뼈 목록에 같은 규칙을 적용하는 일반형.
	// nameOf(i)·parentOf(i)(부모 없음 = boneCount 이상)만 받는다.
	template <typename NameOf, typename ParentOf>
	[[nodiscard]] std::vector<std::uint8_t> DeriveBoneRegionsFrom(
		std::size_t boneCount, NameOf nameOf, ParentOf parentOf)
	{
		std::vector<std::uint8_t> regions(boneCount,
			static_cast<std::uint8_t>(BoneRegion::Root));
		for (std::size_t index = 0; index < boneCount; ++index)
		{
			const std::string name = ToLower(nameOf(index));
			bool matched = true;
			BoneRegion region = BoneRegion::Root;
			if (name.find("spine") != std::string::npos) region = BoneRegion::Spine;
			else if (name.find("neck") != std::string::npos) region = BoneRegion::Neck;
			else if (name.find("shoulder") != std::string::npos)
			{
				if (name.find("left") != std::string::npos) region = BoneRegion::LeftArm;
				else if (name.find("right") != std::string::npos) region = BoneRegion::RightArm;
				else matched = false;
			}
			else if (name.find("leg") != std::string::npos)
			{
				if (name.find("left") != std::string::npos) region = BoneRegion::LeftLeg;
				else if (name.find("right") != std::string::npos) region = BoneRegion::RightLeg;
				else matched = false;
			}
			else matched = false;

			if (matched)
			{
				regions[index] = static_cast<std::uint8_t>(region);
			}
			else if (const std::size_t parent = parentOf(index); parent < index)
			{
				regions[index] = regions[parent];
			}
		}
		return regions;
	}

	[[nodiscard]] std::vector<std::uint8_t> DeriveTypedBoneRegions(
		const assets::ModelSkeletonAsset& skeleton)
	{
		return DeriveBoneRegionsFrom(skeleton.bones.size(),
			[&skeleton](std::size_t index) -> const std::string&
			{ return skeleton.bones[index].name; },
			[&skeleton](std::size_t index) -> std::size_t
			{
				const std::uint32_t parent = skeleton.bones[index].parent;
				return parent == assets::kInvalidModelAssetIndex
					? skeleton.bones.size() : parent;
			});
	}

	// 관측 축 보고 — viaExperiment는 experiment 핸들 축만 참(A/B 대조군 계약),
	// outPath는 실제 출처 셋을 가른다.
	// MBC9: experiment 축이 은퇴해 viaExperiment는 항상 false다(호환 인자).
	void ReportPath(bool* outViaExperiment, AnimatorDataPath* outPath,
		AnimatorDataPath path) noexcept
	{
		if (outViaExperiment) *outViaExperiment = false;
		if (outPath) *outPath = path;
	}

	// typed skeleton 신원 — Scene 본 캐시 무효화 키. 0은 "스켈레톤 없음" 가드라
	// 회피하고, legacy m_serial·experiment Generation 번호 공간과 겹치지 않게
	// {ModelId, SkeletonId, generation} 전체를 FNV-1a 64로 접는다.
	[[nodiscard]] uint64 TypedSkeletonSerial(
		const assets::ModelAssetGeneration& generation,
		const assets::ModelSkeletonAsset& skeleton) noexcept
	{
		std::uint64_t hash = 1469598103934665603ull;
		const auto mix = [&hash](const void* data, std::size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (std::size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};
		const assets::ModelAssetGenerationIdentity& identity = generation.Identity();
		mix(identity.modelId.data.data(), identity.modelId.data.size());
		mix(skeleton.skeletonId.data.data(), skeleton.skeletonId.data.size());
		mix(&identity.generation, sizeof(identity.generation));
		if (0 == hash) hash = 1;
		return static_cast<uint64>(hash);
	}
}

const assets::ModelSkeletonAsset* AnimatorAnimationBinding::Skeleton() const noexcept
{
    if (descriptor)
    {
        return skeleton ? &skeleton->skeleton : nullptr;
    }
    return legacyGeneration ? legacyGeneration->Skeleton() : nullptr;
}

const assets::ModelAnimationDescriptor* Animator::TypedDescriptor() const noexcept
{
    return m_animationDescriptor ? &*m_animationDescriptor : nullptr;
}

const assets::ModelSkeletonAsset* Animator::TypedSkeleton() const noexcept
{
    if (m_granularAnimationBinding)
    {
        return m_skeletonPayload ? &m_skeletonPayload->skeleton : nullptr;
    }
    return m_modelGeneration ? m_modelGeneration->Skeleton() : nullptr;
}

std::size_t Animator::TypedClipCount() const noexcept
{
    if (m_granularAnimationBinding)
    {
        return m_animationDescriptor ? m_animationDescriptor->summary.clips.size() : 0u;
    }
    return m_modelGeneration ? m_modelGeneration->Animations().size() : 0u;
}

const assets::ModelAnimationAsset* Animator::TypedClip(int clipIndex) const noexcept
{
    if (clipIndex < 0)
    {
        return nullptr;
    }
    if (m_granularAnimationBinding)
    {
        for (const auto& selected : m_requestedClips)
        {
            if (selected.clipIndex == clipIndex && selected.payload)
            {
                return &selected.payload->clip;
            }
        }
        return nullptr;
    }
    if (!m_modelGeneration)
    {
        return nullptr;
    }
    const auto clips = m_modelGeneration->Animations();
    const auto index = static_cast<std::size_t>(clipIndex);
    return index < clips.size() ? &clips[index] : nullptr;
}

AnimatorDataPath Animator::GetSkeletonPath() const noexcept
{
    if (!TypedSkeleton())
    {
        return AnimatorDataPath::None;
    }
    return m_granularAnimationBinding ? AnimatorDataPath::Granular
        : AnimatorDataPath::Generation;
}

void Animator::EnsureAnimationBinding()
{
    if (!m_animationBindingInitialized || m_animationBindingMotion != m_Motion)
    {
        const bool motionChanged = m_animationBindingMotion != m_Motion;
        m_animationBindingMotion = m_Motion;
        m_animationBindingInitialized = true;
        GetInstance().layerTrackTables.clear();
        m_descriptorRequest.Cancel();
        m_skeletonRequest.Cancel();
        m_descriptorRequest = {};
        m_skeletonRequest = {};
        m_animationDescriptor.reset();
        m_skeletonPayload.reset();
        m_requestedClips.clear();
        m_skeletonRequested = false;
        m_granularAnimationBinding = false;
        if (FileGuid{} == m_Motion)
        {
            m_modelGeneration.reset();
            ResetAnimationPose();
            return;
        }
        const AssetDepot::AssetLink<assets::ModelAnimationDescriptor> link{
            { experiment::AssetId{ m_Motion.m_guid }, {} } };
        m_granularAnimationBinding = DataSystems->HasModelAnimationDescriptor(link);
        if (!m_granularAnimationBinding)
        {
            // Explicit CEMCv11 fallback only. A pending/failed v3 request must
            // never enter the synchronous legacy aggregate loader.
            if (!m_modelGeneration || motionChanged)
            {
                BindModelGeneration(DataSystems->LoadModelAssetGeneration(m_Motion));
                m_animationBindingInitialized = true;
            }
            return;
        }
        m_modelGeneration.reset();
        m_animationDescriptor = DataSystems->TryAcquire(link);
        if (!m_animationDescriptor)
        {
            m_descriptorRequest = DataSystems->RequestAsync(link);
        }
    }
    if (!m_granularAnimationBinding)
    {
        return;
    }
    if (!m_animationDescriptor)
    {
        const auto result = m_descriptorRequest.Snapshot();
        if (result.status != AssetDepot::AssetRequestStatus::Ready || !result.asset)
        {
            // Terminal failure/staleness is observable until the caller makes
            // an explicit rebind. Never silently replace its requested revision.
            return;
        }
        m_animationDescriptor = result.asset;
        m_descriptorRequest = {};
    }
    if (!m_skeletonPayload)
    {
        if (!m_animationDescriptor->summary.skeletonAssetId.IsValid())
        {
            return;
        }
        if (!m_skeletonRequested)
        {
            m_skeletonPayload = DataSystems->TryAcquireSkeleton(m_animationDescriptor);
            if (!m_skeletonPayload)
            {
                m_skeletonRequest = DataSystems->RequestSkeletonAsync(m_animationDescriptor);
            }
            m_skeletonRequested = true;
        }
        if (!m_skeletonPayload)
        {
            const auto result = m_skeletonRequest.Snapshot();
            if (result.status != AssetDepot::AssetRequestStatus::Ready || !result.asset)
            {
                return;
            }
            m_skeletonPayload = result.asset;
        }
        m_skeletonRequest = {};
        RestoreDeferredAvatarMasks();
    }
}

AnimatorAnimationBinding Animator::CaptureAnimationBinding()
{
    EnsureAnimationBinding();
    AnimatorAnimationBinding binding{};
    binding.descriptor = m_animationDescriptor;
    binding.skeleton = m_skeletonPayload;
    binding.skeletonSerial = GetSkeletonSerial();
    if (!m_granularAnimationBinding)
    {
        binding.legacyGeneration = m_modelGeneration;
        binding.ready = binding.Skeleton() != nullptr;
        return binding;
    }
    if (!m_animationDescriptor)
    {
        return binding;
    }
    std::vector<int> indices;
    const auto select = [&](int index)
    {
        if (index >= 0 && static_cast<std::size_t>(index) < TypedClipCount()
            && std::find(indices.begin(), indices.end(), index) == indices.end())
        {
            indices.push_back(index);
        }
    };
    const auto& instance = GetInstance();
    // AnimatorSystem has already selected transitions on the owner thread.
    // Enabled layers also need their current clip for time/event advancement.
    if (m_animationControllers.empty() || IsDirectEditorPreview())
    {
        select(static_cast<int>(instance.selectedClipIndex));
        if (!IsDirectEditorPreview() && instance.control.isBlending)
        {
            select(instance.control.nextClipIndex);
        }
    }
    else
    {
        for (const auto& controller : m_animationControllers)
        {
            if (!controller || !controller->useController)
            {
                continue;
            }
            select(controller->GetAnimationIndex());
            if (controller->IsBlending())
            {
                select(controller->GetNextAnimationIndex());
            }
        }
    }
    // Old per-layer spans must not outlive the selected owners they borrowed.
    GetInstance().layerTrackTables.clear();
    // Drop deselected owners and subscriber handles. Submitted decode jobs own
    // their own captures and can drain without keeping the old clip resident here.
    std::erase_if(m_requestedClips, [&](const RequestedAnimationClip& clip)
    {
        return std::find(indices.begin(), indices.end(), clip.clipIndex) == indices.end();
    });
    binding.ready = m_skeletonPayload != nullptr;
    for (const int index : indices)
    {
        auto found = std::find_if(m_requestedClips.begin(), m_requestedClips.end(),
            [index](const RequestedAnimationClip& clip) { return clip.clipIndex == index; });
        if (found == m_requestedClips.end())
        {
            m_requestedClips.push_back({ index });
            found = std::prev(m_requestedClips.end());
        }
        if (!found->payload && !found->requested)
        {
            found->payload = DataSystems->TryAcquireAnimation(m_animationDescriptor,
                static_cast<std::size_t>(index));
            if (!found->payload)
            {
                found->request = DataSystems->RequestAnimationAsync(m_animationDescriptor,
                    static_cast<std::size_t>(index));
            }
            found->requested = true;
        }
        if (!found->payload)
        {
            const auto result = found->request.Snapshot();
            if (result.status == AssetDepot::AssetRequestStatus::Ready && result.asset)
            {
                found->payload = result.asset;
                found->request = {};
            }
        }
        if (found->payload)
        {
            const auto& summary = m_animationDescriptor->summary.clips[
                static_cast<std::size_t>(index)];
            const auto& payload = *found->payload;
            const auto& clip = payload.clip;
            const auto& expected = m_animationDescriptor->clips[static_cast<std::size_t>(index)];
            const auto& skeleton = m_animationDescriptor->skeleton;
            if (clip.animationId != summary.clipAssetId.value || clip.name != summary.name
                || clip.durationTicks != summary.durationTicks
                || clip.ticksPerSecond != summary.ticksPerSecond || clip.looping != summary.looping
                || payload.origin.entry.asset != expected.entry.asset
                || payload.origin.blob != expected.blob
                || payload.origin.resolverRevision != expected.resolverRevision
                || !payload.skeleton
                || payload.skeleton->skeleton.skeletonId != m_animationDescriptor->summary.skeletonAssetId.value
                || payload.skeleton->origin.entry.asset != skeleton.entry.asset
                || payload.skeleton->origin.blob != skeleton.blob
                || payload.skeleton->origin.resolverRevision != skeleton.resolverRevision
                || payload.tracks.size() != payload.skeleton->skeleton.bones.size())
            {
                // Fail closed once for this selection. Do not sample a clip
                // whose metadata disagrees with the UI/controller descriptor.
                found->payload.reset();
                found->request = {};
                Debug::PrintLog(spdlog::level::err,
                    "[Animator] Animation payload does not match its exact descriptor: "
                    + m_Motion.ToString());
            }
        }
        if (!found->payload)
        {
            binding.ready = false;
        }
        binding.clips.push_back({ index, found->payload });
    }
    if (binding.ready && m_poseSkeletonSerial != binding.skeletonSerial)
    {
        // Keep the last published pose while any selected payload is pending.
        // Change the skeleton's pose storage only at the ready capture boundary.
        ResetAnimationPose();
        GetInstance().boneRegions = DeriveTypedBoneRegions(m_skeletonPayload->skeleton);
        m_poseSkeletonSerial = binding.skeletonSerial;
    }
    return binding;
}

void Animator::ResetAnimationPose()
{
    m_poseSkeletonSerial = 0;
	auto& instance = GetInstance();
	instance.layerTrackTables.clear();
	instance.layerMaskCaches.clear();
	instance.layerSelected.clear();
	instance.poseGlobals.clear();
	instance.ikBindings.clear();
	instance.ikSnapshots.clear();
	instance.boneTransformBindings.clear();
	instance.boneTransformSnapshots.clear();
	instance.workerPoseBufferCount = 0;
	instance.workerPosePoolIdentity = 0;
	instance.workerCurrentStorage = {};
	instance.pose.Clear();
	instance.l6PreviousPose.Clear();
	instance.l6LatestPose.Clear();
	instance.l6PreviousLocals.clear();
	instance.l6LatestLocals.clear();
	instance.l6PreviousFinals.clear();
	instance.l6LatestFinals.clear();
	instance.l6PreviousGlobals.clear();
	instance.l6LatestGlobals.clear();
	instance.l6PublishedAlpha = 1.f;
	instance.blendRecoverySourcePose.Clear();
	instance.l6Seeded = false;
	instance.l6NextPhase = 0;
	instance.l6ClipIndex = -1;
	instance.l6ControllerSlot = -1;
	instance.bindLocals.clear();
	instance.bindSkeletonSerial = 0;
	instance.blendRecoveryPending = false;
	instance.blendRecoveryActive = false;
	instance.localTransforms.clear();
	instance.finalTransforms.clear();
    for (auto& slot : instance.samplingCursors)
    {
        for (auto& cursor : slot)
        {
            cursor.Clear();
        }
    }
    instance.boneRegions.clear();
}

bool Animator::BindModelDescriptor(
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor,
    own::shared_owner<const assets::ModelSkeletonPayload> skeleton)
{
    if (!descriptor || !skeleton || descriptor->summary.modelAssetId.value != m_Motion.m_guid ||
        descriptor->summary.skeletonAssetId != skeleton->origin.entry.asset.key.assetId ||
        descriptor->origin.resolverRevision != skeleton->origin.resolverRevision ||
        descriptor->skeleton.blob != skeleton->origin.blob || skeleton->skeleton.bones.empty() ||
        skeleton->skeleton.rootBone >= skeleton->skeleton.bones.size())
    {
        return false;
    }
    ResetAnimationPose();
    m_descriptorRequest.Cancel();
    m_skeletonRequest.Cancel();
    m_descriptorRequest = {};
    m_skeletonRequest = {};
    m_requestedClips.clear();
    m_modelGeneration.reset();
    m_animationDescriptor = std::move(descriptor);
    m_skeletonPayload = std::move(skeleton);
    m_animationBindingMotion = m_Motion;
    m_animationBindingInitialized = true;
    m_granularAnimationBinding = true;
    m_skeletonRequested = true;
    auto& instance = GetInstance();
    instance.layerTrackTables.clear();
    instance.boneRegions = DeriveTypedBoneRegions(m_skeletonPayload->skeleton);
    m_poseSkeletonSerial = GetSkeletonSerial();
    RestoreDeferredAvatarMasks();
    return true;
}

void Animator::BindModelGeneration(
    assets::ModelAssetGeneration::Shared generation)
{
    ResetAnimationPose();
    m_animationBindingInitialized = false;
    m_animationBindingMotion = m_Motion;
    m_granularAnimationBinding = false;
    m_animationDescriptor.reset();
    m_skeletonPayload.reset();
    m_descriptorRequest.Cancel();
    m_skeletonRequest.Cancel();
    m_descriptorRequest = {};
    m_skeletonRequest = {};
    m_skeletonRequested = false;
    m_requestedClips.clear();
    auto& instance = GetInstance();
	m_modelGeneration.reset();
	instance.boneRegions.clear();
	if (!generation) return;
	if (generation->Identity().modelId != m_Motion.m_guid) return;
	const assets::ModelSkeletonAsset* skeleton = generation->Skeleton();
	if (nullptr == skeleton || skeleton->bones.empty()
		|| skeleton->rootBone >= skeleton->bones.size())
	{
		return;
	}
	instance.boneRegions = DeriveTypedBoneRegions(*skeleton);
    m_modelGeneration = std::move(generation);
    m_poseSkeletonSerial = GetSkeletonSerial();
}

uint64 Animator::GetSkeletonSerial(bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
	// 0은 "스켈레톤 없음" — Scene 본 전파의 가드 값이다. 신원은
	// {ModelId, SkeletonId, generation} 해시(MBC8) 하나다.
	if (const assets::ModelSkeletonAsset* typedSkeleton = TypedSkeleton())
	{
        ReportPath(outViaExperiment, outPath, GetSkeletonPath());
        if (m_skeletonPayload && m_animationDescriptor)
        {
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](const void* data, std::size_t size)
            {
                const auto* bytes = static_cast<const unsigned char*>(data);
                for (std::size_t index = 0; index < size; ++index)
                {
                    hash ^= bytes[index];
                    hash *= 1099511628211ull;
                }
            };
            const auto& origin = m_animationDescriptor->origin;
            mix(&origin.mountId.value, sizeof(origin.mountId.value));
            mix(&origin.resolverRevision, sizeof(origin.resolverRevision));
            mix(origin.blob.contentSha256.data(), origin.blob.contentSha256.size());
            const auto& digest = m_skeletonPayload->origin.blob.contentSha256;
            mix(digest.data(), digest.size());
            return hash == 0 ? 1 : hash;
        }
        return TypedSkeletonSerial(*m_modelGeneration, *typedSkeleton);
	}
	ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
	return 0;
}

bool Animator::IsSkinBindingCompatible(const assets::ModelAssetGeneration& geometry) const noexcept
{
    if (!m_granularAnimationBinding)
    {
        // Preserve the established CEMCv11 adapter. This gate specifically
        // prevents new independent animation payloads crossing an unchecked rig.
        return true;
    }
    if (!m_skeletonPayload || m_poseSkeletonSerial == 0u
        || m_poseSkeletonSerial != GetSkeletonSerial())
    {
        return false;
    }
    const auto* legacy = geometry.Skeleton();
    const auto& selected = m_skeletonPayload->skeleton;
    if (!legacy || selected.bones.size() != legacy->bones.size()
        || selected.rootBone != legacy->rootBone
        || selected.rootTransform != legacy->rootTransform
        || selected.globalInverseTransform != legacy->globalInverseTransform)
    {
        return false;
    }
    for (std::size_t index = 0; index < selected.bones.size(); ++index)
    {
        const auto& source = selected.bones[index];
        const auto& target = legacy->bones[index];
        if (source.name != target.name || source.parent != target.parent
            || source.inverseBindMatrix != target.inverseBindMatrix)
        {
            return false;
        }
    }
    // Geometry joint indices address this exact ordered table. Matching only
    // bone count or the clip's v3 layout digest would not prove skin compatibility.
    // Both descriptions remain borrowed under their callers' existing owners.
    return true;
}

bool Animator::IsSkinBindingCompatible(const assets::ModelMeshDescriptor& geometry) const noexcept
{
    const auto* selected = TypedSkeleton();
    if (!geometry.skinned || !geometry.skeleton || !selected
        || m_poseSkeletonSerial == 0u || m_poseSkeletonSerial != GetSkeletonSerial()
        || geometry.requiredBoneCount != geometry.skeleton->skeleton.bones.size()
        || geometry.requiredSkinBindingSha256 != geometry.skeleton->skinBindingSha256
        || (m_skeletonPayload
            && m_skeletonPayload->skinBindingSha256 != geometry.requiredSkinBindingSha256))
    {
        return false;
    }
    const auto& bound = geometry.skeleton->skeleton;
    if (selected->bones.size() != bound.bones.size() || selected->rootBone != bound.rootBone
        || selected->rootTransform != bound.rootTransform
        || selected->globalInverseTransform != bound.globalInverseTransform)
    {
        return false;
    }
    for (std::size_t index = 0; index < bound.bones.size(); ++index)
    {
        const auto& poseBone = selected->bones[index];
        const auto& geometryBone = bound.bones[index];
        if (poseBone.name != geometryBone.name || poseBone.parent != geometryBone.parent
            || poseBone.inverseBindMatrix != geometryBone.inverseBindMatrix)
        {
            return false;
        }
    }
    return true;
}

double Animator::GetClipDuration(int clipIndex, bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
    ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
    if (m_animationDescriptor && clipIndex >= 0
        && static_cast<std::size_t>(clipIndex) < m_animationDescriptor->summary.clips.size())
    {
        ReportPath(outViaExperiment, outPath, AnimatorDataPath::Granular);
        return m_animationDescriptor->summary.clips[static_cast<std::size_t>(clipIndex)].durationTicks;
    }
    if (clipIndex < 0 || nullptr == TypedSkeleton())
    {
        return 0.0;
    }
	ReportPath(outViaExperiment, outPath, GetSkeletonPath());
	const assets::ModelAnimationAsset* clip = TypedClip(clipIndex);
	return clip ? clip->durationTicks : 0.0;
}

std::size_t Animator::GetBoneCount(bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
	if (const assets::ModelSkeletonAsset* typedSkeleton = TypedSkeleton())
	{
		ReportPath(outViaExperiment, outPath, GetSkeletonPath());
		return typedSkeleton->bones.size();
	}
	ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
	return 0;
}

std::string Animator::GetBoneName(int boneIndex, bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
	ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
	if (boneIndex < 0) return std::string{};
	const std::size_t index = static_cast<std::size_t>(boneIndex);
	if (const assets::ModelSkeletonAsset* typedSkeleton = TypedSkeleton())
	{
		ReportPath(outViaExperiment, outPath, GetSkeletonPath());
		if (index >= typedSkeleton->bones.size()) return std::string{};
		return typedSkeleton->bones[index].name;
	}
	return std::string{};
}

int Animator::ResolveBoneIndex(const std::string& boneName,
	bool* outViaExperiment, AnimatorDataPath* outPath) const
{
	ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
	if (const assets::ModelSkeletonAsset* typedSkeleton = TypedSkeleton())
	{
		ReportPath(outViaExperiment, outPath, GetSkeletonPath());
		for (std::size_t index = 0; index < typedSkeleton->bones.size(); ++index)
		{
			if (typedSkeleton->bones[index].name == boneName)
				return static_cast<int>(index);
		}
	}
	return -1;
}

namespace
{
	// MBC8 — parent-only 표현에서 legacy MakeBoneMask 재귀와 같은 DFS 선순
	// (자기 → 자식 인덱스 순)으로 BoneMask 트리를 만든다. m_BoneMasks의 push
	// 순서가 곧 저장분 인덱스 대응(ReCreateMask)이라 순서 재현이 계약이다.
	template <typename NameOf, typename ParentOf>
	[[nodiscard]] BoneMask* BuildBoneMasksFrom(AvatarMask& mask,
		std::size_t boneCount, std::size_t rootBone, NameOf nameOf, ParentOf parentOf)
	{
		if (rootBone >= boneCount) return nullptr;
		std::vector<std::vector<std::uint32_t>> children(boneCount);
		for (std::size_t index = 0; index < boneCount; ++index)
		{
			const std::size_t parent = parentOf(index);
			if (parent < boneCount)
				children[parent].push_back(static_cast<std::uint32_t>(index));
		}

		std::vector<BoneMask*> masks(boneCount, nullptr);
		std::vector<std::uint32_t> stack{ static_cast<std::uint32_t>(rootBone) };
		while (!stack.empty())
		{
			const std::uint32_t boneIndex = stack.back();
			stack.pop_back();

			BoneMask* newMask = new BoneMask();
			newMask->boneName = nameOf(boneIndex);
			newMask->isEnabled = true;
			mask.m_BoneMasks.push_back(newMask);
			masks[boneIndex] = newMask;
			const std::size_t parent = parentOf(boneIndex);
			if (parent < boneCount && masks[parent])
			{
				masks[parent]->m_children.push_back(newMask);
			}

			// 자식을 역순으로 쌓아야 pop이 인덱스 순으로 방문한다(legacy 재귀의
			// children 순회 순서 재현).
			for (auto childIt = children[boneIndex].rbegin();
				childIt != children[boneIndex].rend(); ++childIt)
			{
				stack.push_back(*childIt);
			}
		}
		return masks[rootBone];
	}
}

BoneMask* Animator::BuildAvatarBoneMasks(AvatarMask& mask,
	bool* outViaExperiment, AnimatorDataPath* outPath)
{
	ReportPath(outViaExperiment, outPath, AnimatorDataPath::None);
	const assets::ModelSkeletonAsset* typedSkeleton = TypedSkeleton();
	if (nullptr == typedSkeleton) return nullptr;
	ReportPath(outViaExperiment, outPath, GetSkeletonPath());
	return BuildBoneMasksFrom(mask, typedSkeleton->bones.size(),
		typedSkeleton->rootBone,
		[typedSkeleton](std::size_t index) -> const std::string&
		{ return typedSkeleton->bones[index].name; },
		[typedSkeleton](std::size_t index) -> std::size_t
		{
			const std::uint32_t parent = typedSkeleton->bones[index].parent;
			return parent == assets::kInvalidModelAssetIndex
				? typedSkeleton->bones.size() : parent;
		});
}

// Scene 편입/이탈은 첫 생성과 DDOL 재부착에 모두 호출된다. AnimationScheduler의
// raw Animator* 등록도 이 수명 경계를 따라야 씬 언로드 후 다시 채워진다.
void Animator::RestoreDeferredAvatarMasks()
{
    for (const auto& deferred : m_deferredAvatarMasks)
    {
        const auto controller = deferred.controller.lock();
        if (!controller || !controller->m_avatarMask)
        {
            continue;
        }
        AvatarMask restored;
        restored.ReCreateMask(controller->m_avatarMask);
        restored.RootMask = BuildAvatarBoneMasks(restored);
        for (std::size_t index = 0; index < deferred.bones.size(); ++index)
        {
            const auto& authored = deferred.bones[index];
            BoneMask* target = nullptr;
            if (authored.name.empty())
            {
                if (index < restored.m_BoneMasks.size())
                {
                    target = restored.m_BoneMasks[index];
                }
            }
            else
            {
                for (BoneMask* candidate : restored.m_BoneMasks)
                {
                    if (candidate && candidate->boneName == authored.name)
                    {
                        target = candidate;
                        break;
                    }
                }
            }
            if (target)
            {
                target->isEnabled = authored.enabled;
                target->weight = authored.weight;
            }
        }
        controller->ReCreateMask(&restored);
    }
    m_deferredAvatarMasks.clear();
}

void Animator::OnAddedToScene()
{
	AnimatorSystems->Register(this);
	SceneManagers->GetAnimationScheduler().RegisterAnimator(this);
}

void Animator::OnRemovingFromScene()
{
	SceneManagers->GetAnimationScheduler().UnregisterAnimator(this);
	AnimatorSystems->Unregister(this);
}

void Animator::OnPropertyChanged(std::string_view propertyName,
    Meta::PropertyChangeSource source)
{
    Component::OnPropertyChanged(propertyName, source);
    if (propertyName == "m_AnimIndexChosen")
        SetSelectedClipIndex(m_AnimIndexChosen);
}

bool Animator::IsDirectEditorPreview() const
{
    return !SceneManagers->IsGameStart() && GetInstance().editorPreviewActive;
}

bool Animator::UsesMultipleControllers() const
{
    return m_animationControllers.size() >= 2 && !IsDirectEditorPreview();
}

void Animator::SetAnimation(int index)
{
	m_AnimIndex = index;
	UpdateAnimation();
}

void Animator::UpdateAnimation()
{

	if (m_AnimIndex <= 0)
		m_AnimIndex = 0;

	// I6-B0 — 클립 계수는 창구가 정본이다(experiment 우선·legacy 폴백).
	// 이전엔 m_Skeleton을 **가드 없이** 역참조했다 — 모델이 안 붙은 Animator에
	// SetAnimation이 닿으면 그 자리에서 죽는다. 클립이 0이면 선택도 0이다.
    const std::size_t clipCount = GetClipCount();
    if (m_granularAnimationBinding && !m_animationDescriptor)
    {
        // Do not erase an explicit selection just because metadata is pending.
        SetSelectedClipIndex(static_cast<uint32_t>(m_AnimIndex));
        GetInstance().timeElapsed = 0;
        return;
    }
	if (0 == clipCount)
	{
		m_AnimIndex = 0;
	}
	else if (static_cast<std::size_t>(m_AnimIndex) >= clipCount)
	{
		m_AnimIndex = static_cast<int>(clipCount) - 1;
	}

	SetSelectedClipIndex(static_cast<uint32_t>(m_AnimIndex));
	GetInstance().timeElapsed = 0;
	
}

void Animator::CreateController(std::string name)
{
	
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = name;
	animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
}

std::shared_ptr<AnimationController> Animator::CreateController_UI()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	animationController->CreateState("Ani State",-1,true);
	m_animationControllers.push_back(animationController);
	return animationController;
}
std::shared_ptr<AnimationController> Animator::CreateController_UINoAni()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	//animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
	return animationController;
}

void Animator::DeleteController(int index)
{	
	m_animationControllers.erase(m_animationControllers.begin() + index);
}

void Animator::DeleteController(std::string controllerName)
{
	auto it = std::remove_if(m_animationControllers.begin(), m_animationControllers.end(),
		[&](std::shared_ptr<AnimationController> controller)
		{
				return controller->name == controllerName;
		});

	m_animationControllers.erase(it, m_animationControllers.end()); 
	
}

AnimationController* Animator::GetController(std::string name)
{
    for (const auto Controller : m_animationControllers)
    {
            if (Controller->name == name)
                    return Controller.get();
    }
    return nullptr;
}



void Animator::SetUseLayer(int layerindex, bool _useLayer)
{
	if (layerindex >= 0 && layerindex < m_animationControllers.size())
	{
		m_animationControllers[layerindex]->SetUseLayer(_useLayer);
	}
}

Entity* Animator::FindBoneRecursive(Entity* parent, const std::string& boneName)
{
	if (!parent) return nullptr;

	for (Entity::Index childIndex : parent->GetChildrenIndices())
	{
		Entity* child = parent->OwnerSceneFindIndex(childIndex);
		if (!child) continue;

        // Match the same instance suffix normalization as Scene pose bindings.
        // Imported actors may have suffixes beyond (10), even after prior actors die.
		if (child->m_name == boneName || child->RemoveSuffixNumberTag() == boneName)
			return child;

		// 자식의 자식들도 탐색
		if (Entity* result = FindBoneRecursive(child, boneName))
			return result;
	}

	return nullptr;
}

Socket* Animator::MakeSocket(std::string_view socketName, std::string_view boneName, Entity* object)
{
	if (Socket* socket = FindSocket(socketName); socket)
		return socket;

	Entity* socketBone = FindBoneRecursive(object, std::string(boneName));

	// 찾았으면 소켓 생성 후 반환
	if (socketBone)
	{
		Socket* newSocket = new Socket();
		newSocket->m_name = socketName;
		newSocket->GameObjectIndex = 10000; // 기존 기본 인덱스
		newSocket->m_ObjectName = boneName;
		socketvec.push_back(newSocket);
		return newSocket;
	}
	return nullptr;
}

Socket* Animator::FindSocket(std::string_view socketName)
{
	for (const auto socket : socketvec)
	{
		if (socket->m_name == socketName)
			return socket;
	}

	return nullptr;
}

void Animator::ClearControllersAndParams()
{
	m_animationControllers.clear();

	std::unique_lock lock(m_paramMutex);
	for (auto* p : Parameters)
	{
		delete p;  
	}
	Parameters.clear();
	NotifyParameterLayoutChanged();
}

void Animator::DeleteParameter(int index)
{
	std::unique_lock lock(m_paramMutex);
	if (index >= 0 && index < Parameters.size())
	{
		for (const auto controller : m_animationControllers)
		{
			for (const auto state : controller->StateVec)
			{
				for (const auto transition : state->Transitions)
				{
					for (auto& condition : transition->conditions)
					{
						if (condition.valueParameter == Parameters[index])
						{
							condition.valueParameter = nullptr;
						}
					}
				}

			}
		}
		delete Parameters[index];
		Parameters.erase(Parameters.begin() + index);
		NotifyParameterLayoutChanged();
	}
}

ConditionParameter* Animator::AddDefaultParameter(ValueType vType)
{
	std::string baseName;
	switch (vType)
	{
	case ValueType::Float:
		baseName = "NewFloat";
		break;
	case ValueType::Int:
		baseName = "NewInt";
		break;
	case ValueType::Bool:
		baseName = "NewBool";
		break;
	case ValueType::Trigger:
		baseName = "NewTrigger";
		break;
	}
	std::string valueName = baseName;
	int index = 0;
	bool isDuplicate = true;
	{
		std::unique_lock lock(m_paramMutex);
		while (isDuplicate)
		{
			isDuplicate = false;
			for (const auto parm : Parameters)
			{
				if (parm->name == valueName)
				{
					isDuplicate = true;
					valueName = baseName + std::to_string(++index);
					break;
				}
			}
		}
	}
	ConditionParameter* newParameter = new ConditionParameter(0, vType, valueName);
	{
		std::unique_lock lock(m_paramMutex);
		Parameters.push_back(newParameter);
		NotifyParameterLayoutChanged();
	}
	return newParameter;
}

ConditionParameter* Animator::FindParameter(std::string_view valueName)
{
	std::unique_lock lock(m_paramMutex);
	for (const auto parameter : Parameters)
	{
		if (parameter->name == valueName)
		{
			return parameter;
		}
	}
	return nullptr; 
}

bool Animator::ConvertLegacyAvatarMask(AvatarMask& mask)
{
	if (!mask.isHumanoid) return true;
	const assets::ModelSkeletonAsset* skeleton = TypedSkeleton();
	const auto& boneRegions = GetInstance().boneRegions;
	if (!skeleton || boneRegions.size() != skeleton->bones.size()) return false;
	if (mask.m_BoneMasks.empty())
		mask.RootMask = BuildAvatarBoneMasks(mask);
	if (mask.m_BoneMasks.size() != skeleton->bones.size()) return false;

	// Resolve every entry before changing the mask. A partial or stale tree
	// remains readable through its legacy flags instead of silently losing bones.
	std::vector<float> weights;
	weights.reserve(mask.m_BoneMasks.size());
	std::vector<bool> matched(skeleton->bones.size(), false);
	for (const BoneMask* boneMask : mask.m_BoneMasks)
	{
		if (!boneMask) return false;
		auto found = std::find_if(skeleton->bones.begin(), skeleton->bones.end(),
			[&](const auto& bone) { return bone.name == boneMask->boneName; });
		if (found == skeleton->bones.end()) return false;
		const std::size_t boneIndex = static_cast<std::size_t>(found - skeleton->bones.begin());
		if (matched[boneIndex]) return false;
		matched[boneIndex] = true;
		weights.push_back(mask.IsBoneEnabled(
			static_cast<BoneRegion>(boneRegions[boneIndex])) ? 1.f : 0.f);
	}
	for (std::size_t i = 0; i < mask.m_BoneMasks.size(); ++i)
	{
		mask.m_BoneMasks[i]->isEnabled = true;
		mask.m_BoneMasks[i]->weight = weights[i];
	}
	mask.isHumanoid = false;
	return true;
}

std::size_t Animator::FindParameterIndex(std::string_view valueName) const
{
	std::lock_guard lock(m_paramMutex);
	for (std::size_t i = 0; i < Parameters.size(); ++i)
		if (Parameters[i] && Parameters[i]->name == valueName) return i;
	return static_cast<std::size_t>(-1);
}

ConditionParameter* Animator::ParameterAt(std::size_t index) const noexcept
{
	// AnimatorSystem updates states on the game thread. Parameter structural
	// edits also occur there; the cached index is never a raw owning pointer.
	return index < Parameters.size() ? Parameters[index] : nullptr;
}





void Animator::OnDeserialized(const Authoring::NodeView& view)
{
	const Authoring::ReadNode node = Authoring::NodeViewAccess::Node(view);
	SetSelectedClipIndex(m_AnimIndexChosen);
	// CT6-d: 구 ComponentFactory Animator 분기 이동(동작·순서 보존).
	// Parameters·m_animationControllers는 포인터 원소 벡터라 typed 역직렬화가
	// 건드리지 않는다 — 여기의 수동 복원이 실채움이다.
	Model* model = nullptr;
	std::vector<bool> animationBools;
	std::unordered_map<int, std::vector<KeyFrameEvent>> animationKeyFrameMap;
	int aniIndex = 0;
	// I6-B1 — 클립 오버라이드 표기가 Animator 소유로 이주했다. 새 정본을
	// 먼저 읽고, 없을 때만 구 씬의 legacy Skeleton 서브트리를 읽는다. 폴백은
	// **읽기 전용**이다 — 저장은 아래 OnAfterSerialize가 새 표기로만 한다.
	bool authoredOverrides = false;
	if (node["m_clipOverrides"])
	{
		authoredOverrides = true;
		for (const auto entry : node["m_clipOverrides"])
		{
			if (!entry["clipIndex"]) continue;
			const int clipIndex = entry["clipIndex"].As<int>();
			if (clipIndex < 0) continue;
			AnimatorClipOverride& clipOverride = EnsureClipOverride(clipIndex);
			if (entry["loopOverride"])
			{
				clipOverride.loopOverride = entry["loopOverride"].As<bool>();
			}
			if (entry["events"])
			{
				for (const auto keyFrameEvent : entry["events"])
				{
					KeyFrameEvent newEvent;
					Meta::Deserialize(&newEvent, keyFrameEvent);
					clipOverride.events.push_back(newEvent);
				}
			}
		}
	}

	if (!authoredOverrides && node["m_Skeleton"])
	{
		// 구 씬 폴백 — 인덱스는 서브트리의 **순서**가 정한다(이름이 아니다).
		// 그 규약은 저장 당시 자산 클립 순서와 1:1이라는 전제 위에 섰고,
		// 새 표기는 clipIndex를 명시해 그 암묵 전제를 없앤다.
		const auto skel = node["m_Skeleton"];
		if (skel["m_animations"])
		{
			const auto animations = skel["m_animations"];
			for (const auto animation : animations)
			{
				bool _aniBool = animation["m_isLoop"].As<bool>();
				animationBools.push_back(_aniBool);
				const auto keyFrameEvents = animation["m_keyFrameEvent"];
				std::vector<KeyFrameEvent> KeyFrameEventVec;
				for (const auto keyFrameEvent : keyFrameEvents)
				{
					KeyFrameEvent newEvent;
					Meta::Deserialize(&newEvent, keyFrameEvent);
					KeyFrameEventVec.push_back(newEvent);
				}
				if (!KeyFrameEventVec.empty())
				{
					animationKeyFrameMap[aniIndex] = KeyFrameEventVec;
				}
				aniIndex++;
			}
		}
	}

	if (node["m_Motion"])
	{
		FileGuid guid = node["m_Motion"].AsString();
		if (guid != nullFileGuid) m_Motion = guid;
	}

	// I5-D4e-2 — 씬이 저장한 클립별 isLoop·이벤트를 **자기 소유 오버라이드**로
	// 보관한다. 구 코드는 공유 자산(m_Skeleton->m_animations)에 재주입해 같은
	// 스켈레톤을 공유하는 Animator 간 오염(마지막 로드 승자)을 만들었다 — 이제
	// 자산은 불변이고, 재생 루프 판정(IsClipLooping)·발화(InvokeClipEvents)·
	// writer(OnAfterSerialize)가 이 오버라이드를 정본으로 본다.
	for (int i = 0; i < static_cast<int>(animationBools.size()); ++i)
	{
		EnsureClipOverride(i).loopOverride = animationBools[i];
	}
	for (auto& [clipIndex, events] : animationKeyFrameMap)
	{
		EnsureClipOverride(clipIndex).events = std::move(events);
	}

	// typed 재생 바인딩. m_Motion 복원 직후, 아래 컨트롤러 복원 **이전**이어야
	// 한다 — AvatarMask 재생성(BuildAvatarBoneMasks)이 generation을 읽는다.
	EnsureAnimationBinding();
	if (FileGuid{} != m_Motion && nullptr == TypedSkeleton() && !m_granularAnimationBinding)
	{
		Debug::PrintLog(spdlog::level::err, "[Animator] 모델 generation의 스켈레톤을 붙들지 못했다: "
			+ m_Motion.ToString());
	}

	if (node["Parameters"])
	{
		const auto paramNode = node["Parameters"];

		for (const auto param : paramNode)
		{
			ConditionParameter* aniParam = new ConditionParameter();
			Meta::Deserialize(aniParam, param);
			Parameters.push_back(aniParam);
		}
	}

	if (node["m_animationControllers"])
	{
		const auto animationControllerNode = node["m_animationControllers"];

		for (const auto layer : animationControllerNode)
		{
			std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
			Meta::Deserialize(animationController.get(), layer);
			animationController->m_owner = this;
			if (animationController->useMask == true)
			{
				if (layer["m_avatarMask"])
				{
					const auto MaskNode = layer["m_avatarMask"];
                    AvatarMask avatarMask;
                    DeferredAvatarMask deferred;
                    deferred.controller = animationController;
                    const bool deferMask = m_granularAnimationBinding && !TypedSkeleton();
                    Meta::Deserialize(&avatarMask, MaskNode);
					// I5-D4e-3 — 마스크 트리 생성 창구(experiment 정본·legacy 폴백,
					// m_BoneMasks 순서는 legacy DFS 선순 재현 — 저장분 인덱스 대응).
					avatarMask.RootMask = BuildAvatarBoneMasks(avatarMask);
					if (MaskNode["m_BoneMasks"])
					{
						const auto boneMaskNode = MaskNode["m_BoneMasks"];
						int i = 0;

						for (const auto boneMask : boneMaskNode)
						{
							BoneMask authored;
                            Meta::Deserialize(&authored, boneMask);
                            if (deferMask)
                            {
                                deferred.bones.push_back({ authored.boneName,
                                    authored.isEnabled, authored.weight });
                            }
                            BoneMask* target = nullptr;
							if (!authored.boneName.empty())
							{
								for (BoneMask* candidate : avatarMask.m_BoneMasks)
									if (candidate && candidate->boneName == authored.boneName)
									{ target = candidate; break; }
							}
							else if (i < static_cast<int>(avatarMask.m_BoneMasks.size()))
								target = avatarMask.m_BoneMasks[i]; // legacy index-only scene
							if (target)
							{
								target->isEnabled = authored.isEnabled;
								target->weight = authored.weight;
							}
							i++;
						}
					}
                    animationController->ReCreateMask(&avatarMask);
                    if (deferMask)
                    {
                        m_deferredAvatarMasks.push_back(std::move(deferred));
                    }
				}
			}
			if (layer["StateVec"])
			{
				const auto StatesNode = layer["StateVec"];
				for (const auto state : StatesNode)
				{
					std::shared_ptr<AnimationState> sharedState = std::make_shared<AnimationState>();
					Meta::Deserialize(sharedState.get(), state);
					animationController->StateVec.push_back(sharedState);
					animationController->StateNameSet.insert(sharedState->m_name);
					animationController->m_nameToState.insert(std::make_pair(sharedState->m_name, sharedState));
					sharedState->m_ownerController = animationController.get();
					sharedState->SetBehaviour(sharedState->behaviourName);
					if (sharedState->m_isAny)
						animationController->m_anyState = sharedState;
					if (state["Transitions"])
					{
						const auto transitionNode = state["Transitions"];

						for (const auto transition : transitionNode)
						{
							std::shared_ptr<AniTransition> sharedTransition = std::make_shared<AniTransition>();
							Meta::Deserialize(sharedTransition.get(), transition);
							sharedState->Transitions.push_back(sharedTransition);
							sharedTransition->m_ownerController = animationController.get();

							// TransCondition은 값 벡터라 typed deserializer가 이미 채운다.
							// 여기서는 런타임 전용 owner/parameter 포인터만 다시 결합한다.
							for (TransCondition& condition : sharedTransition->conditions)
							{
								condition.m_ownerController = animationController.get();
								condition.SetValue(condition.valueName);
							}
						}
					}
				}
			}
			if (layer["m_curState"])
			{
				const auto curNode = layer["m_curState"];
				if (curNode.IsNull() == false)
				{
					std::string name = curNode["m_name"].AsString();
					animationController->SetCurState(name);
				}
			}

			for (const auto state : animationController->StateVec)
			{
				for (const auto transition : state->Transitions)
				{
					transition->SetCurState(transition->curStateName);
					transition->SetNextState(transition->nextStateName);
				}
			}

			m_animationControllers.push_back(animationController);
		}
	}
}

void Animator::OnBeforeSerialize()
{
	m_AnimIndexChosen = GetSelectedClipIndex();
}

void Animator::OnAfterSerialize(const Authoring::MutableNodeView& view)
{
	// I6-B1 — 오버라이드가 자기 표기를 갖는다. 구 코드는 리플렉션이 legacy
	// Skeleton 포인터를 따라 적은 서브트리에 값을 **되입혔다** — 저장 형상이
	// 은퇴 대상 타입의 형상이라, 그 타입이 죽는 순간 저작분이 갈 곳을 잃는다.
	// 이제 소유(Animator)와 표기가 같은 곳을 가리킨다.
	//
	// ★ 인덱스를 명시한다. 구 표기는 서브트리 **순서**가 인덱스였고, 그것은
	//   저장 당시 자산 클립 순서와 1:1이라는 암묵 전제 위에 섰다.
	//
	// 빈 오버라이드(값도 이벤트도 없는 것)는 적지 않는다 — 왕복 멱등이
	// 이 게이트의 판정이라 쓰는 것과 읽는 것이 정확히 같아야 한다.
	if (m_clipOverrides.empty()) return;

	const Authoring::WriteNode node = Authoring::MutableNodeViewAccess::Node(view);
	Authoring::WriteNode overridesNode;
	for (AnimatorClipOverride& clipOverride : m_clipOverrides)
	{
		if (clipOverride.clipIndex < 0) continue;
		if (!clipOverride.loopOverride.has_value()
			&& clipOverride.events.empty())
		{
			continue;
		}

		if (!overridesNode)
		{
			overridesNode = node.Child("m_clipOverrides");
			overridesNode.SetSequence();
		}
		const Authoring::WriteNode entry = overridesNode.Append();
		entry.SetMap();
		entry.Child("clipIndex").SetScalar(clipOverride.clipIndex);
		if (clipOverride.loopOverride.has_value())
		{
			entry.Child("loopOverride").SetScalar(*clipOverride.loopOverride);
		}
		if (!clipOverride.events.empty())
		{
			const Authoring::WriteNode eventsNode = entry.Child("events");
			eventsNode.SetSequence();
			for (KeyFrameEvent& event : clipOverride.events)
			{
				Meta::SerializeInto(&event, eventsNode.Append());
			}
		}
	}
}

