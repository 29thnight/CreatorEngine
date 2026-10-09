#include "FsrBackend.h"

namespace
{
constexpr const char* kFsrVersion = "FidelityFX SDK 1.1.4 / FSR 3.1.4";
constexpr const char* kFsrRevision = "c6efa6bf7f2027b3ec94f28578bb5965eabb9e55";
}

TemporalCapabilities QueryFsrBuildAvailability(TemporalBackend backend)
{
    TemporalCapabilities result;
    result.provider = TemporalProvider::Fsr;
    result.backend = backend;
    result.sdkVersion = kFsrVersion;
    result.sdkRevision = kFsrRevision;
    bool built = false;
#if CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_DX12_SDK
    built = backend == TemporalBackend::DX12;
#elif CREATOR_ENABLE_FSR_SDK && CREATOR_ENABLE_FSR_VULKAN_SDK
    built = backend == TemporalBackend::Vulkan;
#endif
    result.upscaling = result.frameGeneration = {
        built ? TemporalStatus::NotQueried : TemporalStatus::SdkNotBuilt, 0 };
    return result;
}

#if CREATOR_ENABLE_FSR_SDK
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

namespace
{
TemporalResult Invalid() { return { TemporalStatus::InvalidInput, 0 }; }
TemporalResult Success() { return { TemporalStatus::Success, 0 }; }

bool ValidDescription(const FsrContextDescription& description)
{
    return description.maxRenderExtent.IsValid() && description.displayExtent.IsValid() &&
        description.maxRenderExtent.width <= description.displayExtent.width &&
        description.maxRenderExtent.height <= description.displayExtent.height &&
        description.displayExtent.width <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) &&
        description.displayExtent.height <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
}

bool Matches(const TemporalFrame& frame, const FsrContextDescription& description)
{
    return frame.renderExtent.width <= description.maxRenderExtent.width &&
        frame.renderExtent.height <= description.maxRenderExtent.height &&
        frame.displayExtent == description.displayExtent && frame.depthInverted == description.depthInverted &&
        frame.depthInfinite == description.depthInfinite && frame.highDynamicRange == description.highDynamicRange &&
        frame.motionVectorsAtDisplayResolution == description.motionVectorsAtDisplayResolution;
}

bool IsTexture(const FfxResource& resource, TemporalExtent extent)
{
    return resource.resource && resource.description.type == FFX_RESOURCE_TYPE_TEXTURE2D &&
        resource.description.width == extent.width && resource.description.height == extent.height;
}

bool OptionalTexture(const FfxResource& resource, TemporalExtent extent)
{
    return !resource.resource || IsTexture(resource, extent);
}

bool ResetHistory(const TemporalFrame& frame, const TemporalFrame& previous, bool hasHistory)
{
    return !hasHistory || frame.reset || frame.historyRevision != previous.historyRevision ||
        frame.renderExtent != previous.renderExtent || frame.displayExtent != previous.displayExtent;
}

float CameraNear(const TemporalFrame& frame)
{
    return frame.depthInverted ? (frame.depthInfinite ? std::numeric_limits<float>::max() : frame.cameraFar)
                               : frame.cameraNear;
}

float CameraFar(const TemporalFrame& frame)
{
    return frame.depthInverted ? frame.cameraNear
                               : (frame.depthInfinite ? std::numeric_limits<float>::max() : frame.cameraFar);
}

TemporalResult NoSubmittedWork(void*) { return Success(); }

TemporalResult ReleaseResources(FsrBackendStorage& storage, FfxResourceInternal* resources, uint32_t& count)
{
    auto& interface = storage.GetInterface();
    while (count)
    {
        const auto error = interface.fpDestroyResource(&interface, resources[count - 1], storage.GetContextID());
        if (error != FFX_OK)
            return FsrResult(error);
        resources[--count] = {};
    }
    return Success();
}

FfxResource Resource(FsrBackendStorage& storage, FfxResourceInternal resource)
{
    auto& interface = storage.GetInterface();
    return interface.fpGetResource(&interface, resource);
}

FfxBackbufferTransferFunction TransferFunction(TemporalTransferFunction value)
{
    switch (value)
    {
    case TemporalTransferFunction::PQ: return FFX_BACKBUFFER_TRANSFER_FUNCTION_PQ;
    case TemporalTransferFunction::Linear: return FFX_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
    default: return FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    }
}
}

TemporalResult FsrResult(FfxErrorCode error)
{
    return { error == FFX_OK ? TemporalStatus::Success : TemporalStatus::SdkFailure, static_cast<int64_t>(error) };
}

