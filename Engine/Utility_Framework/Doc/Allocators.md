# CPU 할당자 사용 가이드

Utility의 할당자는 메모리 배치와 재사용을 담당한다. 객체의 소유자, 작업 완료 시점, CPU 제출과 GPU 실행 완료의 관계는 호출 계층이 결정한다. 모든 arena와 `fixed_pool`은 한 writer가 사용하며 내부 동기화를 제공하지 않는다.

## 선택 기준

| 필요 | 구성 | 저장소와 회수 방식 |
|---|---|---|
| 서로 다른 수명을 가진 일반 컨테이너 | `heap_allocator<T>` | mimalloc heap에 개별 할당·해제 |
| 크기가 변하지만 반복되는 작업의 임시 데이터 | `arena_resource` | 부족할 때 블록을 추가하고 `reset()`으로 기존 블록 재사용 |
| arena 위의 STL 컨테이너 | `arena_allocator<T>`, `arena_vector<T>`, `arena_map<K, V>` | 지정한 resource에서 할당; 컨테이너 소멸 후 resource reset |
| trivial 값의 가벼운 가변 길이 버퍼 | `arena_buffer<T>` | arena를 빌리며 원소별 소멸자 관리 없음 |
| 소멸자가 필요한 여러 객체를 함께 보관 | `arena_objects` | 객체 생성과 역순 소멸을 관리한 뒤 raw arena reset |
| 상한이 정해진 단일 작업 scratch | `linear_arena` | 호출자가 제공한 버퍼에서 순차 할당; 전체 reset |
| 중첩 작업이 끝날 때 해당 임시 영역 반환 | `stack_arena` | 호출자 버퍼와 LIFO RAII checkpoint |
| 최대 개수가 정해진 동일 타입 객체의 개별 생성·삭제 | `fixed_pool<T, Capacity>` | 내부 고정 슬롯과 free list; 객체별 반환 |

`linear_arena`, `stack_arena`, `fixed_pool`의 backing storage와 정상 할당·재사용·`try_*` 용량 부족 경로는 heap을 요청하지 않는다. 다만 throwing overload나 잘못된 reset 사용에서 발생하는 예외는 런타임 또는 오류 메시지 저장소를 할당할 수 있고, `T`의 생성자도 별도로 할당할 수 있다. 실패 경로까지 엄격한 예산이 필요하면 `try_allocate`·`try_reset`·`try_emplace`를 사용한다. `try_emplace`도 `T`의 생성자 예외는 그대로 전파한다. `arena_resource`는 용량 확장 시 heap 블록을 요청하므로 **고정 예산 안에서 반드시 실패해야 하는 작업**에는 bounded 모델을 사용한다.

기존 `MemoryPool`은 별개의 object-slot 구현이다. 이 문서의 타입은 `Core.Memory.hpp`, Windows/COM 또는 RenderEngine 헤더를 포함할 필요가 없다.

## Heap과 재사용 arena

구현: [HeapAllocator.h](../HeapAllocator.h), [Arena.h](../Arena.h), [Arena.cpp](../Arena.cpp).

`heap_allocator<T>`는 stateless STL allocator이며 `mi_malloc_aligned` / `mi_free`를 사용한다. 같은 mimalloc provider를 사용하는 다른 스레드에서 해제할 수 있다. 타입 자체가 컨테이너의 동시 접근까지 안전하게 만드는 것은 아니다.

`arena_resource::allocate(bytes, alignment)`는 2의 거듭제곱 정렬을 요구한다. 0바이트 요청은 최소 1바이트로 처리한다. 잘못된 정렬, 크기 오류 또는 메모리 부족은 `std::bad_alloc`을 발생시킨다. 블록 주소는 resource 소멸까지 유지되지만, **할당 결과의 사용 가능 기간은 다음 reset 전까지**다.

기존 렌더러의 블록 정책을 유지하므로 새 블록의 payload capacity는 최소 64KiB다. 더 큰 요청은 정렬 여유를 포함한 블록을 추가한다. `reset()`은 보유 블록을 줄이지 않으며 실제 해제는 resource 소멸 시 수행한다. 작은 고정 예산의 작업에는 아래의 외부 버퍼 기반 모델을 선택한다.

`arena_resource::try_allocate(bytes, alignment, memory)`는 현재 활성 블록만 확인하는 빠른 경로다. 새 블록 생성이나 다른 유지 블록 검색을 하지 않으며, reset 직후에도 실패할 수 있다. 실패 시 출력 포인터를 변경하지 않는다. 일반적인 arena 할당은 `allocate`를 사용한다. 이 함수는 아래 bounded arena의 `try_allocate`와 계약이 다르다.

