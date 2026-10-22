# Utility CPU 할당자 분리 및 추가 모델 검증 — 2026-10-22

공통 CPU arena와 mimalloc heap 어댑터를 `Utility_Framework`로 옮겼다. 기존 렌더러는 같은 구현을 별칭으로 사용한다. 고정 예산의 작업에 사용할 선형 arena, 중첩 scope용 스택 arena, 고정 용량 객체 풀을 추가했다. 기존 렌더러의 반복 할당 예산은 유지됐으며, 새 bounded 모델은 측정한 성공·재사용 경로에서 backing heap 할당을 하지 않았다.

비교 기준은 공개 master 커밋 `d2a705ff3b03d9936484b4cac94e8e710f9f4ab5`다. 로컬 비교 커밋 `c3dd735933a8e2b64795bb86e07ac56cd09543d8`과 트리 `3ff049a3f19c43447407201b89c924bdf2a127db`가 동일하다. 이번 작업은 이전 할당 최적화를 유지하는 구조 분리이며, 새로운 전체 프레임 시간 개선을 주장하지 않는다.

## 구현 범위

| 구성 | 구현과 용도 |
|---|---|
| `heap_allocator<T>` | 기존 mimalloc 기반 stateless STL allocator를 Utility로 이동 |
| `arena_resource`, `arena_stats` | 기존 블록 정렬·증가·유지·reset·통계를 Utility로 이동 |
| `arena_allocator<T>`, `arena_vector<T>`, `arena_map<K, V>` | 기존 nested allocator 전파와 복사 시 heap 분리 계약 유지 |
| `arena_buffer<T>`, `arena_objects` | trivial 버퍼와 역순 객체 소멸 기능 이동 |
| `linear_arena` | 호출자가 제공한 byte span에서 정렬된 순차 할당; 부족하면 실패; 전체 reset |
| `stack_arena` | bounded arena에 주소에 묶인 LIFO RAII checkpoint 추가; 중첩 작업 종료 시 cursor 복구 |
| `fixed_pool<T, Capacity>` | 내부 고정 슬롯과 live bitmap, 미사용 슬롯의 free list를 이용한 개별 객체 생성·반환 |

사용법과 실제 C++ 예제는 [CPU 할당자 사용 가이드](../../Engine/Utility_Framework/Doc/Allocators.md)에 있다.

`RenderArenaSet`, `RenderArenaPool`, `RenderArenaLease`, pointer identity set, renderer 소유권 factory는 RenderEngine에 남겼다. 기존 `render_arena_*`와 `render_allocator<T>`는 같은 Utility 타입의 별칭이다. 추가 가상 allocator 계층, 전역 mutex 또는 할당마다 생성되는 공유 소유권 객체를 넣지 않았다.

mimalloc `static.c`의 컴파일 소유권은 RenderEngine에서 Utility로 옮겼으며, 전체 `.vcxproj` 검사에서 provider가 한 곳인 것을 확인했다. 기존 C 컴파일, PCH 제외, unity 제외 설정을 유지했다. portable runner와 Windows RHI command probe의 명시적인 소스 목록도 새 `Arena.cpp`를 포함하도록 갱신했다. 과거 source snapshot과 비교하는 runner는 Utility 분리 전 소스도 빌드할 수 있다.

네 개의 기존 mirror runner에는 이미 빠져 있던 `Ownership.h`, `LifetimeToken.h`, vendored ownership 헤더를 보완했다. declaration-storage runner의 `RHISubresourceRange.h`도 함께 보완했다. 미러 파일은 기존 snapshot과 해시 기록에 포함되며, fixture 의미나 개선 기준을 바꾸지는 않았다.

## 수명과 예산

- retained arena는 기존처럼 최소 64KiB payload 블록을 추가하고 reset 때 유지한다. 블록 해제는 resource 소멸 시 수행한다.
- bounded 선형·스택 arena는 외부 버퍼를 빌린다. 고정 풀은 슬롯을 자체 객체 안에 가진다. 이 모델들은 backing storage를 heap으로 자동 확장하지 않는다.
- `try_allocate`는 정렬·용량 실패 시 상태를 바꾸지 않고 null을 반환한다. `try_reset`과 명시적인 stack rewind도 거절 시 상태를 유지한다.
- stack checkpoint는 복사·이동할 수 없다. 다른 arena, 이미 종료된 checkpoint, 현재 top이 아닌 checkpoint의 rewind는 실패한다. 잘못된 scope/arena 파괴 순서는 종료 처리하여 dangling checkpoint chain을 허용하지 않는다.
- fixed pool은 생성자가 예외를 던지면 슬롯을 복원한다. live bitmap으로 foreign/interior/double release를 거절하고, 남은 live 객체는 풀 소멸 시 파괴한다. 같은 주소가 새 객체에 재사용된 뒤의 stale raw pointer 구분은 별도 generation handle의 책임이다.
- raw arena의 객체 소멸은 호출자 책임이다. `arena_objects`와 fixed pool은 자신이 생성한 객체의 소멸을 관리한다. 같은 pool의 constructor/destructor 재진입은 지원하지 않는다.
- 예외 런타임·오류 메시지와 사용자 payload 생성자는 별도로 할당할 수 있다. 할당 예산 0회는 아래 측정 구간에 대한 결과이며 모든 throwing 경로의 무할당을 뜻하지 않는다.
- RenderGraph의 컴파일 scratch와 제출 scratch는 계속 별도로 유지한다. CPU 작업 join, 그래프 reset 허용 조건과 GPU retirement는 상위 렌더링 계층의 책임이다.