FsrBackendStorage::~FsrBackendStorage()
{
    // Releasing CPU scratch while a copied SDK interface is live is a UAF.
    if (m_hasContext)
        std::terminate();
}

FsrBackendStorage* FsrBackendStorage::GetOwner(FfxInterface* interface)
{
    FsrBackendStorage* owner = nullptr;
    std::memcpy(&owner, static_cast<std::byte*>(interface->scratchBuffer) - sizeof(std::max_align_t), sizeof(owner));
    return owner;
}

FfxErrorCode FsrBackendStorage::CreateContext(FfxInterface* interface, FfxEffect effect,
    FfxEffectBindlessConfig* bindless, FfxUInt32* contextID)
{
    auto* owner = GetOwner(interface);
    if (owner->m_hasContext)
        return FFX_ERROR_INVALID_ARGUMENT;
    const auto error = owner->m_createContext(interface, effect, bindless, contextID);
    if (error == FFX_OK)
    {
        owner->m_contextID = *contextID;
        owner->m_hasContext = true;
    }
    return error;
}

FfxErrorCode FsrBackendStorage::DestroyContext(FfxInterface* interface, FfxUInt32 contextID)
{
    auto* owner = GetOwner(interface);
    const auto error = owner->m_destroyContext(interface, contextID);
    if (error == FFX_OK)
        owner->m_hasContext = false;
    return error;
}

TemporalResult FsrBackendStorage::Initialize(const FsrBackendDevice& device)
{
    if (m_hasContext)
        return { TemporalStatus::AlreadyInitialized, 0 };
    if (ffxFsr3UpscalerGetEffectVersion() != FFX_SDK_MAKE_VERSION(3, 1, 4) ||
        ffxOpticalflowGetEffectVersion() != FFX_SDK_MAKE_VERSION(1, 1, 2) ||
        ffxFrameInterpolationGetEffectVersion() != FFX_SDK_MAKE_VERSION(1, 1, 3))
        return { TemporalStatus::SdkVersionMismatch, 0 };
    if (!device.device || !device.createInterface || !device.scratchBytes ||
        device.scratchBytes > std::numeric_limits<size_t>::max() - 2 * sizeof(std::max_align_t))
        return Invalid();
    m_scratch.assign(2 + device.scratchBytes / sizeof(std::max_align_t), {});
    auto* owner = this;
    std::memcpy(m_scratch.data(), &owner, sizeof(owner));
    m_interface = {};
    const auto error = device.createInterface(&m_interface, device.device, m_scratch.data() + 1,
        device.scratchBytes, 1);
    if (error != FFX_OK)
        return FsrResult(error);
    if (!m_interface.fpGetSDKVersion || !m_interface.fpGetDeviceCapabilities ||
        !m_interface.fpCreateBackendContext || !m_interface.fpDestroyBackendContext ||
        !m_interface.fpCreateResource || !m_interface.fpDestroyResource || !m_interface.fpGetResource)
        return FsrResult(FFX_ERROR_INCOMPLETE_INTERFACE);
    if (m_interface.fpGetSDKVersion(&m_interface) != FFX_SDK_MAKE_VERSION(1, 1, 4))
        return FsrResult(FFX_ERROR_INVALID_VERSION);
    m_createContext = m_interface.fpCreateBackendContext;
    m_destroyContext = m_interface.fpDestroyBackendContext;
    m_interface.fpCreateBackendContext = CreateContext;
    m_interface.fpDestroyBackendContext = DestroyContext;
    return Success();
}

TemporalResult FsrBackendStorage::CreateSharedContext()
{
    if (!m_interface.fpCreateBackendContext)
        return { TemporalStatus::NotInitialized, 0 };
    return FsrResult(m_interface.fpCreateBackendContext(&m_interface, FFX_EFFECT_SHAREDRESOURCES, nullptr, &m_contextID));
}

TemporalResult FsrBackendStorage::DestroySharedContext()
{
    return m_hasContext ? FsrResult(m_interface.fpDestroyBackendContext(&m_interface, m_contextID)) : Success();
}

