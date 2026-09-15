#include "BrowserThumbnailCache.h"

#include "EditorImGuiTexture.h"
#include "Texture.h"
#include "TextureImage.h"
#include "WorkerPool.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace editor
{
namespace
{

    /// 읽지 않을 만큼 큰 파일. 썸네일 하나 때문에 기가바이트를 들 이유가 없다.
    constexpr std::uint64_t kThumbnailMaxSourceBytes = 64ull * 1024ull * 1024ull;

    /// CPU 픽셀 예산. 넘으면 오래 안 쓴 것부터 버린다.
    constexpr std::uint64_t kThumbnailBudgetBytes = 48ull * 1024ull * 1024ull;

    /// 한 프레임에 새로 넘길 작업 수. 폴더를 처음 열면 타일 수십 개가 한꺼번에
    /// 요청되는데, 그것을 그대로 풀에 쏟으면 디스크가 한 번에 맞는다. 요청은
    /// 즉시 등록하되 **넘기는 것**만 조인다 — 등록이 늦으면 중복 억제가 깨진다.
    constexpr std::size_t kThumbnailDispatchPerFrame = 4;

    struct thumbnail_key_hasher
    {
        std::size_t operator()(const thumbnail_key& key) const noexcept
        {
            std::uint64_t h = key.path;
            h ^= key.revision + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= std::uint64_t(key.subasset) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= std::uint64_t(key.resolution) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= std::uint64_t(key.generator) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return static_cast<std::size_t>(h);
        }
    };

    struct thumbnail_entry
    {
        thumbnail_state state{ thumbnail_state::queued };
        /// ★ 늦은 완료를 가르는 표식. 같은 키를 다시 요청하면(무효화 뒤) 이 수가
        ///   오르고, 옛 작업이 들고 있던 수와 달라져 그 결과가 폐기된다.
        std::uint64_t generation{ 0 };
        std::uint64_t lastUsedFrame{ 0 };
        std::uint64_t bytes{ 0 };
        std::shared_ptr<Texture> texture;
        thumbnail_fs::path source;
        bool dispatched{ false };
        bool visible{ false };
    };

    /// 작업 스레드가 돌려보내는 것. **Texture 가 아니라 픽셀이다.**
    struct thumbnail_completion
    {
        thumbnail_key key{};
        std::uint64_t generation{ 0 };
        std::uint32_t width{ 0 };
        std::uint32_t height{ 0 };
        std::vector<std::byte> rgba;  ///< 비어 있으면 실패
    };

    struct thumbnail_cache_state
    {
        std::unordered_map<thumbnail_key, thumbnail_entry, thumbnail_key_hasher> entries;

        // 작업 스레드 ↔ Presentation 스레드
        std::mutex completionMutex;
        std::vector<thumbnail_completion> completions;

        // Presentation 스레드 ↔ 게임 스레드(CLI)
        std::mutex statsMutex;
        thumbnail_stats published{};

        // Presentation 스레드 전용 — 잠금 없이 쌓고 프레임 머리에서 게시한다.
        thumbnail_stats live{};

        std::atomic<bool> resetRequested{ false };
        std::atomic<bool> shuttingDown{ false };

        std::uint64_t frame{ 0 };
        std::uint64_t nextGeneration{ 1 };
        std::uint64_t bytes{ 0 };

        /// CPU 픽셀 예산. 기본값은 위의 상수고, 게이트가 낮춰 부를 수 있다.
        ///
        /// ★ 조종 창구가 없으면 **축출 축을 자극할 수 없다.** 목록은 clipper 로
        ///   보이는 타일만 요청하므로 파일을 수백 개 뿌려도 48 MB 에 닿지 않는다.
        ///   "예산을 넘으면 버린다" 는 판정문을 적어 두고 그 수를 만들 방법이
        ///   없으면 그 절은 영원히 미자극이다.
        ///
        /// 쓰는 쪽은 게임 스레드(CLI)고 읽는 쪽은 Presentation 스레드다.
        std::atomic<std::uint64_t> budgetBytes{ kThumbnailBudgetBytes };
    };

    thumbnail_cache_state& thumbnail_cache()
    {
        // ★ 함수 지역 정적이다. 작업 스레드가 완료를 밀어 넣는 자리라 이 객체가
        //   작업자보다 먼저 사라지면 안 된다.
        static thumbnail_cache_state instance;
        return instance;
    }

    std::uint64_t thumbnail_hash_path(const thumbnail_fs::path& path)
    {
        // 경로 문자열 그대로 센다 — 대소문자를 접지 않는다. 같은 파일을 두
        // 철자로 가리키면 그림을 두 번 만들 뿐, 틀린 그림이 나오지는 않는다.
        const std::wstring text = path.wstring();
        std::uint64_t h = 1469598103934665603ull;
        for (const wchar_t c : text)
        {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ull;
        }
        return h;
    }

    /// 상자 평균으로 줄인다. 가로세로 비를 지키고 긴 변을 `target` 에 맞춘다.
    ///
    /// ★ 가장 가까운 점을 집는(nearest) 축소는 한 픽셀씩 건너뛰므로 잔무늬가
    ///   심하게 남는다. 썸네일은 대부분 큰 축소율이라 그 차이가 그대로 보인다.
    void thumbnail_downscale_rgba(const std::byte* source, std::uint32_t sourceWidth,
                        std::uint32_t sourceHeight, std::size_t sourceRowPitch,
                        std::uint32_t target, std::vector<std::byte>& outPixels,
                        std::uint32_t& outWidth, std::uint32_t& outHeight)
    {
        const std::uint32_t longest = std::max(sourceWidth, sourceHeight);
        if (0 == longest) { outWidth = outHeight = 0; outPixels.clear(); return; }

        if (longest <= target)
        {
            // 이미 작다 — 그대로 복사한다(행 간격만 촘촘히 맞춘다).
            outWidth = sourceWidth;
            outHeight = sourceHeight;
            outPixels.resize(std::size_t(sourceWidth) * sourceHeight * 4u);
            for (std::uint32_t y = 0; y < sourceHeight; ++y)
            {
                std::memcpy(outPixels.data() + std::size_t(y) * sourceWidth * 4u,
                            source + std::size_t(y) * sourceRowPitch,
                            std::size_t(sourceWidth) * 4u);
            }
            return;
        }

        const double scale = double(target) / double(longest);
        outWidth = std::max(1u, static_cast<std::uint32_t>(sourceWidth * scale));
        outHeight = std::max(1u, static_cast<std::uint32_t>(sourceHeight * scale));
        outPixels.assign(std::size_t(outWidth) * outHeight * 4u, std::byte{});

        for (std::uint32_t y = 0; y < outHeight; ++y)
        {
            const std::uint32_t y0 = std::uint32_t(std::uint64_t(y) * sourceHeight / outHeight);
            const std::uint32_t y1 = std::max(y0 + 1u,
                std::uint32_t(std::uint64_t(y + 1) * sourceHeight / outHeight));
            for (std::uint32_t x = 0; x < outWidth; ++x)
            {
                const std::uint32_t x0 = std::uint32_t(std::uint64_t(x) * sourceWidth / outWidth);
                const std::uint32_t x1 = std::max(x0 + 1u,
                    std::uint32_t(std::uint64_t(x + 1) * sourceWidth / outWidth));

                std::uint32_t sum[4]{};
                std::uint32_t count = 0;
                for (std::uint32_t sy = y0; sy < y1 && sy < sourceHeight; ++sy)
                {
                    const std::byte* row = source + std::size_t(sy) * sourceRowPitch;
                    for (std::uint32_t sx = x0; sx < x1 && sx < sourceWidth; ++sx)
                    {
                        const std::byte* texel = row + std::size_t(sx) * 4u;
                        for (int c = 0; c < 4; ++c)
                            sum[c] += static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(texel[c]));
                        ++count;
                    }
                }
                if (0 == count) continue;
                std::byte* destination = outPixels.data()
                    + (std::size_t(y) * outWidth + x) * 4u;
                for (int c = 0; c < 4; ++c)
                    destination[c] = static_cast<std::byte>(sum[c] / count);
            }
        }
    }

    /// 작업 스레드 몫. 파일을 읽고, 디코드하고, 줄인다. **여기까지다.**
    thumbnail_completion thumbnail_run_generator(const thumbnail_key& key,
                                     const thumbnail_fs::path& source,
                                     std::uint64_t generation)
    {
        thumbnail_completion result;
        result.key = key;
        result.generation = generation;

        std::error_code sizeError;
        const auto size = thumbnail_fs::file_size(source, sizeError);
        if (sizeError || 0 == size || size > kThumbnailMaxSourceBytes) return result;

        std::ifstream stream(source, std::ios::binary);
        if (!stream) return result;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) return result;

        TextureImage image;
        std::string failure;
        if (!Texture::DecodeToRgba8(bytes, image, failure) || !image.IsValid()) return result;

        const TextureSubimage* const top = image.Find(0, 0);
        if (nullptr == top || nullptr == top->pixels) return result;

        thumbnail_downscale_rgba(top->pixels, image.Width(), image.Height(), top->rowPitch,
                       key.resolution, result.rgba, result.width, result.height);
        if (result.rgba.empty()) { result.width = result.height = 0; }
        return result;
    }

    void thumbnail_dispatch(const thumbnail_key& key, thumbnail_entry& item)
    {
        item.dispatched = true;
        item.state = thumbnail_state::working;
        const thumbnail_fs::path source = item.source;
        const std::uint64_t generation = item.generation;
        WorkerPools->Enqueue([key, source, generation]()
        {
            thumbnail_completion done = thumbnail_run_generator(key, source, generation);
            thumbnail_cache_state& self = thumbnail_cache();
            std::lock_guard<std::mutex> guard(self.completionMutex);
            self.completions.push_back(std::move(done));
        });
    }
}

    const char* thumbnail_state_name(thumbnail_state state) noexcept
    {
        switch (state)
        {
        case thumbnail_state::queued:          return "queued";
        case thumbnail_state::working:         return "working";
        case thumbnail_state::awaiting_upload: return "awaiting_upload";
        case thumbnail_state::ready:           return "ready";
        case thumbnail_state::failed:          return "failed";
        default:                               return "unknown";
        }
    }

    thumbnail_key thumbnail_make_key(const thumbnail_fs::path& source,
                                     std::uint64_t revision,
                                     std::uint16_t resolution) noexcept
    {
        thumbnail_key key;
        key.path = thumbnail_hash_path(source);
        key.revision = revision;
        key.subasset = 0;
        key.resolution = resolution;
        key.generator = thumbnail_pick_generator(source.extension().string());
        return key;
    }

    thumbnail_generator thumbnail_pick_generator(std::string_view extension) noexcept
    {
        std::string lowered;
        lowered.reserve(extension.size());
        for (const char c : extension)
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        // ★ 여기 없는 확장자는 유형 아이콘에 머문다. 계약의 "미지원 생성기는
        //   아이콘을 유지한다" 이고, 줄을 더하는 것이 생성기를 넓히는 방법이다.
        //   모델(.fbx·.gltf·.obj·.glb)은 형상을 렌더해야 해서 여기 없다.
        if (".png" == lowered || ".jpg" == lowered || ".jpeg" == lowered
            || ".dds" == lowered)
            return thumbnail_generator::texture;
        return thumbnail_generator::none;
    }

    void thumbnail_begin_frame()
    {
        thumbnail_cache_state& self = thumbnail_cache();
        ++self.frame;

        if (self.resetRequested.exchange(false))
        {
            self.live = thumbnail_stats{};
        }

        // ── ① 완료를 반영한다 ─────────────────────────────────────────────
        std::vector<thumbnail_completion> arrived;
        {
            std::lock_guard<std::mutex> guard(self.completionMutex);
            arrived.swap(self.completions);
        }

        for (thumbnail_completion& done : arrived)
        {
            const auto found = self.entries.find(done.key);
            if (found == self.entries.end() || found->second.generation != done.generation
                || self.shuttingDown.load())
            {
                // ★ 계약의 "오래된 요청의 늦은 완료는 폐기한다". 이 수를 세지
                //   않으면 폐기가 실제로 일어나는지 밖에서 알 수 없다.
                ++self.live.lateDropped;
                continue;
            }

            thumbnail_entry& item = found->second;
            if (done.rgba.empty() || 0 == done.width || 0 == done.height)
            {
                item.state = thumbnail_state::failed;
                ++self.live.failed;
                continue;
            }

            ++self.live.decoded;

            // Texture 는 **여기서** 선다 — Presentation 스레드다.
            std::shared_ptr<Texture> texture(Texture::CreateFromPixels(
                done.width, done.height, "BrowserThumbnail",
                RHIFormat::RGBA8Unorm, done.rgba.data(),
                std::size_t(done.width) * 4u));
            if (!texture)
            {
                item.state = thumbnail_state::failed;
                ++self.live.failed;
                continue;
            }

            item.texture = std::move(texture);
            item.bytes = std::uint64_t(done.rgba.size());
            self.bytes += item.bytes;
            item.state = thumbnail_state::awaiting_upload;
        }

        // ── ② 올라간 것을 Ready 로 승격한다 ──────────────────────────────
        //
        // ★ 프레임 **머리**에서만 바꾼다. 프레임 안에서 승격하면 같은 프레임의
        //   앞 타일과 뒤 타일이 서로 다른 그림을 받는다.
        for (auto& [key, item] : self.entries)
        {
            if (thumbnail_state::awaiting_upload != item.state) continue;
            if (EditorImGuiTexture::IsReady(item.texture.get()))
            {
                item.state = thumbnail_state::ready;
                ++self.live.published;
                continue;
            }
            // 아직이다 — 그리지 않고 등록만 해서 업로드를 **일으킨다.**
            // 이것이 없으면 아무도 등록하지 않아 영원히 안 올라간다.
            EditorImGuiTexture::Prime(item.texture.get());
        }

        // ── ③ 대기 중인 요청을 조금씩 넘긴다 ────────────────────────────
        if (WorkerPools->IsRunning() && !self.shuttingDown.load())
        {
            std::size_t dispatched = 0;
            // 보이는 것을 먼저 넘긴다(계약의 "가시 타일 우선 요청").
            for (int pass = 0; pass < 2 && dispatched < kThumbnailDispatchPerFrame; ++pass)
            {
                const bool wantVisible = (0 == pass);
                for (auto& [key, item] : self.entries)
                {
                    if (dispatched >= kThumbnailDispatchPerFrame) break;
                    if (item.dispatched || thumbnail_state::queued != item.state) continue;
                    if (item.visible != wantVisible) continue;
                    thumbnail_dispatch(key, item);
                    ++dispatched;
                }
            }
        }

        // ── ④ 예산을 넘으면 오래 안 쓴 것부터 버린다 ────────────────────
        const std::uint64_t budget = self.budgetBytes.load();
        if (self.bytes > budget)
        {
            // ★ 나이를 키와 **함께** 담는다. 비교자 안에서 표를 다시 찾으면
            //   (`entries[key]`) 정렬 도중에 표를 건드리는 꼴이 되고, 그것은
            //   없는 키 하나에 항목을 만들어 순회를 무너뜨릴 수 있는 모양이다.
            std::vector<std::pair<std::uint64_t, thumbnail_key>> order;
            order.reserve(self.entries.size());
            for (const auto& [key, item] : self.entries)
            {
                // 진행 중인 것은 버리지 않는다 — 버려도 작업은 계속 돌고,
                // 그 결과는 어차피 늦은 완료로 폐기된다(헛일 두 번).
                if (thumbnail_state::ready == item.state && item.bytes > 0)
                    order.emplace_back(item.lastUsedFrame, key);
            }
            std::sort(order.begin(), order.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [age, key] : order)
            {
                if (self.bytes <= budget) break;
                const auto found = self.entries.find(key);
                if (found == self.entries.end()) continue;
                self.bytes -= found->second.bytes;
                self.entries.erase(found);
                ++self.live.evicted;
            }
        }

        // ── ⑤ 지금 상태를 세어 게시한다 ─────────────────────────────────
        thumbnail_stats snapshot = self.live;
        snapshot.entries = self.entries.size();
        snapshot.bytes = self.bytes;
        snapshot.budgetBytes = budget;
        snapshot.workerPoolRunning = WorkerPools->IsRunning();
        for (const auto& [key, item] : self.entries)
        {
            switch (item.state)
            {
            case thumbnail_state::queued:          ++snapshot.queued; break;
            case thumbnail_state::working:         ++snapshot.working; break;
            case thumbnail_state::awaiting_upload: ++snapshot.awaitingUpload; break;
            case thumbnail_state::ready:           ++snapshot.ready; break;
            default: break;
            }
        }
        {
            std::lock_guard<std::mutex> guard(self.statsMutex);
            self.published = snapshot;
        }

        // 다음 프레임의 가시 표식은 그 프레임의 조회가 다시 세운다.
        for (auto& [key, item] : self.entries) item.visible = false;
    }

    Texture* thumbnail_acquire(const thumbnail_key& key,
                               const thumbnail_fs::path& source,
                               bool visible)
    {
        thumbnail_cache_state& self = thumbnail_cache();
        if (thumbnail_generator::none == key.generator)
        {
            ++self.live.servedIcons;
            return nullptr;
        }

        const auto found = self.entries.find(key);
        if (found == self.entries.end())
        {
            // ★ 무효화를 UI 사건에 걸지 않는다. 같은 경로인데 revision 이 다른
            //   항목이 있다면 그 파일은 바뀐 것이다 — 목록 스캔이 낸 값이 그렇게
            //   말하고 있다. 사건을 잡는 쪽(삭제 메뉴·감시자)에 걸면 놓치는
            //   경로가 생기지만, 이쪽은 **새 키가 생기는 모든 경우**를 덮는다.
            //   진행 중인 옛 작업의 결과는 항목이 사라져 늦은 완료로 폐기된다.
            for (auto it = self.entries.begin(); it != self.entries.end();)
            {
                if (it->first.path != key.path) { ++it; continue; }
                self.bytes -= it->second.bytes;
                it = self.entries.erase(it);
                ++self.live.invalidated;
            }

            thumbnail_entry item;
            item.generation = self.nextGeneration++;
            item.lastUsedFrame = self.frame;
            item.source = source;
            item.visible = visible;
            self.entries.emplace(key, std::move(item));
            ++self.live.requests;
            ++self.live.servedIcons;
            return nullptr;
        }

        thumbnail_entry& item = found->second;
        item.lastUsedFrame = self.frame;
        item.visible = item.visible || visible;
        ++self.live.deduped;

        if (thumbnail_state::ready == item.state && item.texture)
        {
            ++self.live.servedThumbnails;
            return item.texture.get();
        }
        ++self.live.servedIcons;
        return nullptr;
    }

    void thumbnail_invalidate(const thumbnail_fs::path& source)
    {
        thumbnail_cache_state& self = thumbnail_cache();
        const std::uint64_t pathHash = thumbnail_hash_path(source);
        for (auto it = self.entries.begin(); it != self.entries.end();)
        {
            if (it->first.path != pathHash) { ++it; continue; }
            self.bytes -= it->second.bytes;
            it = self.entries.erase(it);
            ++self.live.invalidated;
        }
    }

    void thumbnail_invalidate_all()
    {
        thumbnail_cache_state& self = thumbnail_cache();
        self.live.invalidated += self.entries.size();
        self.entries.clear();
        self.bytes = 0;
    }

    void thumbnail_shutdown()
    {
        thumbnail_cache_state& self = thumbnail_cache();
        self.shuttingDown.store(true);
        self.entries.clear();
        self.bytes = 0;
        std::lock_guard<std::mutex> guard(self.completionMutex);
        self.completions.clear();
    }

    thumbnail_stats thumbnail_read_stats()
    {
        thumbnail_cache_state& self = thumbnail_cache();
        std::lock_guard<std::mutex> guard(self.statsMutex);
        return self.published;
    }

    void thumbnail_reset_stats()
    {
        thumbnail_cache_state& self = thumbnail_cache();
        self.resetRequested.store(true);
        std::lock_guard<std::mutex> guard(self.statsMutex);
        self.published = thumbnail_stats{};
    }

    std::uint64_t thumbnail_default_budget_bytes()
    {
        return kThumbnailBudgetBytes;
    }

    void thumbnail_set_budget_bytes(std::uint64_t bytes)
    {
        // 0 은 "기본값으로 되돌려라" 다. 예산 0 은 뜻이 없다 — 만드는 즉시 버리므로
        // 타일이 영원히 아이콘에 머물고, 그것은 판정이 아니라 고장이다.
        thumbnail_cache_state& self = thumbnail_cache();
        self.budgetBytes.store(0 == bytes ? kThumbnailBudgetBytes : bytes);
    }
}
