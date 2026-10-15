# 스켈레톤·클립 decode 저장소 공유

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 논리 자산과 raw 저장소

`ModelSkeletonStorage`와 `ModelAnimationStorage`는 전체 artifact decode 결과의 불변 소유자다. `ModelSkeletonPayload`/`ModelAnimationPayload`는 각 논리 자산의 origin을 보존하면서 같은 storage를 참조할 수 있다. raw adapter의 skeletonId/animationId는 nil이며 실제 논리 ID는 `origin.entry.asset`에서 읽는다.

clip storage는 track 배열과 그 배열을 가리키는 track table을 함께 소유한다. 비복사·비이동 구조라 table의 borrowed track 주소가 wrapper 이동 때문에 끊기지 않는다. 논리 wrapper의 storage owner도 const이며 재배치할 수 없다. animation payload는 자신의 정확한 hard skeleton payload를 추가로 pin한다.

## 공유 키와 재바인딩 검증

DataSystem의 ModelAssetRuntimeState가 logical/raw cache와 accepted work를 함께 관리한다. raw compatibility key는 content SHA-256, encoded size, kind, representation, schema, platform, ABI, decoder recipe를 포함한다. source locator는 content compatibility에서 제외하지만 verified byteSource/artifactPath 쌍은 storage 안에 같이 보존한다.

skeleton은 identity-free raw decode를 공유할 수 있다. clip bytes는 필요한 skeleton AssetId, ordered bone layout digest, bone count를 포함하므로 keyframe 일부가 같다는 이유만으로 공유하지 않는다. raw cache hit에서도 `BindModelAnimation`은 해당 logical wrapper의 declared hard edge, revision, skeleton identity/count/layout을 다시 검증한다.

alias의 origin을 다른 alias의 source/path로 바꾸지 않는다. 옛 model descriptor를 pin한 소비자는 current resolver를 다시 조회해 최신 데이터로 바뀌는 대신 자신이 캡처한 source snapshot으로 재수화한다.

## 요청·취소·퇴역

logical work는 logical ID/blob/revision/exactness를 구분한다. raw work는 exact-compatible decode를 함께 수행하는 single-flight다. 한 logical consumer의 취소·invalidation이 다른 wrapper의 성공 결과를 바꾸거나 공유 raw 작업을 취소하는 계약이 아니다.

각 logical job은 고정된 raw/skeleton dependency token을 가진다. worker 안에서 다른 worker를 기다리는 방식으로 dependency를 대체하지 않는다. shutdown은 accepted work를 drain하고 raw cache도 기존 staged retirement transaction에 포함한다. eviction은 잠금 아래 owner destructor를 실행하지 않도록 retirement vector에 옮겨 잠금 밖에서 놓는다.

## 예산 의미

logical descriptor/skeleton/clip 기본 retention은 4/16/64MiB다. raw skeleton/clip retention은 추가로 16/64MiB이며 `SetModelAnimationStorageCacheBudgets`가 제어한다. 이는 전체 physical memory hard cap이 아니다. logical ByteSize는 storage와 hard-skeleton closure를 보수적으로 포함하므로 공유 charge가 겹칠 수 있다.

두 종류 예산을 0으로 바꿔도 consumer/frame/accepted-work가 소유한 payload는 유효하다. skinned mesh cache가 skeleton을 pin할 수도 있다. logical eviction을 즉시 메모리 해제량으로 보고하지 않는다.

## 진단 해석

storageLiveBytes는 weak cache entry에서 아직 owner를 얻을 수 있는 storage를 한 번씩 센다. retained/budget는 cache의 강한 보존 영역만 뜻한다. inputStagingBytes는 소유 encoded buffer capacity, decodeStagingChargeBytes는 노출된 중간/후보 저장소의 보수적 charge다. codec 내부 임시 allocation·allocator overhead·GPU·source backing까지 측정하는 peak memory 계기는 아니다.

completedWorkResultPinBytes는 완료된 work가 붙든 unique raw result를 센다. live/retained와 겹칠 수 있어 세 항목을 더해 physical memory로 해석하면 안 된다. logical invalidation 뒤에도 accepted work가 drain될 때까지 staging/result pin 관측이 남을 수 있다.

## 검증 상태와 필요한 fixture

`model_animation_storage_probe.cpp`는 dedicated host용 source fixture다. initialized DataSystem과 dispatch를 중지/재개할 수 있는 scheduler에서 Begin/Poll, Replace/PollReplacement를 진행하도록 작성돼 있다. 이 문서 갱신에서는 빌드하거나 실행하지 않았다.

검증할 핵심은 다른 ID의 same-blob 공유와 distinct origin, incompatible hard skeleton 거부, canceled subscriber 격리, zero-budget external pin, stale request terminalization, old descriptor 재수화, failure injection 시 token의 정확히 한 번 terminalization이다. storage 주소가 같다는 것만으로 수명·취소·회계 계약을 전부 입증할 수 없다.

## 구현 근거

- [Engine/RenderEngine/Assets/ModelSkeletonPayload.h](../../Engine/RenderEngine/Assets/ModelSkeletonPayload.h)
- [Engine/RenderEngine/Assets/ModelAnimationPayload.h](../../Engine/RenderEngine/Assets/ModelAnimationPayload.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp](../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.cpp)
- [Tools/regression/model_animation_storage_probe.cpp](../../Tools/regression/model_animation_storage_probe.cpp)
