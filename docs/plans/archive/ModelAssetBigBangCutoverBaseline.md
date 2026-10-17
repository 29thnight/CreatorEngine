# 보관: 모델 cutover 기준선의 비교 단위

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 기준선은 다시 쓰는 현재 수치가 아니다

MBC0는 cutover 이전 corpus·identity·코드 표면과 성능을 고정하려는 역사적 기준선이다. 기존 mbc0_corpus_baseline.json과 mbc_cutover_freeze.baseline.tsv는 당시 입력과 조사 조건을 가진 자료다. 이 문서 재작성에서 그 값이나 측정 파일을 변경하지 않았다.

옛 source symbol 접촉 수, 모델 수, GUID 분류를 현재 master의 값으로 재사용하지 않는다. 비교하려면 같은 corpus와 설정을 새 run에서 측정하고 revision·tool fingerprint·실제 결과를 따로 남겨야 한다. 과거 legacy 경로를 되살려 live A/B 스위치를 추가하는 것은 현재 계약이 아니다.

## 현재 비교해야 할 identity

AuthorModelAsset은 source decode→stable key 결합→UUIDv8 closure→schema-v2 sidecar→cooked generation 검증→게시를 한 transaction으로 수행한다. generation directory를 먼저 게시하고 canonical sidecar가 마지막 commit record가 된다. failure 시 이전 canonical 상태를 보존하는 것이 핵심이다.

runtime 소비는 logical ModelId만 비교하지 않고 generation과 typed child descriptor를 함께 확인한다. old generation을 이미 잡은 consumer가 새 resolver의 자식과 섞이지 않아야 한다. “같은 이름의 모델을 그렸다”는 검사는 cutover 의미 보존 증거로 부족하다.

## 유효한 후속 gate

새 artifact 형식의 strict readback, source/sidecar 손상 시 무변경, duplicate stable key와 잘못된 identity 거부, stale consumer, scene 저장·재로드의 exact reference를 확인한다. 품질·latency·memory 비교는 별도 동등 입력 실험이다. 현재 작업은 이러한 코드 계약만 조사했으며 MBC 예산을 다시 판정하지 않았다.

## 구현 근거

- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h)
- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.cpp)
- [Engine/RenderEngine/Assets/ModelSidecarV2.h](../../../Engine/RenderEngine/Assets/ModelSidecarV2.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h](../../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h)
