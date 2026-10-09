#pragma once

#include "../../RHI/RHIHandle.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

// Shared TU/FG values. Native resources, SDK contexts and command objects live
// only under RHI/Temporal. A supported adapter is not an active product feature.
enum class TemporalProvider : uint8_t { None, Fsr, Dlss, XeSS };
enum class TemporalBackend : uint8_t { DX12, Vulkan };
enum class TemporalQuality : uint8_t { NativeAA, Quality, Balanced, Performance, UltraPerformance };
enum class TemporalStatus : uint8_t
{
    Success,
    NotQueried,
    SdkNotBuilt,
    RuntimeUnavailable,
    SdkVersionMismatch,
    BackendUnsupported,
    FeatureUnsupported,
    InvalidInput,
    NotInitialized,
    AlreadyInitialized,
    SdkFailure,
    IntegrationRequired,
    EditorViewportForbidden
};

struct TemporalResult
{
    TemporalStatus status{ TemporalStatus::NotQueried };
    int64_t nativeCode{ 0 };

    bool IsSuccess() const { return status == TemporalStatus::Success; }
};

struct TemporalExtent
{
    uint32_t width{ 0 };
    uint32_t height{ 0 };

    bool IsValid() const { return width != 0 && height != 0; }
    bool operator==(const TemporalExtent&) const = default;
};

// All matrices are UNJITTERED, row-major, and follow the SDK-neutral camera
// space supplied by the renderer. Adapters translate layout/conventions where
// necessary; zero-filled matrices never count as valid camera data.
struct TemporalCamera
{
    std::array<float, 16> viewMatrix{};
    std::array<float, 16> projectionMatrix{};
    std::array<float, 16> cameraViewToClip{};
    std::array<float, 16> clipToCameraView{};
    std::array<float, 16> clipToPreviousClip{};
    std::array<float, 16> previousClipToClip{};
    std::array<float, 3> position{};
    std::array<float, 3> up{};
    std::array<float, 3> right{};
    std::array<float, 3> forward{};
    float aspectRatio{ 0.0f };
    bool valid{ false };
    bool orthographicProjection{ false };
};

struct TemporalFrame
{
    // Simulation/real-frame identity, never an interpolated presentation index.
    uint64_t realFrameId{ 0 };
    uint64_t historyRevision{ 0 };
    TemporalExtent renderExtent;
    TemporalExtent displayExtent;
    // Render-resolution pixel jitter, centered on zero. Motion maps current
    // pixels to previous pixels, includes camera motion, and excludes jitter.
    // Pixel axes originate top-left, +X right and +Y down.
    float jitterX{ 0.0f };
    float jitterY{ 0.0f };
    float motionVectorScaleX{ 0.0f };
    float motionVectorScaleY{ 0.0f };
    float frameTimeMilliseconds{ 0.0f };
    float cameraNear{ 0.0f };
    float cameraFar{ 0.0f };
    float cameraVerticalFov{ 0.0f }; // Radians.
    float viewSpaceToMeters{ 1.0f };
    float preExposure{ 1.0f };
    bool reset{ true };
    bool depthInverted{ false };
    bool depthInfinite{ false };
    bool highDynamicRange{ true };
    bool motionVectorsAtDisplayResolution{ false };
    bool motionVectorsDilated{ false };
    TemporalCamera camera;
};

// Handles retain the existing RHI/graph ownership. This description does not
// acquire a lease or make a resource safe to reuse. The integration layer must
// retain graph resources through actual SDK/queue final consumption.
struct TemporalUpscaleInputs
{
    TemporalFrame frame;
    RHITextureHandle color; // Pre-tone-map linear HDR.
    RHITextureHandle depth;
    RHITextureHandle motionVectors;
    RHITextureHandle exposure; // Optional 1x1 exposure texture.
    RHITextureHandle reactiveMask;
    RHITextureHandle transparencyMask;
    RHITextureHandle responsiveMask; // History-weighting input; not a reactive-mask alias.
    RHITextureHandle output; // Display-resolution linear HDR, before UI.
};

enum class TemporalPresentationTarget : uint8_t { Unbound, PlayerSwapchain, EditorViewport };
enum class TemporalTransferFunction : uint8_t { SRGB, PQ, Linear };

struct TemporalFrameGenerationConfig
{
    TemporalPresentationTarget target{ TemporalPresentationTarget::Unbound };
    TemporalExtent displayExtent;
    TemporalTransferFunction transferFunction{ TemporalTransferFunction::SRGB };
    float minLuminance{ 0.0f };
    float maxLuminance{ 0.0f };
    uint32_t interpolatedFrameCount{ 1 };
};

struct TemporalFrameGenerationInputs
{
    TemporalFrame frame;
    RHITextureHandle hudlessColor;
    RHITextureHandle uiColor; // Separate UI, never baked into interpolation input.
    RHITextureHandle depth;
    RHITextureHandle motionVectors;
};

struct TemporalCapabilities
{
    TemporalProvider provider{ TemporalProvider::None };
    TemporalBackend backend{ TemporalBackend::DX12 };
    TemporalResult upscaling;
    TemporalResult frameGeneration;
    uint32_t maxInterpolatedFrames{ 0 };
    // Static SDK version/commit literals owned by the adapter, not DLL paths.
    const char* sdkVersion{ nullptr };
    const char* sdkRevision{ nullptr };
    // Runtime-selected implementations can differ from the header SDK version.
    // Own the text so a capability snapshot survives the SDK query buffer.
    std::string upscalerImplementation;
    std::string frameGeneratorImplementation;
};

// Independent choices deliberately permit DLSS upscaling with FSR FG.
// Selection is eligibility, not activation: input validation, context creation,
// ownership and graph/presenter integration must succeed separately. None means
// native-resolution rendering / real-frame presentation, never a low-resolution
// texture mislabeled as a reconstructed output.
struct TemporalSelection
{
    TemporalProvider upscaler{ TemporalProvider::None };
    TemporalProvider frameGenerator{ TemporalProvider::None };
    TemporalResult requestedUpscaler;
    TemporalResult requestedFrameGenerator;
};

TemporalResult ValidateTemporalFrame(const TemporalFrame& frame);
TemporalResult ValidateTemporalCamera(const TemporalCamera& camera);
TemporalResult ValidateTemporalUpscaleInputs(const TemporalUpscaleInputs& inputs);
TemporalResult ValidateTemporalFrameGenerationConfig(const TemporalFrameGenerationConfig& config);
TemporalSelection SelectTemporalProviders(TemporalProvider requestedUpscaler,
    TemporalProvider requestedFrameGenerator, TemporalBackend backend,
    std::span<const TemporalCapabilities> capabilities);
