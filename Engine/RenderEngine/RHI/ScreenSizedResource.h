#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// 화면 크기를 따라가는 리소스의 계약 (PHASE 3-2 / 7 교차).
//
// ── 왜 필요했나 ──
//
// 창을 리사이즈해도 렌더 타깃이 따라오지 않았다. Texture::Resize2DViews가
// 인자로 받은 새 크기를 쓰지 않고 바뀌지 않은 m_desc로 텍스처를 다시 만들고
// 있었다(크기를 갱신하는 SetSize 호출이 주석 처리돼 있었다). 반면 뷰포트는
// 갱신되므로 뷰포트와 타깃 크기가 어긋나고, 그려진 부분만 타깃 좌상단에 몰린 채
// 그 타깃 전체가 화면에 늘려 그려진다 — 증상은 '화면이 구석에 작게 보인다'였다.
//
// 그런데 '전부 따라가게' 고칠 수는 없다. 리사이즈 이벤트에는 화면과 무관한
// 것들도 등록돼 있다(그림자 맵 2048, 컬러그레이딩 LUT, 라이트맵, 큐브맵).
// 그것들까지 창 크기가 되면 그림자 해상도가 창에 따라 출렁인다.
//
// 그래서 따라갈 것만 선언한다. 기본값은 '따라가지 않음'이다 —
// 빠뜨렸을 때 지금 상태로 남는 쪽이, 엉뚱하게 크기가 바뀌는 쪽보다 낫다.
//
// ── 왜 백엔드 중립인가 ──
//
// DX11이 교체되면 사라질 코드에 이 계약을 묶어 두면 DX12에서 같은 일을 다시
// 하게 된다. 크기 추종은 백엔드의 문제가 아니라 '창이 바뀌면 무엇이 따라가야
// 하는가'의 문제라, 양쪽이 같은 버스를 구독한다.

struct ScreenSizePolicy
{
    // 화면 크기를 따라가는가. 기본은 아니오 — 선언한 것만 따라간다.
    bool follows{ false };

    // 화면의 1/N 해상도로 쓰는 버퍼가 있다(SSAO·블룸 등). 0은 1로 본다.
    uint32_t divisorX{ 1 };
    uint32_t divisorY{ 1 };

    uint32_t ApplyX(uint32_t screenWidth) const
    {
        const uint32_t d = (0 == divisorX) ? 1u : divisorX;
        return (screenWidth / d) > 0 ? (screenWidth / d) : 1u;
    }

    uint32_t ApplyY(uint32_t screenHeight) const
    {
        const uint32_t d = (0 == divisorY) ? 1u : divisorY;
        return (screenHeight / d) > 0 ? (screenHeight / d) : 1u;
    }
};

// 창 크기 변경을 알리는 버스.
//
// 기존 TextureManager의 OnTextureResizeEvent와 나란히 두지 않고 이쪽 하나로
// 모은다. 둘을 두면 '어느 쪽에 등록해야 하는가'가 규율이 되고, 규율은 언젠가
// 어긋난다.
//
// 순서 계약: 해제(Release) → 크기 통지(Resize) 두 단계다. DX11 스왑체인은
// 백버퍼를 참조하는 뷰가 하나라도 살아 있으면 리사이즈가 실패하므로, 만들기
// 전에 전부 놓아야 한다. DX12는 그 제약이 없지만 같은 순서를 따르는 편이
// 호출부가 백엔드를 몰라도 되게 한다.
// 화면을 따라가겠다고 선언한 리소스의 명부.
//
// 진단용이다. "따라가야 하는데 안 따라간 것"이 하나만 있어도 그 텍스처를 쓰는
// 패스가 어긋나는데, 화면만 보면 어느 텍스처인지 알 수 없다 — 렌더 타깃이
// 수십 개고 대부분 중간 결과라 화면에 직접 보이지도 않는다. 이름과 크기를
// 나란히 찍을 수 있어야 한다(render.rtinfo).
//
// 추종을 선언한 것만 들어오므로 목록이 짧다(수십 개). 전부 담았다면 텍스처
// 수백 개를 훑게 되고 그건 진단이 아니라 소음이다.
class ScreenSizedRegistry
{
public:
    struct Entry
    {
        const void* owner{ nullptr };
        std::string name;
        std::function<std::pair<uint32_t, uint32_t>()> querySize;
    };