TemporalResult FsrBackendStorage::QueryDeviceCapabilities(TemporalBackend backend)
{
    if (!m_hasContext)
        return { TemporalStatus::NotInitialized, 0 };
    FfxDeviceCapabilities capabilities{};
    auto result = FsrResult(m_interface.fpGetDeviceCapabilities(&m_interface, &capabilities));
    if (!result.IsSuccess())
        return result;
    // VK reports shader model 5.1 by design; shader-model gating is DX12-only.
    // FP16 is optional in the pinned shaders. No vendor/device ID predicate.
    if (backend == TemporalBackend::DX12 && capabilities.maximumSupportedShaderModel < FFX_SHADER_MODEL_6_0)
        return { TemporalStatus::FeatureUnsupported, 0 };
    return Success();
}

FsrUpscaler::~FsrUpscaler()
{
    if (!Shutdown().IsSuccess())
        std::terminate(); // Never release resources after a failed GPU drain.
}

TemporalResult FsrUpscaler::Initialize(const FsrBackendDevice& device, const FsrContextDescription& description,
    FsrGpuSynchronization synchronization)
{
    if (m_context || m_initialized)
        return { TemporalStatus::AlreadyInitialized, 0 };
    if (!ValidDescription(description) || !synchronization.waitForGpuIdle)
        return Invalid();
    m_description = description;
    m_synchronization = synchronization;
    auto result = m_sharedBackend.Initialize(device);
    if (result.IsSuccess()) result = m_sharedBackend.CreateSharedContext();
    if (result.IsSuccess()) result = m_sharedBackend.QueryDeviceCapabilities(device.backend);
    if (result.IsSuccess()) result = m_effectBackend.Initialize(device);
    if (result.IsSuccess())
    {
        m_context = std::make_unique<FfxFsr3UpscalerContext>();
        FfxFsr3UpscalerContextDescription create{};
        create.maxRenderSize = { description.maxRenderExtent.width, description.maxRenderExtent.height };
        create.maxUpscaleSize = { description.displayExtent.width, description.displayExtent.height };
        create.backendInterface = m_effectBackend.GetInterface();
        create.flags = FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION;
        if (description.depthInverted) create.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
        if (description.depthInfinite) create.flags |= FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE;
        if (description.highDynamicRange) create.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
        if (description.autoExposure) create.flags |= FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
        if (description.motionVectorsAtDisplayResolution)
            create.flags |= FFX_FSR3UPSCALER_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        result = FsrResult(ffxFsr3UpscalerContextCreate(m_context.get(), &create));
    }
    if (result.IsSuccess())
    {
        FfxFsr3UpscalerSharedResourceDescriptions descriptions{};
        result = FsrResult(ffxFsr3UpscalerGetSharedResourceDescriptions(m_context.get(), &descriptions));
        FfxCreateResourceDescription resources[] = { descriptions.dilatedDepth, descriptions.dilatedMotionVectors,
            descriptions.reconstructedPrevNearestDepth };
        auto& interface = m_sharedBackend.GetInterface();
        for (const auto& resource : resources)
        {
            if (!result.IsSuccess()) break;
            result = FsrResult(interface.fpCreateResource(&interface, &resource, m_sharedBackend.GetContextID(),
                &m_shared[m_sharedCount]));
            if (result.IsSuccess()) ++m_sharedCount;
        }
    }
    if (!result.IsSuccess())
    {
        const auto cleanup = Shutdown();
        return cleanup.IsSuccess() ? result : cleanup;
    }
    m_initialized = true;
    return Success();
}

