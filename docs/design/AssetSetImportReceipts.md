# AssetSet import receipt와 정확한 재사용

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## receipt가 생략하는 작업

`build-asset-set`은 typed source definition으로 CEMF3를 만드는 독립 콘텐츠 빌드다. import receipt hit는 같은 source transaction의 import 결과를 재사용한다. 동일 blob의 CAS deduplication과는 다르며 `reusedImports/recookedImports`와 `reusedBlobs`를 별도로 센다. model의 선택 subasset 묶음은 하나의 transaction이다.

BuildTool은 검증된 engine payload digest와 현재 orchestration assembly hash를 이용해 SHA-256 toolFingerprint를 만든다. native builder는 fingerprint가 64자리 lowercase hash인지 확인한다. 임의 오래된 importer cache를 최신 도구 결과로 인정하지 않는다.

## key와 검증

`asset-set-import-receipt-v2` recipe에는 build version, tool fingerprint, platform/ABI, manifest version, texture encoding/import settings, selected typed identity·kind·representation·schema, dependency edge와 mandatory source inventory가 들어간다. capture된 각 입력의 canonical path, bytes, digest로 전체 input proof를 만든다. timestamp만으로 hit를 판단하지 않는다.

receipt는 `ImportReceipts/<recipeKey>/<receiptHash>.receipt`에 불변 기록으로 저장한다. 후보 수 256, receipt 64 MiB, input 4096 등 코드의 bounded parser 제한을 넘으면 실패한다. filename hash, 중복·탈출 경로, 필수 입력 누락, typed identity, effective texture recipe, CAS blob size/hash, trailing bytes를 검사한다. 손상 receipt는 무조건 성공으로 넘기는 cache hint가 아니다.

hit에서도 typed payload readback은 남는다. source 입력이 바뀌면 같은 recipe 아래 과거 receipt가 현재 입력의 증거가 되지 않는다. importer가 외부 dependency를 추가로 읽는 경우 그 파일도 capture inventory에 포함되어야 한다.

## 쓰기와 실패 수명

cache의 OS guard가 동시 빌드를 직렬화한다. 고유 `.incomplete` work directory와 candidate만 쓰고, 완성 record를 rename한다. 죽은 프로세스가 남긴 work directory는 다음 빌드의 hit·resume 대상으로 열거하지 않는다. output은 새 immutable path만 허용하며 cache blob을 독립 복사·재검증한다. manifest와 report 검증 후 output rename 및 backing enrollment를 완료한다.

## 검증할 회귀

같은 입력 두 번째 빌드, sidecar/settings/tool/외부 모델 dependency 변경, 일부 CAS 변조, malformed receipt, 중간 프로세스 종료, 다른 드라이브 output을 구분해서 검증한다. 현재 소스에서 방어 경로를 확인했지만 실제 hit 비율·속도 향상이나 Windows 실패 주입 통과를 측정하지 않았다.

## 구현 근거

- [BuildTool/AssetSetBuilding.cs](../../BuildTool/AssetSetBuilding.cs)
- [Tools/AssetCooker/AssetSetBuild.h](../../Tools/AssetCooker/AssetSetBuild.h)
- [Tools/AssetCooker/AssetSetBuild.cpp](../../Tools/AssetCooker/AssetSetBuild.cpp)