`reset()`은 객체를 소멸시키지 않고 각 블록의 cursor만 되돌린다. `std::vector::clear()`만 호출하면 컨테이너의 capacity가 이전 메모리를 계속 가리킨다. 따라서 해당 컨테이너를 소멸시킨 뒤 reset하고, 다음 작업에서는 resource에 다시 연결한 컨테이너를 생성한다. `arena_buffer`와 반환된 span에도 같은 수명 제한이 적용된다.

다음 예제는 resource를 멤버로 유지한다. 각 작업의 컨테이너는 짧게 존재하지만 arena 블록은 다음 작업에서도 재사용한다.

```cpp
#include "Arena.h"
#include "HeapAllocator.h"
#include <cstdint>
#include <vector>

class QueryScratch
{
  public:
    std::size_t run()
    {
        std::size_t result{};
        {
            ce::arena_vector<std::uint32_t> candidates{&m_storage};
            ce::arena_buffer<std::uint32_t> indices{m_storage};
            candidates.reserve(128);
            indices.reserve(128);
            for (std::uint32_t index = 0; index < 128; ++index)
            {
                candidates.push_back(index);
                indices.push_back(index);
            }
            result = candidates.size() + indices.size();
        }
        m_storage.reset();
        return result;
    }

  private:
    ce::arena_resource m_storage;
};

using PersistentIds = std::vector<std::uint32_t, ce::heap_allocator<std::uint32_t>>;
```

`arena_buffer<T>`는 trivially copyable이면서 trivially destructible인 타입만 지원한다. 원소 수를 예상할 수 있으면 `reserve`로 growth 중 복사를 줄인다. 이전 버퍼 공간은 개별 해제되지 않고 resource reset 때 함께 재사용된다.

### STL allocator의 복사·fallback 계약

resource가 지정된 `arena_allocator`는 해당 resource만 사용하며 개별 `deallocate`는 메모리를 반환하지 않는다. **resource가 없는 기본 allocator는 `heap_allocator`를 사용한다.** `arena_vector<T> values;`처럼 기본 생성한 컨테이너를 arena 기반이라고 가정하지 않는다.

컨테이너의 일반 복사 생성은 의도적으로 resource를 상속하지 않는다. 복사본이 원본 작업보다 오래 살아도 arena를 빌리지 않도록 heap allocator 상태로 전환한다. arena에 남길 복사는 allocator를 명시한다.

```cpp
#include "Arena.h"
#include <cassert>
#include <cstdint>

ce::arena_vector<std::uint32_t> MakeDetachedIds()
{
    ce::arena_resource storage;
    ce::arena_vector<std::uint32_t> source{&storage};
    source.assign({1, 2, 3});

    ce::arena_vector<std::uint32_t> sameLifetime(source, ce::arena_allocator<std::uint32_t>{&storage});
    ce::arena_vector<std::uint32_t> detached(source);
    assert(sameLifetime.get_allocator().resource() == &storage);
    assert(detached.get_allocator().resource() == nullptr);
    return detached;
}
```

이 예제의 `detached` 복사는 heap 할당을 의도한 것이다. 반복 작업에서 필요 없는 복사를 만들지 않는다. allocator 전파를 지원하는 중첩 arena 컨테이너와 map의 mapped value는 같은 resource를 전달받지만, 내부적으로 별도 allocator를 사용하는 임의 타입의 할당까지 자동으로 전환하지는 않는다.

## 객체 수명을 함께 관리하는 arena

`arena_objects::emplace<T>`는 생성자를 실행하고, 소멸이 필요한 객체를 역순 소멸 목록에 등록한다. `reset()`과 소멸자는 객체를 먼저 파괴한 뒤 블록을 재사용하거나 해제한다. `T`의 소멸자는 예외를 던지지 않아야 한다. `copy<T>(span)`은 값을 arena에 복사하며, nontrivial 원소도 소멸을 관리한다.

생성자나 복사 생성자가 던진 예외는 호출자에게 전달된다. 실패 과정에서 이미 생성한 배열 원소는 파괴되지만, 할당한 raw bytes는 다음 reset까지 유지될 수 있다. 실패한 요청마다 arena 전체를 자동으로 되돌리지는 않는다.

```cpp
#include "Arena.h"
#include <array>
#include <cassert>
#include <span>

struct ScratchObject
{
    int* destroyed;
    explicit ScratchObject(int& counter) noexcept : destroyed(&counter) {}
    ~ScratchObject() noexcept { ++*destroyed; }
};

void ObjectArenaExample()
{
    int destroyed = 0;
    ce::arena_objects objects;
    (void)objects.emplace<ScratchObject>(destroyed);
    const std::array<int, 3> source{2, 4, 8};
    const std::span<const int> copied = objects.copy<int>(source);
    assert(copied[1] == 4);
    objects.reset();
    assert(destroyed == 1);
    // copied와 emplace가 반환한 포인터는 이 시점부터 사용하지 않는다.
}
```