TemporalResult FsrUpscaler::Dispatch(const TemporalFrame& frame, const FsrUpscaleResources& resources, float sharpness)
{
    if (!m_initialized)
        return { TemporalStatus::NotInitialized, 0 };
    auto result = ValidateTemporalFrame(frame);
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    if (!result.IsSuccess()) return result;
    if (frame.camera.orthographicProjection) return { TemporalStatus::FeatureUnsupported, 0 };
    if (!Matches(frame, m_description) || !resources.commandList || !std::isfinite(sharpness) ||
        sharpness < 0.0f || sharpness > 1.0f || (m_hasSuccessfulFrame && frame.realFrameId <= m_lastFrame.realFrameId) ||
        !IsTexture(resources.color, frame.renderExtent) || !IsTexture(resources.depth, frame.renderExtent) ||
        !IsTexture(resources.motionVectors, motionExtent) || !IsTexture(resources.output, frame.displayExtent) ||
        resources.color.resource == resources.output.resource || resources.depth.resource == resources.output.resource ||
        resources.motionVectors.resource == resources.output.resource || resources.exposure.resource == resources.output.resource ||
        resources.reactiveMask.resource == resources.output.resource || resources.transparencyMask.resource == resources.output.resource ||
        !OptionalTexture(resources.exposure, { 1, 1 }) || !OptionalTexture(resources.reactiveMask, frame.renderExtent) ||
        !OptionalTexture(resources.transparencyMask, frame.renderExtent))
        return Invalid();
    FfxFsr3UpscalerDispatchDescription dispatch{};
    dispatch.commandList = resources.commandList;
    dispatch.color = resources.color;
    dispatch.depth = resources.depth;
    dispatch.motionVectors = resources.motionVectors;
    dispatch.exposure = resources.exposure;
    dispatch.reactive = resources.reactiveMask;
    dispatch.transparencyAndComposition = resources.transparencyMask;
    dispatch.output = resources.output;
    dispatch.dilatedDepth = Resource(m_sharedBackend, m_shared[0]);
    dispatch.dilatedMotionVectors = Resource(m_sharedBackend, m_shared[1]);
    dispatch.reconstructedPrevNearestDepth = Resource(m_sharedBackend, m_shared[2]);
    dispatch.jitterOffset = { frame.jitterX, frame.jitterY };
    dispatch.motionVectorScale = { frame.motionVectorScaleX, frame.motionVectorScaleY };
    dispatch.renderSize = { frame.renderExtent.width, frame.renderExtent.height };
    dispatch.upscaleSize = { frame.displayExtent.width, frame.displayExtent.height };
    dispatch.enableSharpening = sharpness > 0.0f;
    dispatch.sharpness = sharpness;
    dispatch.frameTimeDelta = frame.frameTimeMilliseconds;
    dispatch.preExposure = frame.preExposure;
    dispatch.reset = ResetHistory(frame, m_lastFrame, m_hasHistory);
    dispatch.cameraNear = CameraNear(frame);
    dispatch.cameraFar = CameraFar(frame);
    dispatch.cameraFovAngleVertical = frame.cameraVerticalFov;
    dispatch.viewSpaceToMetersFactor = frame.viewSpaceToMeters;
    result = FsrResult(ffxFsr3UpscalerContextDispatch(m_context.get(), &dispatch));
    if (result.IsSuccess())
    {
        m_lastFrame = frame;
        m_hasHistory = m_hasSuccessfulFrame = true;
    }
    else
        m_hasHistory = false; // A failed SDK dispatch may already have mutated history.
    return result;
}

TemporalResult FsrUpscaler::Shutdown()
{
    if (m_initialized)
    {
        const auto result = m_synchronization.waitForGpuIdle(m_synchronization.context);
        if (!result.IsSuccess()) return result;
    }
    auto result = ReleaseResources(m_sharedBackend, m_shared, m_sharedCount);
    if (!result.IsSuccess()) return result;
    // The tracked callback distinguishes a partially-created SDK context from
    // a failure before allocation; never destroy an unrelated default ID zero.
    if (m_context && m_effectBackend.HasContext())
    {
        result = FsrResult(ffxFsr3UpscalerContextDestroy(m_context.get()));
        if (!result.IsSuccess()) return result;
        if (m_effectBackend.HasContext()) return FsrResult(FFX_ERROR_BACKEND_API_ERROR);
    }
    m_context.reset();
    result = m_sharedBackend.DestroySharedContext();
    if (!result.IsSuccess()) return result;
    m_initialized = m_hasHistory = m_hasSuccessfulFrame = false;
    return Success();
}

TemporalResult FsrUpscaler::GetRenderExtent(TemporalExtent display, TemporalQuality quality, TemporalExtent& render)
{
    if (!display.IsValid()) return Invalid();
    if (quality == TemporalQuality::NativeAA)
    {
        render = display; // SDK supports equal input/output dimensions.
        return Success();
    }
    FfxFsr3UpscalerQualityMode mode;
    switch (quality)
    {
    case TemporalQuality::Quality: mode = FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY; break;
    case TemporalQuality::Balanced: mode = FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED; break;
    case TemporalQuality::Performance: mode = FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE; break;
    case TemporalQuality::UltraPerformance: mode = FFX_FSR3UPSCALER_QUALITY_MODE_ULTRA_PERFORMANCE; break;
    default: return Invalid();
    }
    TemporalExtent candidate;
    const auto result = FsrResult(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
        &candidate.width, &candidate.height, display.width, display.height, mode));
    if (result.IsSuccess() && candidate.IsValid()) render = candidate;
    return result.IsSuccess() && !candidate.IsValid() ? Invalid() : result;
}

FsrFrameGenerator::~FsrFrameGenerator()
{
    if (!Shutdown().IsSuccess())
        std::terminate();
}