선형 할당·checkpoint 복구·고정 풀의 개별 생성/반환은 관리 데이터에 대해 O(1)이다. 객체 생성·소멸 비용은 `T`에 달려 있으며, fixed pool 초기화와 전체 clear는 O(Capacity)다. 큰 풀은 매 호출 지역 객체보다 시스템이 유지하는 저장소에 두도록 사용 가이드에 명시했다.

## 검증 결과

GCC 13.3, C++23 Linux 환경에서 다음 검증을 수행했다. Utility fixture는 실제 vendored mimalloc을 링크하고 GNU linker wrapping으로 공개 할당 진입점을 관찰한다. allocator 대체 shim을 사용하지 않는다. source snapshot에 RenderEngine 디렉터리 자체가 없고 컴파일 의존성에도 Renderer가 없음을 검사한다.

| 검증 | 결과 | 근거 |
|---|---|---|
| Utility 단독 일반 빌드 | 43,026개 검사, lifetime 거절 subprocess 2개 통과 | [일반 결과](utility-allocators-20261022/utility-normal.json) |
| Utility 단독 ASan/UBSan | 동일 43,026개 검사와 subprocess 2개 통과 | [sanitizer 결과](utility-allocators-20261022/utility-sanitized.json) |
| 그래프 선언·컴파일·큐 할당 | 10개 예산 통과, 3회 × 1,024회 측정 | [그래프 할당 결과](utility-allocators-20261022/graph-allocations.json) |
| 프로덕션 CPU Graph ASan/UBSan | binding·ownership·range hazard/barrier·shared read·alias ordering 통과 | [그래프 correctness](utility-allocators-20261022/graph-sanitized.json) |
| RHI 이전/현재 비교 | 각각 34,399개 검사, 1,759개 시나리오 통과; 기존 command probe도 통과 | [RHI 비교](utility-allocators-20261022/rhi-comparison.json) |
| 현재 RHI ASan/UBSan | 34,399개 검사와 기존 command probe 통과 | [RHI sanitizer](utility-allocators-20261022/rhi-sanitized.json) |
| Owned queue ASan/UBSan | 5,474개 검사 통과 | [큐 sanitizer](utility-allocators-20261022/queue-sanitized.json) |
| Vulkan CPU 경계 ASan/UBSan | 524개 검사, 실제 scheduler worker 8개, warm binding 할당 0회 | [Vulkan sanitizer](utility-allocators-20261022/vulkan-sanitized.json) |
| 프로젝트와 최종 소스 | 단일 mimalloc provider, XML/AST/whitespace 검사, 테스트 입력 해시 대조 통과 | [최종 검증 요약](utility-allocators-20261022/verification-summary.json) |

Utility 검사는 over-alignment, `SIZE_MAX`, 잘못된 정렬, 빈 arena, 정확한 용량 소진, 외부 버퍼 canary, reset 후 블록 재사용, 역순 파괴, 생성·배열 복사 실패 정리, nested STL 전파, 다른 arena로 이동, arena보다 오래 사는 복사본을 포함한다. 새 모델에는 checkpoint 순서·소유자·재사용 거절, 예외에 의한 scope 복구, capacity 1/65 및 bitmap 경계, const 멤버 객체의 반복 union 수명 전환을 포함한다.

### 반복 할당 결과

Utility fixture의 다음 7개 구간을 각각 128회 실행했다. retained 모델은 warmup 이후, bounded 모델은 준비된 외부 버퍼/고정 슬롯에서 측정했다.

| 구간 | C++ `new` | mimalloc 공개 API 요청 |
|---|---:|---:|
| Retained raw arena | 0 | 0 |
| Object arena | 0 | 0 |
| Trivial arena buffer | 0 | 0 |
| Nested STL arena containers | 0 | 0 |
| Bounded linear arena | 0 | 0 |
| Nested stack arena | 0 | 0 |
| Fixed pool 재사용 | 0 | 0 |