## 고정 버퍼 선형 할당자

구현: [LinearArena.h](../LinearArena.h).

`linear_arena`는 `std::span<std::byte>`를 빌린다. 버퍼를 소유하거나 확장하지 않는다. `try_allocate`는 공간 부족, 잘못된 정렬 또는 표현할 수 없는 요청에 `nullptr`을 반환하고 상태를 유지한다. `allocate`는 같은 실패에 `std::bad_alloc`을 던진다. 0바이트 요청은 1바이트를 사용하며, 정렬 padding도 버퍼 예산에 포함된다.

객체 생성·소멸은 호출자의 책임이다. 다음 예제에서 `destroy_at`보다 먼저 reset하면 안 된다.

```cpp
#include "LinearArena.h"
#include <array>
#include <cstdint>
#include <memory>

std::uint32_t LinearArenaExample()
{
    alignas(64) std::array<std::byte, 1024> backing{};
    ce::linear_arena scratch{backing};
    void* memory = scratch.try_allocate(sizeof(std::uint32_t), alignof(std::uint32_t));
    if (!memory)
    {
        return 0;
    }
    std::uint32_t* value = std::construct_at(static_cast<std::uint32_t*>(memory), 42u);
    const std::uint32_t result = *value;
    std::destroy_at(value);
    scratch.reset();
    return result;
}
```

## 중첩 작업용 스택 할당자

구현: [StackArena.h](../StackArena.h).

`stack_arena`는 같은 bounded allocation 계약에 `checkpoint()`를 추가한다. 반환된 `scope`는 현재 cursor를 보관하며 소멸할 때 해당 위치로 되돌린다. scope는 복사하거나 이동할 수 없다. 항상 arena를 먼저 선언하고, checkpoint를 생성한 역순으로 소멸시킨다. backing buffer는 arena와 모든 사용자를 포함하는 수명을 가져야 한다.

```cpp
#include "StackArena.h"
#include <array>
#include <cassert>

void StackArenaExample()
{
    alignas(64) std::array<std::byte, 4096> backing{};
    ce::stack_arena scratch{backing};
    {
        ce::stack_arena::scope task = scratch.checkpoint();
        (void)scratch.allocate(32, 16);
        {
            ce::stack_arena::scope child = scratch.checkpoint();
            (void)scratch.allocate(64, 64);
            assert(!scratch.rewind(task)); // child가 아직 active인 동안 거부.
        }
        assert(scratch.used_bytes() == 32);
    }
    assert(scratch.used_bytes() == 0);
}
```

`rewind(scope&)` 또는 `scope::rewind()`는 해당 arena의 active top checkpoint에서만 성공한다. 다른 arena의 scope, 이미 되돌린 scope, 내부 scope보다 먼저 되돌리려는 요청은 `false`를 반환하며 메모리를 변경하지 않는다. 수동 rewind가 성공한 scope의 소멸자는 다시 되돌리지 않는다.

active scope가 있으면 `try_reset()`은 `false`를 반환하고 `reset()`은 `std::logic_error`를 던진다. 정상적인 실패 처리는 이 API로 수행한다. **내부 scope가 남아 있는데 외부 scope를 파괴하거나, scope보다 arena를 먼저 파괴하면 `std::terminate`한다.** 연결된 checkpoint에 dangling pointer를 남기는 잘못된 수명 순서를 허용하지 않는다. scope는 raw memory만 관리하므로, 그 영역에 생성한 객체는 rollback 전에 소멸해야 한다.

## 고정 크기 객체 풀

구현: [FixedPool.h](../FixedPool.h).

`fixed_pool<T, Capacity>`는 양수의 컴파일 타임 capacity를 가진다. 미사용 슬롯에 free-list index를 저장하고 live bitmap으로 사용 상태를 관리한다. 개별 생성·반환은 O(1), 초기화·clear는 O(Capacity)다. `T`는 배열 또는 cv-qualified 타입일 수 없고 소멸자가 예외를 던지지 않아야 한다.

`try_emplace`는 풀이 가득 찼을 때만 생성자를 호출하지 않고 `nullptr`을 반환한다. `T`의 생성자가 던진 예외는 슬롯을 반환한 뒤 그대로 전파한다. `emplace`는 용량 부족에 `std::bad_alloc`을 던진다. `destroy`는 정확한 현재 live 슬롯만 받아들이며, null·다른 풀·슬롯 내부 주소·이미 반환된 슬롯에는 `false`를 반환한다. `clear`와 풀 소멸자는 남은 객체를 파괴한다. 파괴 순서에 의존하지 않는다.