TemporalResult FsrFrameGenerator::Initialize(const FsrBackendDevice& device, const FsrContextDescription& description,
    const TemporalFrameGenerationConfig& configuration, FfxSurfaceFormat format, FsrGpuSynchronization synchronization)
{
    if (m_opticalFlow || m_interpolation || m_initialized)
        return { TemporalStatus::AlreadyInitialized, 0 };
    auto result = ValidateTemporalFrameGenerationConfig(configuration);
    if (!result.IsSuccess()) return result;
    if (!ValidDescription(description) || !synchronization.waitForGpuIdle || !device.waitForPresents ||
        configuration.displayExtent != description.displayExtent || format == FFX_SURFACE_FORMAT_UNKNOWN)
        return Invalid();
    if (configuration.interpolatedFrameCount != 1)
        return { TemporalStatus::FeatureUnsupported, 0 };
    m_device = device;
    m_description = description;
    m_configuration = configuration;
    m_backBufferFormat = format;
    m_synchronization = synchronization;
    result = m_sharedBackend.Initialize(device);
    if (result.IsSuccess()) result = m_sharedBackend.CreateSharedContext();
    if (result.IsSuccess()) result = m_sharedBackend.QueryDeviceCapabilities(device.backend);
    if (result.IsSuccess()) result = m_opticalFlowBackend.Initialize(device);
    if (result.IsSuccess()) result = m_interpolationBackend.Initialize(device);
    if (result.IsSuccess())
    {
        m_opticalFlow = std::make_unique<FfxOpticalflowContext>();
        FfxOpticalflowContextDescription create{};
        create.backendInterface = m_opticalFlowBackend.GetInterface();
        create.resolution = { description.displayExtent.width, description.displayExtent.height };
        result = FsrResult(ffxOpticalflowContextCreate(m_opticalFlow.get(), &create));
    }
    if (result.IsSuccess())
    {
        m_interpolation = std::make_unique<FfxFrameInterpolationContext>();
        FfxFrameInterpolationContextDescription create{};
        create.backendInterface = m_interpolationBackend.GetInterface();
        create.maxRenderSize = { description.maxRenderExtent.width, description.maxRenderExtent.height };
        create.displaySize = { description.displayExtent.width, description.displayExtent.height };
        create.backBufferFormat = create.previousInterpolationSourceFormat = format;
        if (description.depthInverted) create.flags |= FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED;
        if (description.depthInfinite) create.flags |= FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE;
        if (configuration.transferFunction != TemporalTransferFunction::SRGB)
            create.flags |= FFX_FRAMEINTERPOLATION_ENABLE_HDR_COLOR_INPUT;
        if (description.motionVectorsAtDisplayResolution)
            create.flags |= FFX_FRAMEINTERPOLATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        // Baseline deliberately serial. Async enabling requires additional
        // prepared resource sets, queue ownership and completion tracking.
        result = FsrResult(ffxFrameInterpolationContextCreate(m_interpolation.get(), &create));
    }
    if (result.IsSuccess())
    {
        FfxFrameInterpolationSharedResourceDescriptions frameResources{};
        FfxOpticalflowSharedResourceDescriptions opticalResources{};
        result = FsrResult(ffxFrameInterpolationGetSharedResourceDescriptions(m_interpolation.get(), &frameResources));
        if (result.IsSuccess())
            result = FsrResult(ffxOpticalflowGetSharedResourceDescriptions(m_opticalFlow.get(), &opticalResources));
        FfxCreateResourceDescription resources[] = { frameResources.dilatedDepth, frameResources.dilatedMotionVectors,
            frameResources.reconstructedPrevNearestDepth, opticalResources.opticalFlowVector, opticalResources.opticalFlowSCD };
        auto& interface = m_sharedBackend.GetInterface();
        for (const auto& resource : resources)
        {
            if (!result.IsSuccess()) break;
            result = FsrResult(interface.fpCreateResource(&interface, &resource, m_sharedBackend.GetContextID(),
                &m_shared[m_sharedCount]));
            if (result.IsSuccess()) ++m_sharedCount;
        }
    }
    if (!result.IsSuccess())
    {
        const auto cleanup = Shutdown();
        return cleanup.IsSuccess() ? result : cleanup;
    }
    if (!m_interpolationBackend.GetInterface().fpSwapChainConfigureFrameGeneration)
    {
        Shutdown();
        return FsrResult(FFX_ERROR_INCOMPLETE_INTERFACE);
    }
    m_initialized = true;
    return Success();
}

