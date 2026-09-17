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

    /// 다시 훑는 데 걸린 시간의 이만큼 배를 다음 확인까지 쉰다(최소 `kRevalidateMs`).
    /// 폴더 하나에 파일 5 만 개면 훑기만 40ms 라, 1 초마다 훑으면 그 폴더에 선
    /// 것만으로 초마다 멈칫한다. 확인에 쓰는 시간을 1% 아래로 묶는다.
    constexpr int kRevalidateCostFactor = 100;

    /// 프레임당 다시 훑을 폴더 수. 낡은 것을 한꺼번에 훑으면 초당 몇 번씩
    /// 24 회 스캔이 몰려 p95 가 오히려 나빠진다.
    constexpr int kRescanBudget = 1;

    struct cache_slot
    {
        browser_directory_listing listing{};
        clock_type::time_point scannedAt{};
        /// 다음 확인까지 쉴 시간. 훑는 비용에 따라 늘어난다.
        std::chrono::milliseconds revalidateAfter{ kRevalidateMs };
        /// 목록을 만든 순회의 지문. 같으면 항목을 새로 만들지 않는다.
        std::uint64_t fingerprint{};
    };

    struct cache_state
    {
        std::unordered_map<std::string, cache_slot> slots;
        browser_cache_stats stats{};
        int frameBudget{ kRescanBudget };
        std::uint64_t generation{ 1 };
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

    /// 항목 하나가 목록에 남기는 것(경로·종류·revision)을 지문에 접는다. 목록을
    /// 만드는 순회와 확인만 하는 순회가 **같은 함수**를 타야 둘이 어긋나지 않는다.
    std::uint64_t fold_entry(std::uint64_t hash, const browser_fs::directory_entry& it,
        bool& isDirectory, bool& isSymlink, std::uint64_t& revision)
    {
        std::error_code kindError;
        isDirectory = it.is_directory(kindError) && !kindError;
        std::error_code linkError;
        isSymlink = it.is_symlink(linkError) && !linkError;
        revision = 0;
        // W7 썸네일의 revision. 폴더는 묻지 않는다 — 크기를 물으면 오류다.
        // 두 값 다 찾기 결과에 담겨 오므로 여기서 새 접촉이 생기지 않는다.
        if (!isDirectory)
        {
            std::error_code timeError;
            const auto written = it.last_write_time(timeError);
            std::error_code sizeError;
            const auto bytes = it.file_size(sizeError);
            if (!timeError) revision = std::uint64_t(written.time_since_epoch().count());
            if (!sizeError) revision ^= std::uint64_t(bytes);
        }
        constexpr std::uint64_t prime = 1099511628211ull;
        for (const auto unit : it.path().native()) { hash ^= std::uint64_t(unit); hash *= prime; }
        hash ^= revision; hash *= prime;
        hash ^= (isDirectory ? 2u : 0u) | (isSymlink ? 1u : 0u); hash *= prime;
        return hash;
    }

    constexpr std::uint64_t kFingerprintSeed = 1469598103934665603ull;

    /// 항목을 만들지 않고 지문만 낸다. 문자열 할당이 없어 목록 만들기의 절반 아래다.
    std::uint64_t fingerprint_directory(const browser_fs::path& directory, size_t& count, std::error_code& ec)
    {
        std::uint64_t hash = kFingerprintSeed;
        count = 0;
        bool isDirectory{}, isSymlink{};
        std::uint64_t revision{};
        for (browser_fs::directory_iterator it(directory,
                 browser_fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            hash = fold_entry(hash, *it, isDirectory, isSymlink, revision);
            ++count;
        }
        return hash;
    }

    bool same_listing(const browser_directory_listing& a, const browser_directory_listing& b)
    {
        if (a.valid != b.valid || a.error != b.error || a.entries.size() != b.entries.size()) return false;
        for (size_t i = 0; i < a.entries.size(); ++i)
        {
            const auto& left = a.entries[i];
            const auto& right = b.entries[i];
            if (left.revision != right.revision || left.isDirectory != right.isDirectory
                || left.isSymlink != right.isSymlink || left.pathUtf8 != right.pathUtf8) return false;
        }
        return true;
    }

    void finish_scan(cache_slot& slot, clock_type::time_point startedAt)
    {
        const auto now = clock_type::now();
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(now - startedAt);
        slot.revalidateAfter = std::max(std::chrono::milliseconds(kRevalidateMs), spent * kRevalidateCostFactor);
        slot.scannedAt = now;
        ++state().stats.scans;
    }

    void scan_into(const browser_fs::path& directory, cache_slot& slot, bool fresh)
    {
        const auto startedAt = clock_type::now();
        // W2-B: 다시 훑을 때는 지문부터 본다. 같으면 목록을 만들지 않는다 —
        // 파일 5 만 개 폴더에서 목록 만들기·정렬·버리기가 초마다 0.5 초를 멈췄다.
        if (!fresh && slot.listing.valid)
        {
            size_t count = 0;
            std::error_code probeError;
            const std::uint64_t fingerprint = fingerprint_directory(directory, count, probeError);
            if (!probeError && count == slot.listing.entries.size() && fingerprint == slot.fingerprint)
            {
                finish_scan(slot, startedAt);
                return;
            }
        }

        browser_directory_listing listing;
        std::uint64_t fingerprint = kFingerprintSeed;
        std::error_code ec;
        for (browser_fs::directory_iterator it(directory,
                 browser_fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            browser_directory_entry entry;
            // ★ 종류·revision 은 여기서 한 번만 묻는다. 예전에는 정렬 비교자가
            //   비교마다 `is_directory(ec)` 를 불렀다 — 비교 횟수만큼 stat 이다.
            fingerprint = fold_entry(fingerprint, *it, entry.isDirectory, entry.isSymlink, entry.revision);
            entry.path = it->path();
            entry.nameUtf8 = to_utf8(it->path().filename());
            entry.pathUtf8 = to_utf8(it->path());
            entry.extension = it->path().extension().string();
            listing.entries.push_back(std::move(entry));
        }
        listing.error = ec;
        listing.valid = !ec;
        // 정렬은 여기서 한 번 해 둔다 — 폴더 먼저, 그 다음 이름 오름차순.
        // 창이 내림차순을 원하면 뒤집기만 하면 되고, 그것은 비교가 아니라
        // 순회 방향이라 stat 을 다시 부르지 않는다.
        // ★ 이미 담아 둔 UTF-8 이름으로 비교한다. `path.filename()` 은 비교마다 경로
        //   둘을 새로 만들어 5 만 개에서 정렬만 150ms 였다(이름 비교 8ms). 전체
        //   자산 범위가 같은 키로 다시 세우므로 두 범위의 순서도 같아진다.
        std::sort(listing.entries.begin(), listing.entries.end(),
            [](const browser_directory_entry& a, const browser_directory_entry& b)
            {
                if (a.isDirectory != b.isDirectory) return a.isDirectory;
                return a.nameUtf8 < b.nameUtf8;
            });
        // W2-B: 내용이 같으면 목록 객체를 그대로 둔다 — 창이 기억한 항목 주소가 산다.
        if (fresh || !same_listing(slot.listing, listing))
        {
            slot.listing = std::move(listing);
            ++state().generation;
        }
        slot.fingerprint = fingerprint;
        finish_scan(slot, startedAt);
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
            scan_into(directory, slot, true);
            found = cache.slots.emplace(key, std::move(slot)).first;
            cache.stats.cached = cache.slots.size();
            return found->second.listing;
        }

        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock_type::now() - found->second.scannedAt);
        if (age >= found->second.revalidateAfter && cache.frameBudget > 0)
        {
            --cache.frameBudget;
            scan_into(directory, found->second, false);
            return found->second.listing;
        }
        ++cache.stats.hits;
        return found->second.listing;
    }

    const browser_directory_listing* browser_cache_peek(const browser_fs::path& directory)
    {
        if (directory.empty()) return nullptr;
        cache_state& cache = state();
        const auto found = cache.slots.find(to_utf8(directory));
        return found == cache.slots.end() ? nullptr : &found->second.listing;
    }

    void browser_cache_invalidate()
    {
        cache_state& cache = state();
        cache.stats.evictions += cache.slots.size();
        cache.slots.clear();
        cache.stats.cached = 0;
        ++cache.generation;
        cache.forceThisFrame = true;
    }

    std::uint64_t browser_cache_generation()
    {
        return state().generation;
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
