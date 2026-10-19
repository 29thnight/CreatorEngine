#include "shadow_camera_prediction.generated.cpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    class CpuDepthTexture final : public ITexture
    {
      public:
        void Reset(std::uint32_t width, std::uint32_t height, float depth)
        {
            m_width = width;
            m_height = height;
            m_depth = depth;
            m_holeX = m_holeY = -1;
            m_loads = 0;
        }

        void SetHole(int x, int y, float depth)
        {
            m_holeX = x;
            m_holeY = y;
            m_holeDepth = depth;
        }

        std::uint64_t Loads() const { return m_loads; }

        TextureDimensions GetDimensions(int = -1) override
        {
            TextureDimensions dimensions{};
            dimensions.shape = SLANG_TEXTURE_2D;
            dimensions.width = m_width;
            dimensions.height = m_height;
            dimensions.numberOfLevels = 1;
            return dimensions;
        }

        void Load(const int32_t* location, void* output, size_t size) override
        {
            if (size != sizeof(float) || location[0] < 0 || location[1] < 0 || location[2] != 0 ||
                std::uint32_t(location[0]) >= m_width || std::uint32_t(location[1]) >= m_height)
            {
                throw std::runtime_error("Production HZB helper addressed an undeclared texture cell");
            }
            ++m_loads;
            const float depth = location[0] == m_holeX && location[1] == m_holeY ? m_holeDepth : m_depth;
            std::memcpy(output, &depth, sizeof(depth));
        }

        void Sample(SamplerState, const float*, void*, size_t) override
        {
            throw std::runtime_error("Production HZB helper unexpectedly sampled a filtered texture");
        }

        void SampleLevel(SamplerState, const float*, float, void*, size_t) override
        {
            throw std::runtime_error("Production HZB helper unexpectedly sampled a filtered mip");
        }

      private:
        std::uint32_t m_width{1}, m_height{1};
        float m_depth{1}, m_holeDepth{1};
        int m_holeX{-1}, m_holeY{-1};
        std::uint64_t m_loads{};
    };

    class ShadowPredictionCpuProbe final
    {
      public:
        ShadowPredictionCpuProbe()
        {
            m_parameters.predictionViews_0 = {m_views.data(), m_views.size()};
            m_parameters.predictionBounds_0 = {m_bounds.data(), m_bounds.size()};
            m_parameters.predictionResults_0 = {m_results.data(), m_results.size()};
            m_parameters.geometryOcclusion0_0 = {&m_textures[0]};
            m_parameters.geometryOcclusion1_0 = {&m_textures[1]};
            m_parameters.geometryOcclusion2_0 = {&m_textures[2]};
            m_parameters.geometryOcclusion3_0 = {&m_textures[3]};
            m_parameters.geometryOcclusion4_0 = {&m_textures[4]};
            m_parameters.geometryOcclusion5_0 = {&m_textures[5]};
            m_parameters.geometryOcclusion6_0 = {&m_textures[6]};
            m_parameters.geometryOcclusion7_0 = {&m_textures[7]};
            m_parameters.geometryOcclusion8_0 = {&m_textures[8]};
            m_parameters.geometryOcclusion9_0 = {&m_textures[9]};
            m_parameters.geometryOcclusion10_0 = {&m_textures[10]};
            m_parameters.geometryOcclusion11_0 = {&m_textures[11]};
            m_parameters.geometryOcclusion12_0 = {&m_textures[12]};
            m_parameters.geometryOcclusion13_0 = {&m_textures[13]};
            m_parameters.geometryOcclusion14_0 = {&m_textures[14]};
            m_parameters.geometryOcclusion15_0 = {&m_textures[15]};
#if SHADOW_PREDICTION_VARIANT == 3
            m_parameters.geometryVisibilityCandidates_0 = {m_candidates.data(), m_candidates.size()};
            for (std::size_t view = 0; view < m_views.size(); ++view)
            {
                m_candidates[view].bin_0 = static_cast<std::uint32_t>(view);
                m_parameters.meshletViewFirst_0[view] = {&m_firstCounts[view], 1};
                for (std::size_t level = 0; level < 16; ++level)
                {
                    const auto slot = view * 16 + level;
                    m_parameters.meshletViewHzb_0[slot] = {&m_textures[slot]};
                }
            }
#else
            m_parameters.PredictionProjection_0 = &m_cameraProjection;
            m_parameters.meshletFirstStage_0 = {m_firstCounts.data(), 1};
#endif
        }

        void Run()
        {
            CheckKnownPlanes();
            CheckUnknownInputs();
            CheckCameraCoverage();
            CheckAlternateProjectionAndRecovery();
            CheckEveryCoveredCell();
            CheckOddExtents();
#if SHADOW_PREDICTION_VARIANT == 3
            CheckViewIsolation();
#endif
            std::cout << "SHADOW_CAMERA_PREDICTION_CPU_OK variant=" << SHADOW_PREDICTION_VARIANT
                      << " checks=" << m_checks << " helperCalls=" << m_calls << " nativeGpu=false\n";
        }

      private:
        static constexpr bool kRecovery = SHADOW_PREDICTION_VARIANT == 2;

        void Check(bool value, std::string_view name)
        {
            ++m_checks;
            if (!value)
            {
                throw std::runtime_error(std::string(name));
            }
        }

        void SetView(std::uint32_t index, std::uint32_t width = 16, std::uint32_t height = 16, float depth = .2f)
        {
            auto& view = m_views[index];
            view = {};
            for (unsigned column = 0; column < 4; ++column)
            {
                view.projection_0[column][column] = 1;
                view.history_0[column][column] = 1;
            }
            view.width_0 = width;
            view.height_0 = height;
            view.valid_0 = 1;
            view.mode_0 = 4;
            view.bins_0 = 1;
            m_firstCounts[index] = 0;
            std::uint32_t levelWidth = width, levelHeight = height;
            do
            {
                levelWidth = (levelWidth + 1) / 2;
                levelHeight = (levelHeight + 1) / 2;
                ++view.levels_0;
            } while (levelWidth > 1 || levelHeight > 1);
            levelWidth = width;
            levelHeight = height;
            for (unsigned level = 0; level < 16; ++level)
            {
                levelWidth = (levelWidth + 1) / 2;
                levelHeight = (levelHeight + 1) / 2;
                m_textures[index * 16 + level].Reset(levelWidth, levelHeight, depth);
            }
            SetBounds(index, {-.05f, -.05f, .6f, 0}, {.05f, .05f, .6f, 0});
        }

        void SetBounds(std::uint32_t index, Vector<float, 4> minimum, Vector<float, 4> maximum)
        {
            m_bounds[index * 2] = minimum;
            m_bounds[index * 2 + 1] = maximum;
        }

        bool Execute(std::uint32_t count = 1)
        {
#if SHADOW_PREDICTION_VARIANT != 3
            m_cameraProjection.meshletHistoryColumns_0 = m_views[0].history_0;
#endif
            for (auto& result : m_results)
            {
                result = {UINT32_MAX, UINT32_MAX};
            }
            ComputeVaryingInput varying{};
            varying.endGroupID = {count, 1, 1};
            ShadowCameraPredictionCpu(&varying, nullptr, &m_parameters);
            m_calls += count;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                Check(m_results[index].x <= 1 && m_results[index].y <= 1, "Every fixture result was written");
            }
            return m_results[0].x != 0;
        }

        std::uint64_t Loads() const
        {
            std::uint64_t loads{};
            for (const auto& texture : m_textures)
            {
                loads += texture.Loads();
            }
            return loads;
        }

        void CheckKnownPlanes()
        {
            for (unsigned candidate = 1; candidate < 32; ++candidate)
            {
                for (unsigned depth = 1; depth < 32; ++depth)
                {
                    SetView(0, 17, 9, static_cast<float>(depth) / 32);
                    const float z = static_cast<float>(candidate) / 32;
                    SetBounds(0, {-.1f, -.2f, z, 0}, {.2f, .1f, z, 0});
                    Check(Execute() == (candidate > depth), "Independent standard-Z plane ordering");
                    Check(m_results[0].y == 1, "Interior plane has complete camera coverage");
                }
            }
        }

        void CheckUnknownInputs()
        {
            const auto nan = std::numeric_limits<float>::quiet_NaN();
            const auto infinity = std::numeric_limits<float>::infinity();
            for (float depth : {1.f, -.1f, 1.1f, nan, infinity})
            {
                SetView(0, 16, 16, depth);
                Check(!Execute(), "Clear, invalid and nonfinite HZB depths pass");
            }
            for (float coordinate : {nan, infinity, -infinity})
            {
                SetView(0);
                m_bounds[0].x = coordinate;
                Check(!Execute() && m_results[0].y == 0, "Nonfinite geometry remains unknown");
                SetView(0);
                m_views[0].projection_0[0].x = coordinate;
                m_views[0].history_0[0].x = coordinate;
                Check(!Execute() && m_results[0].y == 0, "Nonfinite projection remains unknown");
            }
            SetView(0);
            m_bounds[0].x = 1;
            Check(!Execute() && m_results[0].y == 0, "Reversed bounds remain unknown");
            SetView(0);
            m_bounds[0].z = -.01f;
            Check(!Execute() && m_results[0].y == 0, "Near-plane crossing remains unknown");
            SetView(0);
            m_views[0].projection_0[3].w = m_views[0].history_0[3].w = 0;
            Check(!Execute() && m_results[0].y == 0, "Zero homogeneous W remains unknown");
            SetView(0);
            m_views[0].projection_0[3].w = m_views[0].history_0[3].w = -1;
            Check(!Execute() && m_results[0].y == 0, "Negative homogeneous W remains unknown");
            SetView(0);
            m_views[0].width_0 = 0;
            Check(!Execute(), "Zero depth width cannot reject geometry");
            SetView(0);
            m_views[0].levels_0 = 0;
            Check(!Execute(), "Missing pyramid cannot reject geometry");
            SetView(0);
            m_views[0].valid_0 = 0;
            Check(!Execute(), "Invalid projection metadata cannot reject geometry");
        }

        void CheckCameraCoverage()
        {
            SetView(0);
            SetBounds(0, {.8f, -.1f, .6f, 0}, {1.2f, .1f, .6f, 0});
            Check(Execute() == kRecovery && m_results[0].y == 0,
                  "Prediction keeps partial camera coverage; light recovery retains its original clipping contract");
            SetView(0);
            SetBounds(0, {1.2f, -.1f, .6f, 0}, {1.4f, .1f, .6f, 0});
            Check(!Execute() && m_results[0].y == 0, "Completely off-camera geometry remains unknown");
            SetView(0);
            SetBounds(0, {-.1f, -.1f, .99f, 0}, {.1f, .1f, 1.01f, 0});
            Check(Execute() == kRecovery && m_results[0].y == 0, "Prediction requires the whole camera far range");
            SetView(0);
            SetBounds(0, {-.1f, -.1f, .6f, 0}, {1.f, .1f, .6f, 0});
            Check(Execute() == kRecovery && m_results[0].y == 0, "Rounded viewport boundary remains unknown");
        }

        void CheckAlternateProjectionAndRecovery()
        {
            SetView(0, 16, 16, .5f);
            m_views[0].projection_0[2] = {0, 0, -1, 1};
            Check(Execute() != kRecovery,
                  "Prediction samples camera projection while recovery samples the independent light projection");
            SetView(0);
            m_firstCounts[0] = 1;
#if SHADOW_PREDICTION_VARIANT == 1
            Check(Execute(), "Independent first prediction does not consume a recovery mask");
#else
            m_views[0].mode_0 = 3;
            Check(!Execute() && Loads() == 0, "First-stage survivors bypass recovery depth and remain in the union");
            m_firstCounts[0] = 0;
            Check(Execute() && Loads() != 0, "First-stage rejects are tested against actual light-space depth");
#endif
#if SHADOW_PREDICTION_VARIANT == 3
            SetView(0, 32, 32, .2f);
            m_views[0].projection_0[2] = {0, 0, -1, 1};
            const bool firstRejected = Execute();
            Check(firstRejected, "Camera foreground can predict away a light-visible caster");
            SetView(0, 64, 64, .8f);
            m_views[0].projection_0[2] = {0, 0, -1, 1};
            m_views[0].mode_0 = 3;
            m_firstCounts[0] = firstRejected ? 0 : 1;
            Check(!Execute(), "Actual reverse-light depth recovers the caster at an independent extent");
            SetView(0);
            m_views[0].mode_0 = 0;
            Check(!Execute() && Loads() == 0, "Disabled shared-view occlusion performs no texture reads");
#endif
        }

        void CheckEveryCoveredCell()
        {
            // 작은 정사각형의 픽셀 범위 [6,10]은 level 1의 네 셀 [1,2]×[1,2]을 덮는다.
            for (int y = 1; y <= 2; ++y)
            {
                for (int x = 1; x <= 2; ++x)
                {
                    SetView(0);
                    m_textures[1].SetHole(x, y, 1);
                    Check(!Execute(), "Every covered HZB cell can preserve a depth hole");
                }
            }
            SetView(0);
            Check(Execute() && m_textures[1].Loads() == 4, "The complete four-cell footprint is read");
        }

        void CheckOddExtents()
        {
            std::uint32_t random = 0x473be217;
            for (unsigned iteration = 0; iteration < 4096; ++iteration)
            {
                random = random * 1664525u + 1013904223u;
                const auto width = 1u + random % 65u;
                const auto height = 1u + (random >> 8) % 65u;
                const float x = (float(random & 255u) / 255.f - .5f) * 1.2f;
                const float y = (float((random >> 8) & 255u) / 255.f - .5f) * 1.2f;
                const float z = .1f + float((random >> 16) & 255u) / 255.f * .8f;
                const bool hidden = (iteration & 1u) != 0;
                SetView(0, width, height, z + (hidden ? -.05f : .05f));
                SetBounds(0, {x - .02f, y - .02f, z, 0}, {x + .02f, y + .02f, z, 0});
                Check(Execute() == hidden && m_results[0].y == 1,
                      "Independent plane oracle across nonempty odd/even depth extents");
            }
        }