```cpp
#include "FixedPool.h"

struct ParticleState
{
    float remaining;
    explicit ParticleState(float seconds) noexcept : remaining(seconds) {}
};

class ParticleSlots
{
  public:
    ParticleState* spawn(float seconds) { return m_particles.try_emplace(seconds); }
    bool remove(ParticleState* particle) noexcept { return m_particles.destroy(particle); }
    std::size_t available() const noexcept { return m_particles.available(); }

  private:
    ce::fixed_pool<ParticleState, 1024> m_particles;
};
```

반환된 raw pointer는 풀을 소유하지 않는다. 슬롯을 반환하면 해당 포인터는 사용을 끝내야 한다. **같은 주소에 새 객체가 생성된 뒤 예전 포인터가 다시 전달되는 경우는 구분하지 못한다.** 외부에 오래 보관하는 식별자가 필요하면 별도의 generation handle 계층을 사용한다. `T`의 생성자·소멸자에서 같은 풀을 재진입해 변경하지 않는다.

슬롯 저장소는 풀 객체 내부에 포함된다. 큰 capacity의 풀을 호출마다 지역 변수로 만들면 그만큼 stack 공간과 초기화 비용이 발생한다. 오래 사는 시스템의 멤버로 두고, 시스템 객체 자체가 놓이는 저장소와 예산도 함께 결정한다. `fixed_pool`은 STL allocator 어댑터가 아니므로 가변 크기 vector 저장소용으로 사용하지 않는다.

## 통계 해석

| 구성 | 제공 값 | 해석 |
|---|---|---|
| `arena_resource`, `arena_objects` | `arena_stats` | `retainedBytes`와 `blockCount`는 보유 블록; `activeBlocks`, `activeCapacity`, `usedBytes`는 현재 reset 구간; `resets`는 누적 reset 횟수 |
| `linear_arena`, `stack_arena` | `capacity`, `used_bytes`, `remaining_bytes`, `peak_bytes`, `allocation_count`, `reset_count` | used/peak에는 alignment padding 포함; 성공한 allocation만 누적; reset/rewind로 peak와 allocation count를 지우지 않음 |
| `stack_arena` | `rewind_count` | 성공한 수동·RAII checkpoint rollback 횟수; 전체 reset과 별개 |
| `fixed_pool` | `capacity`, `size`, `available`, `empty`, `full` | 객체 슬롯 단위의 현재 용량·사용량 |

`arena_stats::usedBytes`는 요청한 bytes의 합으로 alignment padding을 포함하지 않는다. `retainedBytes`는 블록 payload capacity이며 블록 헤더까지 포함한 전체 heap footprint는 아니다. `remaining_bytes()`도 정렬 전 남은 raw bytes이므로 같은 크기의 aligned 요청이 항상 성공한다는 뜻은 아니다. 통계는 단일 writer 계약 아래에서 읽는다.

## 렌더러와 빌드 경계

기존 `render_arena_*`와 `render_allocator` 이름은 Utility 구현의 별칭이다. 구현을 복제하거나 별도 가상 allocator 계층을 추가하지 않는다. mimalloc의 공통 provider는 Utility 프로젝트에서 컴파일하며 RenderEngine은 Utility에 의존한다. 여러 프로젝트에 `static.c`를 중복 추가하지 않는다.

[RenderArena](../../RenderEngine/RenderArena.h)에 남는 `RenderArenaSet`, `RenderArenaPool`, `RenderArenaLease`는 writer domain, 캐시 수·용량, 마지막 소유자 반환 같은 렌더 정책을 관리한다. `RenderGraph`의 compile/submit scratch 분리와 RHI의 완료 확인도 그대로 상위 계층 책임이다. **CPU 제출 함수가 반환됐다는 이유만으로 GPU가 아직 사용하는 데이터를 reset하지 않는다.** Utility에는 GPU fence, frame index 또는 graph epoch를 넣지 않는다.

## 검증 실행

저장소 루트에서 C++23 컴파일러와 C 컴파일러를 사용한다. standalone runner는 실제 mimalloc과 Utility 소스만으로 빌드·링크하고, 정렬·경계·예외·수명 및 반복 할당 예산을 검사한다. zero-allocation 예산은 예외를 던지지 않는 성공·재사용·`try_*` 경로를 대상으로 하며, 예외 처리 기구의 저장소까지 포함하지 않는다.

```sh
python3 Tools/regression/verify-utility-allocators.py \
  --output /tmp/utility-allocators.json

ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-utility-allocators.py \
  --sanitize --output /tmp/utility-allocators-sanitized.json
```

`--compiler`, `--c-compiler`, `--source-root`, `--keep-build`는 `--help`를 참조한다. 빌드 산출물을 유지할 때는 새 디렉터리 또는 빈 디렉터리를 지정한다. 두 번째 명령의 `detect_leaks=0`은 process inspection이 제한된 실행 환경용이며 ASan/UBSan 검사는 유지한다. 이 검증은 native Editor/GPU 프레임 시간 향상을 증명하지 않는다.
