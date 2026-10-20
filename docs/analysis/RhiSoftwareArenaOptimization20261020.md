# Software RHI 비-pmr 아레나·락 최적화

2026-10-20. 기준 HEAD `2935f29452698d08f4c0cf720c255ae383c3ca3f`와 기존 소프트웨어 RHI 작업 트리에서 전후 비교했다. PR에는 명령 기록·네이티브 번역의 제품 배선과 이 후속 최적화를 함께 포함한다.

## 실행 결과

| 항목 (ms) | 변경 전 | 첫 시험 | 최종 청크 커서 |
|---|---:|---:|---:|
| Game CPU 중앙값 | 14.1133 | 15.1127 | 13.9197 |
| Game CPU p95 | 17.4203 | 18.0442 | 16.2151 |
| RenderCommandRecord 평균 | 2.1711 | 2.4990 | 2.1115 |
| RHISoftwareTranslate 평균 | 1.8541 | 2.0145 | 1.8589 |
| RenderGraphDeclare 평균 | 5.5251 | 5.6663 | 5.3108 |
| MaterialGraphScenePrepare 평균 | 2.8875 | 3.0362 | 2.8141 |
| RenderGraphCompile 평균 | 0.8174 | 0.8386 | 0.8179 |

Game CPU 중앙값은 1.37% 감소했다. 기록 OFF의 Scene+Game 완료 뷰 처리량은 51.62→53.26뷰/초(3.18% 증가)다. RHI 번역 평균은 1.8541→1.8589ms로 개선되지 않았다. **120 FPS 목표는 미달이다.** 최종 Game CPU 중앙값 13.9197ms는 8.3333ms 예산보다 5.5864ms 길다.

첫 시험에서 명령별 범용 아레나 호출과 통계 구조체 복사 경로가 들어가 Game CPU가 7.08% 증가했다. 이 시험은 `after/`에 보존했다. 최종판은 청크 내부 직접 커서와 청크 활성화 때의 통계 갱신을 사용한다.

후속 비용에는 `RenderGraphDeclare` 평균 5.3108ms와 그 안의 준비 경로가 있다. 표의 마커들은 중첩·다른 스레드 구간이므로 서로 더하지 않는다.

[계측·검증 JSON](RhiSoftwareArenaOptimization20261020.json)

두 실행은 Debug DX12 CreatorRobot, worker 8개, 같은 프로젝트·Scene/Game 뷰·카메라·draw 입력·해상도·foreground 조건, `CREATOR_RHI_SOFTWARE_COMMANDS=1`, `CREATOR_BINDING_DETAIL_SCOPES=0`이다. 각 15초의 native profiling ON/OFF 구간을 기록했다. 이번 전후 실행은 ETW를 켜지 않았다. 기존 ETW 병목 근거는 `Artifacts/render-arena-20261020/after-final/stack-summary.txt`와 `lockit-wait-summary.txt`에 있다. 전후 수치는 이번 native capture에서 직접 계산했다.

원본·실행 로그·바이너리 식별·픽셀 검산은 로컬 `Artifacts/rhi-lockfree-20261020/`에 보관한다. `before/identity.json`, `after-fast/identity.json`은 실행 DLL/PDB의 SHA-256을, `source-identity.json`은 최종 소스를 기록한다. 변경 전 DLL은 기존 작업 트리의 빌드이며 변경 후 DLL은 이 PR의 최종 Debug 빌드다.

## 변경한 실행 경로

1. `EnhancedRenderGraph::RecordSoftware`가 worker target별 명령 목록을 기록한다. 값·span payload는 target의 단일 writer 아레나에 복사한다. 밀봉한 batch를 bounded FIFO에 넣고 RHI thread가 번역·제출한다. GPU 완료 수명은 별도 completion fence와 owner token으로 보존한다.
2. `render_arena_resource`는 pmr를 사용하지 않는 단조 증가 아레나다. backing block은 vendored mimalloc v3.5.3에서 얻는다. 정렬된 offset 할당에는 mutex·atomic·가상 호출이 없다. 64KiB 이상 block 주소는 안정적이고 마지막 owner 뒤 reset으로 재사용한다.
3. `render_arena_allocator<T>`는 중첩 vector와 map value에 같은 writer domain을 전달한다. allocator를 명시하지 않은 일반 복사는 독립 mimalloc 저장소로 만들어 원본 프레임이 끝난 뒤에도 유지된다. 서로 다른 domain의 move assignment는 목적지 저장소를 지킨다.
4. `RenderArenaPool`의 free set은 원자 포인터 슬롯 8개다. 반환자가 독점 상태에서 reset한 뒤 release CAS로 게시한다. 획득자의 acquire exchange가 포인터 소유권과 초기화 결과를 가져간다. intrusive next link를 읽지 않으므로 제거·재게시가 ABA 링크로 이어지지 않는다. 캐시의 backing capacity는 16MiB로 제한하고 넘친 set은 즉시 해제한다. 원자 포인터와 size_t가 lock-free인지 compile-time assertion으로 검사한다.
5. 소프트웨어 명령 블록도 같은 비-pmr 풀을 사용한다. 명령별 할당은 현재 청크의 정렬 계산·커서 증가만 수행한다. 풀 접근과 backing 통계 갱신은 64KiB 이상 청크를 활성화할 때만 수행한다. 범용 아레나 호출과 전체 통계 복사를 명령마다 반복하지 않는다. 마지막 번역·취소 소비자가 반환한 뒤 다음 기록에서 재사용한다. 다른 batch의 live block을 빌리지 않는다. 명령 헤더의 payload 포인터를 1바이트 offset으로 치환해 헤더를 32→24바이트, Draw를 48→40바이트로 줄였다. 명령·payload의 실제 정렬 중 큰 쪽을 사용해 Draw를 저장소에서도 40바이트 간격으로 배치한다.
6. `DX12GpuProfiler::MergeSlices`는 매 프레임 unordered_map node를 만들고 지우던 경로를 연속 open-addressing 버킷으로 바꿨다. 버킷·병합 scratch·출력 문자열은 재사용한다. 빈 버킷은 0, 값은 scratch index+1이다. 충돌은 선형 탐사하며 최대 load factor는 0.5다. 이름은 호출 중에만 빌리고 성공·예외 반환 모두 scope guard가 제거한다. busy 구간 정렬과 slice 순회는 span으로 Debug STL iterator 등록 락을 없앴다.

