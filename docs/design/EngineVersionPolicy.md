# 버전·배포·API 식별 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 서로 다른 식별자

엔진 제품 표시는 `EngineVersion.json`에서 시작한다. 현재 값은 productName `CreatorEngine 2`, featureRelease 빈 문자열, version `0.0.0.0`, channel `preview`, localDevelopment `true`이다. 이 값은 정식 릴리스 번호가 배정되었다는 뜻이 아니다.

`EngineVersion.props`와 `Engine/Utility_Framework/EngineVersion.h`는 이 파일에서 생성되어 관리 코드와 native Windows version resource에 같은 빌드 번호를 공급한다. 버전 변경 시 `Tools/distribution/update-engine-version.ps1` 경로로 파생 파일을 함께 갱신한다. 일반 빌드가 소스 버전 번호를 자동 증가시키는 계약은 없다.

- productName/featureRelease: 사람이 읽는 제품 세대·기능 릴리스 표시
- version: 네 자리 엔진 빌드 번호
- buildId: 발행 시 새로 부여되는 배포 UUID
- payloadDigest: 실제 파일 목록·크기·hash에 연결된 payload 식별
- scriptApi: managed/native 호출 계약 버전
- contentAbiVersion/contentAbi: cooked 콘텐츠 생산자·소비자 계약. 현재 헤더는 1 / `creator-content-v1`
- channel: preview/stable 분류이며 exact build 선택을 대신하지 않는다

## 발행과 사용 시 검증

`EnginePublisher.Publish`는 Editor, Player, AssetCooker, AssetPacker host record의 version·configuration·shipping·script API·content ABI와 파일 hash를 대조한다. ScriptCore 소스의 기대 API만 비교하는 데 그치지 않고 복사된 DLL의 metadata도 읽는다. ProfilerViewer는 별도 standalone deployment record를 검사한다.

`EngineDistribution.Load`는 manifest와 `engine.info`를 함께 검증한다. `AssertProject`는 설정별 lock의 version/buildId/configuration이 선택 배포와 맞아야 통과한다. 같은 `0.0.0.0` 개발 배포 두 개도 buildId가 다르면 같은 엔진으로 처리하지 않는다.

`CurrentEngineDistributionIdentity()`는 runtime 근처 metadata를 찾아 컴파일된 제품/버전과 맞는 경우 표시용 channel/buildId/digest를 읽는다. 읽기 실패나 불일치 때 기본 `preview`/`unpublished`로 남는다. 이 표시용 fallback을 패키징 무결성 검증의 대체물로 사용하지 않는다.

## 운영 규칙과 아직 없는 보장

빌드 번호의 Major/Minor/Build/Revision 부여와 기능 릴리스의 공개 명칭은 릴리스 결정이다. 번호 증가만으로 ABI 호환을 선언하지 않는다. cooked 포맷이 그대로 소비될 수 없게 바뀌면 content identity를 함께 변경해야 하며, native/managed bridge 변경에는 script API 검토가 필요하다.

서명, MSI ProductVersion/제품 식별자 매핑, channel 승격과 다운로드 catalog는 현재 배포 manifest 구현에서 자동으로 제공되지 않는다. 특히 설치 프로그램과 `*.creatorproject`는 [Launcher 계획](../plans/EngineDistributionAndLauncherPlan.md)의 미구현 제품 요구다. 기존 문서의 예시 릴리스 번호를 현재 발행 사실로 읽지 않는다.

## 구현 근거

- [EngineVersion.json](../../EngineVersion.json)
- [EngineVersion.props](../../EngineVersion.props)
- [Engine/Utility_Framework/EngineVersion.h](../../Engine/Utility_Framework/EngineVersion.h)
- [Engine/Utility_Framework/ContentAbi.h](../../Engine/Utility_Framework/ContentAbi.h)
- [Engine/Utility_Framework/EngineDistributionIdentity.h](../../Engine/Utility_Framework/EngineDistributionIdentity.h)
- [BuildTool/EnginePublisher.cs](../../BuildTool/EnginePublisher.cs)
- [BuildTool/EngineDistribution.cs](../../BuildTool/EngineDistribution.cs)
- [BuildTool/ContentCompatibility.cs](../../BuildTool/ContentCompatibility.cs)
- [Tools/distribution/update-engine-version.ps1](../../Tools/distribution/update-engine-version.ps1)
