#pragma once
// InlineVector<T, N> — 작은 버퍼 최적화(SBO) 벡터 (SceneGraphRedesignPlan §4
// 트랙 K, K2 스테이지 B).
//
// 목적: GameObject::m_components(오브젝트당 실사용 규모 0~6개, 씬 58%·프리팹
// 89%가 0개 — GameObject.h FindComponentSlot 주석의 실측)처럼 대부분 인라인
// 용량 안에서 끝나는 벡터가, 매 오브젝트마다 힙 할당 하나(빈 벡터라도 push_back
// 첫 호출에서)를 무조건 무는 비용을 없앤다. N개까지는 객체 안의 고정 버퍼에
// 산다 — 힙 할당은 N을 넘어설 때만 일어난다.
//
// ── 불변식 (위반 시 이 설계를 재검토할 것) ──────────────────────────────────
//
//   1. 인라인 버퍼에 저장되는 것은 포인터 크기 요소(예: Managed::UniquePtr<T>,
//      곧 std::unique_ptr<T>)뿐이다. Component 객체 그 자체의 바이트를
//      인라인화하지 않는다 — m_components가 담는 것은 언제나 unique_ptr
//      핸들(포인터 하나) 뿐이고, 가리키는 Component 인스턴스는 여전히 힙에
//      있다(unique_alloc/AddComponent가 `new T(...)`로 만든 그 주소).
//
//   2. 요소 재배치는 std::vector와 같은 시점(용량을 넘어서 커질 때)에만
//      일어난다. 그 순간에도 옮겨지는 것은 unique_ptr 핸들(포인터의 포인터)
//      뿐이지 Component 인스턴스가 아니다 — Component* 자체(가리키는 주소)는
//      InlineVector가 커지든, 원소가 지워지든 불변이다. 따라서 다른 시스템이
//      캐시해 둔 Component* raw 포인터(Scene::RegisterComponent가 SystemSchedule
//      페이즈 리스트에 얹어 두는 것 등)는 InlineVector 도입으로 조금도 덜
//      안전해지지 않는다 — std::vector<UniquePtr<T>> 시절과 동일한 보장이다.
//
// ── 인터페이스 ───────────────────────────────────────────────────────────
//
// std::vector 인터페이스의 부분집합만 구현한다(호출처 무변경이 목표):
// begin/end/size/empty/operator[]/push_back/emplace_back/erase/clear/reserve.
// 무브 온리 요소(unique_ptr 등)를 1급으로 지원한다 — 복사 생성자/대입은
// 요소가 복사 가능할 때만 조건부로 열린다(C++20 제약 특수 멤버 함수, P0848과
// 같은 패턴: std::optional/variant가 쓰는 관용구). 이 조건부성이 바로
// K2 스테이지 A가 기댔던 함정 방지책과 맞물린다 — TypeTrait.h의
// IsCopyableForProperty<T>()는 std::is_copy_constructible_v<T>를 묻는데,
// InlineVector<UniquePtr<Component>,4>가 여기서 정확히 false로 판정되려면
// (std::vector와 달리) 복사 생성자를 원소 비복사 가능 시 아예 열지 않아야
// 한다 — std::vector의 복사 생성자는 원소 타입과 무관하게 항상 "선언"돼 있어
// is_copy_constructible_v가 true로 오판하는 것과 대비된다(TypeTrait.h 주석).
#include <cstddef>
#include <type_traits>
#include <utility>
#include <new>
#include <algorithm>
#include <iterator>

template<class T, std::size_t N = 4>
class InlineVector
{
    static_assert(N >= 1, "InlineVector: 인라인 용량 N은 1 이상이어야 한다");

public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;

    InlineVector() noexcept
        : m_data(InlinePtr())
        , m_size(0)
        , m_capacity(N)
    {
    }

    ~InlineVector()
    {
        DestroyAll();
        Deallocate();
    }