Draw·RDG의 지역 scratch 컨테이너는 프레임 lease의 아레나를 사용한다. 새 코드에서 명령마다 지역 heap 컨테이너를 만들지 않는다. 캐시 warm-up 뒤 raw backing block을 재사용하고, 컨테이너의 비자명 소멸자를 먼저 실행한 뒤 raw storage를 reset한다. pool 교환의 lock-free 보장과 cold allocation·shared_ptr 수명 관리의 비용은 각각 다른 동작이다.

RHI FIFO의 owner 등록·실패·장치 수명 전이와 condition-variable 알림은 기존 동기화를 유지한다. 티켓의 ready polling은 acquire atomic load를 사용한다. 프레임 수명 경계를 단일 producer FIFO라고 가정해 임의로 SPSC 큐로 바꾸지 않았다.

## 검증

- 최종 Debug 제품 빌드 통과. 기존 SceneRuntime 생성 소스 중복 경고 `MSB8027`는 작업 시작 전에도 있던 프로젝트 변경에서 발생한다.
- CPU probe 통과: 39개 encoder 메서드 coverage, payload·예산·single-use·오류, nested allocator·map value, 독립 복사/서로 다른 domain move, 16,000회 동시 pool 재사용, 고정 주소·4096 정렬, 비동기 owner·초과 cache·예외 정리, 청크 이동·40바이트 실제 간격과 블록 재사용.
- 최종 바이너리 DX12 GPU validation 통과: 전체 RenderGraph 검사, native 번역 clear/barrier/copy, 8×8 RGBA 256채널 리드백 일치, stale recording 거절, descriptor/upload 고정, async queue, translation pool 재사용, GPU 은퇴 뒤 arena 반환. validation errors 0.
- GPU timing 병합의 128조각/32이름, 첫 등장 순서·합산/span·suffix·1,000회 저장소 재사용·입력 문자열 해제·빈/0길이 입력 검사 통과.
- CreatorRobot 전후 정상 종료·입력 replay·씬 교체 후 replay 통과. depth/baseColor/normal/metalRough/emissive/sceneHdr 출력의 다른 float 값 수 0. 캡처 complete, unacked/dropped/invalid provenance 0, GPU 이벤트 전부 해당 측정에 대응.
- 재질 packet plan 전용 검사 통과: hits=1, builds=91, multilevel=3, retained owner 보존, invalidation=4. 재질 코드 생성 22사례·15,434검사·101컴파일·31거절·3,960 GPU 검사와 metadata JSON 검증 통과.
- clang-format 19.1.5 검사와 Git whitespace 검사 통과. Renderer 구현의 pmr 사용 0건. mimalloc 원본 파일 hash 전부 일치. vendor 전용 `.gitattributes`로 upstream 개행·공백을 그대로 보존한다.
- 무관한 편집기·프로파일링·문서 변경 10개는 시작 patch와 동일하며 PR에서 제외한다.

재질 전체 표면 suite는 범위를 패킷 계획 전용 검사로 전환하면서 중지했다. 전체 재질 표면 suite의 통과로 집계하지 않는다.

재실행 명령:

```powershell
pwsh -NoProfile -File Tools/regression/verify-rhi-software-commands.ps1
pwsh -NoProfile -File Tools/regression/verify-rhi-software-native.ps1 -OutputDirectory <새 디렉터리>
```

`verify-rhi-software-commands.ps1`은 MSVC `/Od /MDd /W4 /WX`로 probe를 빌드한다. mimalloc C 소스는 기존 프로젝트와 같이 `/O2`로 빌드한다. 엔진 Debug C++의 최적화 수준이나 iterator debug 설정을 낮추지 않았다.

제품 번역은 DX12 graphics queue의 serial translation이다. Vulkan adapter 타입은 있지만 Vulkan 제품 경로 전환과 병렬 번역은 구현 범위에 포함하지 않았다. 소프트웨어 기록은 기존과 같이 명시적 환경 변수로 켠다.

## 참고 근거

[CPU Performance Engineering](https://github.com/usamahz/cpu-performance-engineering)의 측정·동시성·allocator 지침을 확인했다. 현재 연결된 MCP 도구 목록에는 해당 서버가 없어 원본 자료를 사용했다. 저장된 ETW의 `std::_Lockit`, iterator orphan/adopt, heap 할당 스택과 현재 RHI 소스를 연결해 변경 대상을 골랐다. 전역 Debug 검사 해제 대신 수명별 재사용과 연속 저장소로 해당 호출을 줄였다.