#if SHADOW_PREDICTION_VARIANT == 3
        void CheckViewIsolation()
        {
            for (unsigned view = 0; view < 8; ++view)
            {
                SetView(view, 17 + view * 2, 9 + view * 3, view % 2 ? .9f : .2f);
                m_candidates[view].bin_0 = 7 - view;
                m_views[view].history_0[0].w = float(view) * .03f;
            }
            Execute(8);
            for (unsigned bin = 0; bin < 8; ++bin)
            {
                const auto view = 7 - bin;
                Check(bool(m_results[bin].x) == (view % 2 == 0),
                      "Shared producer IDs select their own camera projection, dimensions and depth bindings");
            }
        }
#endif

        GlobalParams_0 m_parameters{};
        std::array<MeshletViewData_0, 8> m_views{};
        std::array<Vector<float, 4>, 16> m_bounds{};
        std::array<Vector<std::uint32_t, 2>, 8> m_results{};
        std::array<std::uint32_t, 8> m_firstCounts{};
        std::array<CpuDepthTexture, 128> m_textures;
#if SHADOW_PREDICTION_VARIANT == 3
        std::array<GeometryVisibilityCandidate_0, 8> m_candidates{};
#else
        SLANG_ParameterGroup_PredictionProjection_0 m_cameraProjection{};
#endif
        std::uint64_t m_checks{}, m_calls{};
    };
} // namespace

int main()
{
    try
    {
        ShadowPredictionCpuProbe probe;
        probe.Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SHADOW_CAMERA_PREDICTION_CPU_FAILED variant=" << SHADOW_PREDICTION_VARIANT
                  << " reason=" << error.what() << '\n';
        return 1;
    }
}