TemporalResult FsrFrameGenerator::Prepare(const TemporalFrame& frame, const FsrFrameGenerationResources& resources)
{
    std::lock_guard lock(m_mutex);
    if (!m_initialized) return { TemporalStatus::NotInitialized, 0 };
    if (m_framePending) return { TemporalStatus::IntegrationRequired, 0 };
    auto result = ValidateTemporalFrame(frame);
    if (result.IsSuccess()) result = ValidateTemporalCamera(frame.camera);
    if (!result.IsSuccess()) return result;
    const auto motionExtent = frame.motionVectorsAtDisplayResolution ? frame.displayExtent : frame.renderExtent;
    if (frame.camera.orthographicProjection) return { TemporalStatus::FeatureUnsupported, 0 };
    if (!Matches(frame, m_description) || !resources.commandList ||
        (m_hasSuccessfulFrame && frame.realFrameId <= m_lastFrame.realFrameId) ||
        !IsTexture(resources.depth, frame.renderExtent) || !IsTexture(resources.motionVectors, motionExtent) ||
        !IsTexture(resources.hudlessColor, frame.displayExtent) ||
        resources.hudlessColor.description.format != m_backBufferFormat)
        return Invalid();
    FfxFrameInterpolationPrepareDescription prepare{};
    prepare.commandList = resources.commandList;
    prepare.depth = resources.depth;
    prepare.motionVectors = resources.motionVectors;
    prepare.renderSize = { frame.renderExtent.width, frame.renderExtent.height };
    prepare.jitterOffset = { frame.jitterX, frame.jitterY };
    prepare.motionVectorScale = { frame.motionVectorScaleX, frame.motionVectorScaleY };
    prepare.frameTimeDelta = frame.frameTimeMilliseconds;
    prepare.cameraNear = CameraNear(frame);
    prepare.cameraFar = CameraFar(frame);
    prepare.cameraFovAngleVertical = frame.cameraVerticalFov;
    prepare.viewSpaceToMetersFactor = frame.viewSpaceToMeters;
    // SDK ID is contiguous; the real simulation ID remains separate metadata.
    prepare.frameID = m_sdkFrameID;
    std::copy(frame.camera.position.begin(), frame.camera.position.end(), prepare.cameraPosition);
    std::copy(frame.camera.up.begin(), frame.camera.up.end(), prepare.cameraUp);
    std::copy(frame.camera.right.begin(), frame.camera.right.end(), prepare.cameraRight);
    std::copy(frame.camera.forward.begin(), frame.camera.forward.end(), prepare.cameraForward);
    prepare.dilatedDepth = Resource(m_sharedBackend, m_shared[0]);
    prepare.dilatedMotionVectors = Resource(m_sharedBackend, m_shared[1]);
    prepare.reconstructedPrevDepth = Resource(m_sharedBackend, m_shared[2]);
    result = FsrResult(ffxFrameInterpolationPrepare(m_interpolation.get(), &prepare));
    if (result.IsSuccess())
    {
        m_preparedFrame = frame;
        m_hudlessColor = resources.hudlessColor;
        m_reset = ResetHistory(frame, m_lastFrame, m_hasHistory);
        m_framePending = true;
        m_generated = false;
        m_generationError = FFX_OK;
    }
    else
        m_hasHistory = false;
    return result;
}

TemporalResult FsrFrameGenerator::Configure(FfxSwapchain swapchain)
{
    // Producer calls are externally serialized. Do not hold the callback mutex
    // across SDK configuration: the swapchain may wait for an earlier callback.
    if (!m_initialized) return { TemporalStatus::NotInitialized, 0 };
    if (!m_framePending || !swapchain || (m_swapchain && m_swapchain != swapchain)) return Invalid();
    FfxFrameGenerationConfig configure{};
    configure.swapChain = swapchain;
    configure.frameGenerationEnabled = true;
    configure.frameGenerationCallback = Generate;
    configure.frameGenerationCallbackContext = this;
    configure.HUDLessColor = m_hudlessColor;
    configure.frameID = m_sdkFrameID;
    configure.interpolationRect = { 0, 0, static_cast<int32_t>(m_configuration.displayExtent.width),
        static_cast<int32_t>(m_configuration.displayExtent.height) };
    const auto result = FsrResult(m_interpolationBackend.GetInterface().fpSwapChainConfigureFrameGeneration(&configure));
    if (result.IsSuccess())
    {
        m_swapchain = swapchain;
        m_configured = true;
    }
    return result;
}

