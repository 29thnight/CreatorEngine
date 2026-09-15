#include "BrowserDirectorySnapshot.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace
{
    using namespace editor;
    using clock_type = std::chrono::steady_clock;

    /// 스캔한 지 이만큼 지나면 낡은 것으로 본다. 밖에서 파일이 바뀌는 것을 이
    /// 경로가 알 방법이 지금은 없어서 나이로 대신한다.
    constexpr int kRevalidateMs = 1000;

    /// 프레임당 다시 훑을 폴더 수. 낡은 것을 한꺼번에 훑으면 초당 몇 번씩
    /// 24 회 스캔이 몰려 p95 가 오히려 나빠진다.
    constexpr int kRescanBudget = 1;

    struct cache_slot
    {
        browser_directory_listing listing{};
        clock_type::time_point scannedAt{};
    };

    struct cache_state
    {
        std::unordered_map<std::string, cache_slot> slots;
        browser_cache_stats stats{};
        int frameBudget{ kRescanBudget };
        bool forceThisFrame{};
    };

    cache_state& state()
    {
        static cache_state instance;
        return instance;
    }

    /// W7-5: 어휘 비교가 성립하려면 양쪽이 같은 모양이어야 한다. 두 가지를 접는다.
    ///
    ///   ① `lexically_normal` — `.`·`..`·중복 구분자.
    ///   ② **후행 구분자** — `PathFinder::RelativeToPrefab("")` 은 `path / ""` 라
    ///      끝에 구분자를 남기는데, 표준의 정규화 절차는 마지막 요소가 `..` 일
    ///      때만 그것을 지운다. 그래서 `".../Prefabs/"` 와 `".../Prefabs"` 가
    ///      정규화 뒤에도 서로 다르다. `equivalent` 는 이 차이를 덮고 있었으므로
    ///      접지 않고 바꾸면 **드롭 대상이 조용히 죽는다.**
    ///
    /// 대소문자는 접지 않는다 — 비교하는 두 값이 모두 `browser_canonical` 을 거쳐
    /// 디스크의 철자로 풀린 뒤에 들어오기 때문이다. 한쪽이라도 날것이면 그쪽을
    /// 고쳐야지 여기서 무마할 일이 아니다.
    browser_fs::path strip_trailing_separator(const browser_fs::path& path)
    {
        browser_fs::path normalized = path.lexically_normal();
        if (!normalized.empty() && !normalized.has_filename())
        {
            browser_fs::path parent = normalized.parent_path();
            if (!parent.empty()) normalized = parent;
        }
        return normalized;
    }

    /// 빈 목록. 경로를 못 읽었을 때 돌려줄 것이 필요하다 — 참조를 주는
    /// 계약이라 매번 새로 만들 수 없다.
    const browser_directory_listing& empty_listing()
    {
        static const browser_directory_listing listing{};
        return listing;
    }

    std::string to_utf8(const browser_fs::path& path)
    {
        const auto utf8 = path.u8string();
        return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
    }

    void scan_into(const browser_fs::path& directory, cache_slot& slot)
    {
        browser_directory_listing listing;
        std::error_code ec;
        for (browser_fs::directory_iterator it(directory,
                 browser_fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            browser_directory_entry entry;
            entry.path = it->path();
            entry.nameUtf8 = to_utf8(it->path().filename());
            entry.pathUtf8 = to_utf8(it->path());
            entry.extension = it->path().extension().string();
            // ★ 여기서 한 번만 묻는다. 예전에는 정렬 비교자가 비교마다
            //   `is_directory(ec)` 를 불렀다 — 비교 횟수만큼 stat 이다.
            std::error_code kindError;
            entry.isDirectory = it->is_directory(kindError) && !kindError;
            std::error_code linkError;
            entry.isSymlink = it->is_symlink(linkError) && !linkError;
            listing.entries.push_back(std::move(entry));
        }
        listing.error = ec;
        listing.valid = !ec;
        // 정렬은 여기서 한 번 해 둔다 — 폴더 먼저, 그 다음 이름 오름차순.
        // 창이 내림차순을 원하면 뒤집기만 하면 되고, 그것은 비교가 아니라
        // 순회 방향이라 stat 을 다시 부르지 않는다.
        std::sort(listing.entries.begin(), listing.entries.end(),
            [](const browser_directory_entry& a, const browser_directory_entry& b)
            {
                if (a.isDirectory != b.isDirectory) return a.isDirectory;
                return a.path.filename() < b.path.filename();
            });
        slot.listing = std::move(listing);
        slot.scannedAt = clock_type::now();
        ++state().stats.scans;
    }
}

namespace editor
{
    void browser_cache_begin_frame()
    {
        cache_state& cache = state();
        cache.frameBudget = kRescanBudget;
        if (cache.forceThisFrame)
        {
            // 무효화 다음 프레임은 예산을 풀어 준다. 사람이 방금 만든 폴더가
            // 1 초 뒤에 나타나면 그것은 버그로 보인다.
            cache.frameBudget = static_cast<int>(cache.slots.size()) + 1;
            cache.forceThisFrame = false;
        }
    }

    const browser_directory_listing& browser_cache_listing(const browser_fs::path& directory)
    {
        if (directory.empty()) return empty_listing();
        cache_state& cache = state();
        const std::string key = to_utf8(directory);
        auto found = cache.slots.find(key);
        if (found == cache.slots.end())
        {
            // 처음 보는 폴더는 예산과 무관하게 훑는다 — 줄 것이 없다.
            cache_slot slot;
            scan_into(directory, slot);
            found = cache.slots.emplace(key, std::move(slot)).first;
            cache.stats.cached = cache.slots.size();
            return found->second.listing;
        }

        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock_type::now() - found->second.scannedAt).count();
        if (age >= kRevalidateMs && cache.frameBudget > 0)
        {
            --cache.frameBudget;
            scan_into(directory, found->second);
            return found->second.listing;
        }
        ++cache.stats.hits;
        return found->second.listing;
    }

    void browser_cache_invalidate()
    {
        cache_state& cache = state();
        cache.stats.evictions += cache.slots.size();
        cache.slots.clear();
        cache.stats.cached = 0;
        cache.forceThisFrame = true;
    }

    browser_cache_stats browser_cache_get_stats()
    {
        cache_state& cache = state();
        cache.stats.cached = cache.slots.size();
        return cache.stats;
    }

    bool browser_same_directory(const browser_fs::path& a, const browser_fs::path& b)
    {
        if (a.empty() || b.empty()) return false;
        return strip_trailing_separator(a) == strip_trailing_separator(b);
    }

    browser_fs::path browser_canonical(const browser_fs::path& path, std::error_code& ec)
    {
        // ★ 계수는 결과가 아니라 **호출**에 건다. 실패해도 디스크는 이미 만졌다 —
        //   성공한 것만 세면 없는 경로를 묻는 자리가 계수기 밖으로 빠져나간다.
        ++state().stats.probes;
        return browser_fs::weakly_canonical(path, ec);
    }

    bool browser_directory_exists(const browser_fs::path& path)
    {
        std::error_code ec;
        ++state().stats.probes;
        return browser_fs::is_directory(path, ec) && !ec;
    }
}