    // ── 이동 — 항상 가능(요소가 이동 가능해야 함, 대부분의 실사용 T가 그렇다).
    // noexcept는 요소의 이동 생성자 noexcept성에 조건부로 걸린다 — 인라인
    // 모드끼리 이동할 때는 원소별로 실제 이동 생성을 호출하기 때문(힙 모드는
    // 포인터 3개만 훔쳐 항상 무예외다). GameObject(GameObject&&) noexcept =
    // default가 이 조건부 noexcept에 기댄다 — Managed::UniquePtr(=unique_ptr)의
    // 이동 생성자가 noexcept이므로 실사용 시점에는 noexcept(true)로 접힌다.
    InlineVector(InlineVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : m_data(InlinePtr())
        , m_size(0)
        , m_capacity(N)
    {
        MoveFrom(other);
    }

    InlineVector& operator=(InlineVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        if (this != &other)
        {
            DestroyAll();
            Deallocate();
            m_data = InlinePtr();
            m_capacity = N;
            m_size = 0;
            MoveFrom(other);
        }
        return *this;
    }

    // ── 복사 — 요소가 복사 가능할 때만 (C++20 제약 특수 멤버 함수 관용구).
    // 두 선언은 상호 배타적 제약이라 항상 정확히 하나만 유효 후보다 — T가
    // 복사 불가면 아래 delete 쪽이 뽑히고, std::is_copy_constructible_v<
    // InlineVector<T,N>>는 정확히 false를 돌려준다(std::vector의 함정과 달리
    // "선언은 있는데 본문만 깨지는" 어긋남이 없다).
    InlineVector(const InlineVector& other)
        requires std::is_copy_constructible_v<T>
        : m_data(InlinePtr())
        , m_size(0)
        , m_capacity(N)
    {
        reserve(other.m_size);
        for (size_type i = 0; i < other.m_size; ++i)
        {
            ::new (static_cast<void*>(m_data + i)) T(other.m_data[i]);
        }
        m_size = other.m_size;
    }

    InlineVector(const InlineVector&)
        requires (!std::is_copy_constructible_v<T>)
    = delete;

    InlineVector& operator=(const InlineVector& other)
        requires std::is_copy_constructible_v<T>
    {
        if (this != &other)
        {
            InlineVector tmp(other);
            *this = std::move(tmp);
        }
        return *this;
    }

    InlineVector& operator=(const InlineVector&)
        requires (!std::is_copy_constructible_v<T>)
    = delete;

    // ── 반복자 ──────────────────────────────────────────────────────────
    iterator begin() noexcept { return m_data; }
    iterator end() noexcept { return m_data + m_size; }
    const_iterator begin() const noexcept { return m_data; }
    const_iterator end() const noexcept { return m_data + m_size; }
    const_iterator cbegin() const noexcept { return m_data; }
    const_iterator cend() const noexcept { return m_data + m_size; }

    // ── 크기/용량 ───────────────────────────────────────────────────────
    size_type size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }
    size_type capacity() const noexcept { return m_capacity; }

    void reserve(size_type newCapacity)
    {
        if (newCapacity > m_capacity)
        {
            GrowTo(newCapacity);
        }
    }

    // ── 원소 접근 ───────────────────────────────────────────────────────
    reference operator[](size_type index) noexcept { return m_data[index]; }
    const_reference operator[](size_type index) const noexcept { return m_data[index]; }

    // ── 삽입 ────────────────────────────────────────────────────────────
    void push_back(const T& value)
        requires std::is_copy_constructible_v<T>
    {
        EnsureCapacityForOne();
        ::new (static_cast<void*>(m_data + m_size)) T(value);
        ++m_size;
    }

    void push_back(T&& value)
    {
        EnsureCapacityForOne();
        ::new (static_cast<void*>(m_data + m_size)) T(std::move(value));
        ++m_size;
    }