FfxErrorCode FsrFrameGenerator::Generate(const FfxFrameGenerationDispatchDescription* description, void* context)
{
    if (!description || !context) return FFX_ERROR_INVALID_POINTER;
    const auto error = static_cast<FsrFrameGenerator*>(context)->DispatchGeneratedFrame(*description);
    if (error != FFX_OK)
    {
        // SDK 1.1.4's DX12/VK callers pass a mutable local descriptor but
        // publish a const callback type. They drop failed commands yet decide
        // whether to present output from this count. Suppress stale output.
        const_cast<FfxFrameGenerationDispatchDescription*>(description)->numInterpolatedFrames = 0;
        auto* owner = static_cast<FsrFrameGenerator*>(context);
        std::lock_guard lock(owner->m_mutex);
        owner->m_generationError = error;
        owner->m_hasHistory = false;
    }
    return error;
}

FfxErrorCode FsrFrameGenerator::DispatchGeneratedFrame(const FfxFrameGenerationDispatchDescription& description)
{
    std::lock_guard lock(m_mutex);
    if (!m_initialized || !m_framePending || m_generated || description.frameID != m_sdkFrameID ||
        description.numInterpolatedFrames != 1 || !description.commandList ||
        !IsTexture(description.presentColor, m_configuration.displayExtent) ||
        !IsTexture(description.outputs[0], m_configuration.displayExtent))
        return FFX_ERROR_INVALID_ARGUMENT;
    FfxOpticalflowDispatchDescription opticalFlow{};
    opticalFlow.commandList = description.commandList;
    opticalFlow.color = m_hudlessColor;
    opticalFlow.reset = description.reset || m_reset;
    opticalFlow.backbufferTransferFunction = TransferFunction(m_configuration.transferFunction);
    opticalFlow.minMaxLuminance = { m_configuration.minLuminance, m_configuration.maxLuminance };
    opticalFlow.opticalFlowVector = Resource(m_sharedBackend, m_shared[3]);
    opticalFlow.opticalFlowSCD = Resource(m_sharedBackend, m_shared[4]);
    auto error = ffxOpticalflowContextDispatch(m_opticalFlow.get(), &opticalFlow);
    if (error != FFX_OK) return error;
    FfxFrameInterpolationDispatchDescription dispatch{};
    dispatch.commandList = description.commandList;
    dispatch.displaySize = { m_configuration.displayExtent.width, m_configuration.displayExtent.height };
    dispatch.renderSize = { m_preparedFrame.renderExtent.width, m_preparedFrame.renderExtent.height };
    dispatch.currentBackBuffer = description.presentColor;
    dispatch.currentBackBuffer_HUDLess = m_hudlessColor;
    dispatch.output = description.outputs[0]; // SDK-provided swapchain interpolation target.
    dispatch.interpolationRect = description.interpolationRect;
    dispatch.opticalFlowVector = opticalFlow.opticalFlowVector;
    dispatch.opticalFlowSceneChangeDetection = opticalFlow.opticalFlowSCD;
    dispatch.opticalFlowBufferSize = { opticalFlow.opticalFlowVector.description.width,
        opticalFlow.opticalFlowVector.description.height };
    dispatch.opticalFlowScale = { 1.0f / dispatch.displaySize.width, 1.0f / dispatch.displaySize.height };
    dispatch.opticalFlowBlockSize = 8; // Pinned Optical Flow API output block size.
    dispatch.cameraNear = CameraNear(m_preparedFrame);
    dispatch.cameraFar = CameraFar(m_preparedFrame);
    dispatch.cameraFovAngleVertical = m_preparedFrame.cameraVerticalFov;
    dispatch.viewSpaceToMetersFactor = m_preparedFrame.viewSpaceToMeters;
    dispatch.frameTimeDelta = m_preparedFrame.frameTimeMilliseconds;
    dispatch.reset = opticalFlow.reset;
    dispatch.backBufferTransferFunction = TransferFunction(m_configuration.transferFunction);
    dispatch.minMaxLuminance[0] = m_configuration.minLuminance;
    dispatch.minMaxLuminance[1] = m_configuration.maxLuminance;
    dispatch.frameID = m_sdkFrameID;
    dispatch.dilatedDepth = Resource(m_sharedBackend, m_shared[0]);
    dispatch.dilatedMotionVectors = Resource(m_sharedBackend, m_shared[1]);
    dispatch.reconstructedPrevDepth = Resource(m_sharedBackend, m_shared[2]);
    error = ffxFrameInterpolationDispatch(m_interpolation.get(), &dispatch);
    m_generated = error == FFX_OK;
    return error;
}