렌더러의 기존 예산도 유지했다. 일반·subresource·alias 컴파일과 두 큐 계획은 warm 반복당 `new=0, mi=0`이다. 그래프 제출은 native mock 내부 할당을 제외하고 필수 completion lease `new=1`(56바이트)을 유지한다. 비동기 RHI 제출은 target 1개에서 `new=1, mi=0`, target 8개에서 `new=5, mi=0`으로 이전/현재가 같다. 별도의 recording-only 8-target 구간은 이전/현재 모두 `new=0, mi=9`다. 전체 렌더러가 무할당이라는 의미는 아니다.

## 측정 한계와 진단

Windows Editor/MSVC 전체 프로젝트 빌드와 native GPU 실행은 이 환경에서 수행하지 않았다. 프로젝트 XML과 compile provider 구성을 검사하고, 실제 CPU allocator/Graph/RHI 구현은 portable fixture로 컴파일·실행했다. 그래프와 RHI 원본에는 시간 값도 있지만, 이번 작업은 동시 검증 작업이 있는 환경에서 할당 예산을 확인한 것이므로 새로운 latency 개선의 근거로 사용하지 않는다.

LeakSanitizer는 프로세스 inspection 제한으로 비활성화했다. 초기 기본 설정의 Graph 실행은 `/proc/.../task` 접근 실패로 LeakSanitizer가 종료했으며, `detect_leaks=0`으로 ASan/UBSan을 유지한 재실행은 통과했다. Utility 일반 빌드의 vendored mimalloc GCC `stringop-overflow` 진단과 sanitizer 실행의 OS 정렬 할당 fallback 경고는 원본 결과에 보존했다. ASan/UBSan 오류는 없었다.

기존 SSGI mirror의 추가 smoke는 같은 `c3dd735` SSGI/Graph 구현끼리 비교했다. 컴파일·링크·실행은 성공했지만 마지막의 strict allocation-improvement gate는 개선이 없어서 실패했다. 이번 공통화의 acceptance gate로 계산하지 않았으며, 기존 성능 기준을 낮추지 않았다.

## 재현

저장소 루트에서 실행한다. 새 경로 또는 빈 경로를 build 디렉터리로 사용한다. 공개 baseline `d2a705f`는 실제 측정에 쓴 로컬 `c3dd735`와 트리가 같다.

```sh
UTILITY_BASELINE=d2a705ff3b03d9936484b4cac94e8e710f9f4ab5
UTILITY_ENKITS=/path/to/enkiTS
UTILITY_VULKAN_HEADERS=/path/to/Vulkan-Headers

python3 Tools/regression/verify-utility-allocators.py --output /tmp/utility-normal.json
python3 Tools/regression/verify-utility-allocators.py --sanitize --output /tmp/utility-sanitized.json

python3 Tools/regression/verify-render-graph-allocations.py --repetitions 3 --iterations 1024 \
  > /tmp/utility-graph-allocations.json
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-render-graph-program.py --sanitize

python3 Tools/regression/verify-rhi-software-portable.py --enkits "$UTILITY_ENKITS" \
  --baseline-ref "$UTILITY_BASELINE" --output /tmp/utility-rhi-comparison.json
python3 Tools/regression/verify-rhi-software-portable.py --enkits "$UTILITY_ENKITS" \
  --sanitize --output /tmp/utility-rhi-sanitized.json
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-rhi-software-queue.py \
  --enkits "$UTILITY_ENKITS" --sanitize --output /tmp/utility-queue-sanitized.json
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-vulkan-software-commands.py \
  --enkits "$UTILITY_ENKITS" --vulkan-headers "$UTILITY_VULKAN_HEADERS" \
  --sanitize --output /tmp/utility-vulkan-sanitized.json
```

테스트 의존성은 enkiTS v1.12 (`0289cf6ffce86697e1f46cb4eeca1c946bffec71`)와 Vulkan-Headers v1.4.328을 사용했다. runner는 의존성을 다운로드하지 않는다.

## 모델 선택 참고

중첩 메모리 stack을 marker로 되돌리는 모델은 Epic의 [FMemMark](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FMemMark) 설명과 같은 수명 패턴에 해당한다. 동일 크기 슬롯의 free-list 모델은 Boost의 [Simple Segregated Storage](https://www.boost.org/doc/libs/latest/libs/pool/doc/html/boost_pool/pool/pooling.html)에 설명된 용도에 해당한다. 이번 구현은 CreatorEngine의 bounded storage와 명시적인 수명 계약에 맞게 작성했으며 외부 구현 코드를 가져오지 않았다.