    template<class... Args>
    reference emplace_back(Args&&... args)
    {
        EnsureCapacityForOne();
        T* slot = m_data + m_size;
        ::new (static_cast<void*>(slot)) T(std::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }

    // ── 삭제 — [first, last) 구간을 지우고 뒤 원소를 당겨 채운다(std::vector와
    // 동일 시맨틱). std::erase_if(InlineVector&, Pred)는 std 네임스페이스가
    // 아니라 이 헤더가 전역에 두는 자유 함수 erase_if(ADL) 몫이다 — std::vector
    // 전용 오버로드가 std 안에 있어, 사용부는 std:: 한정자를 뗀 erase_if(...)를
    // 쓴다(Scene.cpp·PrefabUtility.cpp).
    iterator erase(iterator first, iterator last)
    {
        iterator writePos = first;
        for (iterator readPos = last; readPos != end(); ++readPos, ++writePos)
        {
            *writePos = std::move(*readPos);
        }
        for (iterator p = writePos; p != end(); ++p)
        {
            p->~T();
        }
        m_size -= static_cast<size_type>(last - first);
        return first;
    }

    iterator erase(iterator pos)
    {
        return erase(pos, pos + 1);
    }

    void clear() noexcept
    {
        DestroyAll();
    }

private:
    T* InlinePtr() noexcept { return reinterpret_cast<T*>(m_inlineStorage); }
    const T* InlinePtr() const noexcept { return reinterpret_cast<const T*>(m_inlineStorage); }
    bool IsInline() const noexcept { return m_data == InlinePtr(); }

    static T* Allocate(size_type n)
    {
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
    }

    // 현재 m_data·m_capacity 기준으로 힙 버퍼를 해제한다(인라인 모드면 아무
    // 일도 안 한다) — 호출 시점에 m_capacity가 실제로 그 버퍼를 할당할 때 쓴
    // 값과 같아야 한다(GrowTo가 이 순서를 지킨다: 새 버퍼로 옮긴 뒤, m_capacity를
    // 새 값으로 덮어쓰기 전에 옛 버퍼를 이 함수로 해제).
    void Deallocate() noexcept
    {
        if (!IsInline())
        {
            ::operator delete(m_data, m_capacity * sizeof(T), std::align_val_t(alignof(T)));
        }
    }

    void DestroyAll() noexcept
    {
        for (size_type i = 0; i < m_size; ++i)
        {
            m_data[i].~T();
        }
        m_size = 0;
    }

    void EnsureCapacityForOne()
    {
        if (m_size == m_capacity)
        {
            GrowTo(m_capacity * 2);
        }
    }

    // 용량을 newCapacity로 키운다(newCapacity > m_capacity 전제). 기존 원소를
    // 새 버퍼로 이동 생성한 뒤 옛 자리를 파괴하고, 옛 버퍼가 힙이었으면
    // 해제한다 — 인라인이었으면 Deallocate()가 아무 일도 안 한다(불변식 2:
    // 이 이동은 요소가 unique_ptr이면 핸들만 옮기지 Component 인스턴스를
    // 건드리지 않는다).
    void GrowTo(size_type newCapacity)
    {
        T* newData = Allocate(newCapacity);

        for (size_type i = 0; i < m_size; ++i)
        {
            ::new (static_cast<void*>(newData + i)) T(std::move(m_data[i]));
            m_data[i].~T();
        }

        Deallocate();
        m_data = newData;
        m_capacity = newCapacity;
    }

    // *this는 호출 시점에 "빈 인라인" 상태(m_data==InlinePtr(), m_size==0,
    // m_capacity==N)여야 한다 — 생성자 직후이거나, 이동 대입이 자신의 기존
    // 내용을 이미 비운 뒤다.
    void MoveFrom(InlineVector& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        if (other.IsInline())
        {
            // 인라인끼리는 훔칠 버퍼가 없다 — 원소를 하나씩 이동 생성한 뒤
            // 원본 자리를 파괴한다(원본이 인라인 슬롯을 다시 쓰지 않도록
            // m_size를 0으로 내린다).
            for (size_type i = 0; i < other.m_size; ++i)
            {
                ::new (static_cast<void*>(m_data + i)) T(std::move(other.m_data[i]));
                other.m_data[i].~T();
            }
            m_size = other.m_size;
            other.m_size = 0;
        }
        else
        {
            // 힙 버퍼는 통째로 훔친다 — O(1), 원소 이동 생성 0회. 원본의
            // 인라인 슬롯은 애초에 쓰인 적이 없으므로(원본이 힙 모드였다는
            // 뜻) 파괴할 것도 없다 — size만 0으로.
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;

            other.m_data = other.InlinePtr();
            other.m_capacity = N;
            other.m_size = 0;
        }
    }

    alignas(T) unsigned char m_inlineStorage[sizeof(T) * N];
    T* m_data;
    size_type m_size;
    size_type m_capacity;
};

// std::erase_if(std::vector<T,Alloc>&, Pred)는 <vector>가 std 네임스페이스
// 안에 선언하는 std::vector 전용 오버로드다 — InlineVector는 std::vector가
// 아니므로 그 오버로드 집합에 안 걸린다(그리고 표준이 사용자 정의 타입을 위해
// std 네임스페이스에 새 오버로드를 얹는 것은 "기존 템플릿의 특수화"가 아니라
// 허용 범위 밖이다). 그래서 이 자유 함수를 InlineVector와 같은 전역
// 네임스페이스에 둔다 — 호출부(Scene.cpp·PrefabUtility.cpp)는 std:: 한정자
// 없이 unqualified erase_if(...)로 불러 일반 이름 조회/ADL로 이 오버로드를
// 찾는다.
template<class T, std::size_t N, class Pred>
inline typename InlineVector<T, N>::size_type erase_if(InlineVector<T, N>& container, Pred pred)
{
    auto it = std::remove_if(container.begin(), container.end(), pred);
    const auto removed = static_cast<typename InlineVector<T, N>::size_type>(std::distance(it, container.end()));
    container.erase(it, container.end());
    return removed;
}