TemporalResult FsrFrameGenerator::Drain()
{
    // Do not hold m_mutex while the SDK waits on its callback/presentation thread.
    if (m_swapchain)
    {
        const auto result = FsrResult(m_device.waitForPresents(m_swapchain));
        if (!result.IsSuccess()) return result;
    }
    return m_synchronization.waitForGpuIdle(m_synchronization.context);
}

TemporalResult FsrFrameGenerator::FinishFrame()
{
    if (!m_initialized) return { TemporalStatus::NotInitialized, 0 };
    const auto result = Drain();
    if (!result.IsSuccess()) return result;
    std::lock_guard lock(m_mutex);
    if (!m_framePending) return Success();
    if (m_generated)
    {
        m_lastFrame = m_preparedFrame;
        m_hasHistory = m_hasSuccessfulFrame = true;
    }
    else
        m_hasHistory = false;
    m_framePending = false;
    m_hudlessColor = {};
    ++m_sdkFrameID;
    if (m_generationError != FFX_OK) return FsrResult(m_generationError);
    return m_generated ? Success() : TemporalResult{ TemporalStatus::IntegrationRequired, 0 };
}

TemporalResult FsrFrameGenerator::Shutdown()
{
    if (m_initialized)
    {
        if (m_configured)
        {
            FfxFrameGenerationConfig disable{};
            disable.swapChain = m_swapchain;
            const auto result = FsrResult(m_interpolationBackend.GetInterface().fpSwapChainConfigureFrameGeneration(&disable));
            if (!result.IsSuccess()) return result;
            m_configured = false;
        }
        const auto result = Drain();
        if (!result.IsSuccess()) return result;
    }
    auto result = ReleaseResources(m_sharedBackend, m_shared, m_sharedCount);
    if (!result.IsSuccess()) return result;
    if (m_interpolation && m_interpolationBackend.HasContext())
    {
        result = FsrResult(ffxFrameInterpolationContextDestroy(m_interpolation.get()));
        if (!result.IsSuccess()) return result;
        if (m_interpolationBackend.HasContext()) return FsrResult(FFX_ERROR_BACKEND_API_ERROR);
    }
    m_interpolation.reset();
    if (m_opticalFlow && m_opticalFlowBackend.HasContext())
    {
        result = FsrResult(ffxOpticalflowContextDestroy(m_opticalFlow.get()));
        if (!result.IsSuccess()) return result;
        if (m_opticalFlowBackend.HasContext()) return FsrResult(FFX_ERROR_BACKEND_API_ERROR);
    }
    m_opticalFlow.reset();
    result = m_sharedBackend.DestroySharedContext();
    if (!result.IsSuccess()) return result;
    m_initialized = m_framePending = m_hasHistory = m_hasSuccessfulFrame = m_generated = false;
    m_swapchain = nullptr;
    m_hudlessColor = {};
    return Success();
}

TemporalCapabilities QueryFsrCapabilities(const FsrBackendDevice& device, const FsrContextDescription& description,
    const TemporalFrameGenerationConfig& configuration, FfxSurfaceFormat format)
{
    auto result = QueryFsrBuildAvailability(device.backend);
    if (result.upscaling.status != TemporalStatus::NotQueried) return result;
    const FsrGpuSynchronization synchronization{ NoSubmittedWork, nullptr };
    auto upscaler = std::make_unique<FsrUpscaler>();
    auto generator = std::make_unique<FsrFrameGenerator>();
    result.upscaling = upscaler->Initialize(device, description, synchronization);
    result.frameGeneration = generator->Initialize(device, description, configuration, format, synchronization);
    if (result.upscaling.IsSuccess()) result.upscalerImplementation = "AMD FSR 3.1.4 source";
    if (result.frameGeneration.IsSuccess())
    {
        result.maxInterpolatedFrames = 1;
        result.frameGeneratorImplementation = "AMD FSR 3.1.4 Optical Flow + Frame Interpolation source";
    }
    const auto upscaleCleanup = upscaler->Shutdown();
    const auto generationCleanup = generator->Shutdown();
    if (!upscaleCleanup.IsSuccess()) { result.upscaling = upscaleCleanup; upscaler.release(); }
    if (!generationCleanup.IsSuccess()) { result.frameGeneration = generationCleanup; generator.release(); }
    return result;
}
#endif