    static ScreenSizedRegistry& Get()
    {
        static ScreenSizedRegistry instance;
        return instance;
    }

    void Register(const void* owner, std::string name,
        std::function<std::pair<uint32_t, uint32_t>()> querySize)
    {
        if (nullptr == owner) return;

        std::lock_guard<std::mutex> guard(m_mutex);
        Unregister_NoLock(owner);
        m_entries.push_back(Entry{ owner, std::move(name), std::move(querySize) });
    }

    void Unregister(const void* owner)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        Unregister_NoLock(owner);
    }

    std::vector<Entry> Snapshot() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_entries;
    }

private:
    void Unregister_NoLock(const void* owner)
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].owner == owner)
            {
                m_entries.erase(m_entries.begin() + i);
                return;
            }
        }
    }

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

class ScreenResizeBus
{
public:
    using Handle = uint64_t;
    static constexpr Handle kInvalidHandle = 0;

    using ReleaseCallback = std::function<void()>;
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

    static ScreenResizeBus& Get()
    {
        static ScreenResizeBus instance;
        return instance;
    }

    Handle Subscribe(ReleaseCallback onRelease, ResizeCallback onResize)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        const Handle handle = ++m_nextHandle;
        m_subscribers.push_back(Subscriber{ handle, std::move(onRelease), std::move(onResize) });
        return handle;
    }

    void Unsubscribe(Handle handle)
    {
        if (kInvalidHandle == handle) return;

        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_subscribers.size(); ++i)
        {
            if (m_subscribers[i].handle == handle)
            {
                m_subscribers.erase(m_subscribers.begin() + i);
                return;
            }
        }
    }

    // 현재 화면 크기. 리소스를 새로 만들 때 이 값을 쓰면 '만들어진 시점의
    // 크기'와 '지금 크기'가 갈릴 일이 없다.
    void SetSize(uint32_t width, uint32_t height)
    {
        m_width = width;
        m_height = height;
    }

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    /// 종횡비. 예전에는 DX11 전역(g_aspectRatio)이 이 값을 따로 들고 있었는데,
    /// 크기와 비율이 서로 다른 자리에 있으면 리사이즈 도중 어긋난다 - 같은
    /// 출처에서 계산하면 그럴 수가 없다(D4).
    float GetAspectRatio() const
    {
        return (0 == m_height) ? 1.f
            : static_cast<float>(m_width) / static_cast<float>(m_height);
    }

    void BroadcastRelease()
    {
        for (const auto& subscriber : SnapshotSubscribers())
        {
            if (subscriber.onRelease) subscriber.onRelease();
        }
    }

    void BroadcastResize(uint32_t width, uint32_t height)
    {
        SetSize(width, height);
        for (const auto& subscriber : SnapshotSubscribers())
        {
            if (subscriber.onResize) subscriber.onResize(width, height);
        }
    }

private:
    struct Subscriber
    {
        Handle          handle{ kInvalidHandle };
        ReleaseCallback onRelease;
        ResizeCallback  onResize;
    };

    // 콜백이 도는 동안 구독이 바뀔 수 있다(리소스를 다시 만들면서 등록/해제).
    // 잠금을 쥔 채 콜백을 부르면 그 자리에서 교착하므로 사본을 떠서 돈다.
    std::vector<Subscriber> SnapshotSubscribers() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_subscribers;
    }

    mutable std::mutex      m_mutex;
    std::vector<Subscriber> m_subscribers;
    Handle                  m_nextHandle{ kInvalidHandle };
    uint32_t                m_width{ 0 };
    uint32_t                m_height{ 0 };
};

