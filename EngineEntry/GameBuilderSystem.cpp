#ifndef DYNAMICCPP_EXPORTS
#include "GameBuilderSystem.h"
#include "PakHelper.h"

// ★ MSBuild 호출이 사라졌다 (PHASE 12 B0-3).
//
//   예전에는 여기서 GameBuild.sln을 /t:Rebuild로 다시 컴파일했다 — 게임
//   빌드가 엔진을 게임용 구성으로 재컴파일하던 언리얼식 모델의 잔재다.
//   유니티식 전환(BuildPipelinePlan §2.0)으로 Player.exe는 에디터와 함께
//   이미 빌드돼 있고, 게임 빌드는 그 산출물을 복사하는 일이 됐다.
//   TrainAsis·GameBuild.sln과 함께 MSBuild 커맨드 조립·경로 탐색이 여기서
//   걷혔다. MSBuildHelper·vswhere 기계 자체의 은퇴는 B2의 몫.

void GameBuilderSystem::Initialize()
{
	m_isInitialized = true;
}

void GameBuilderSystem::Finalize()
{
}

void GameBuilderSystem::BuildGame()
{
	// B0 시점의 게임 빌드 = 에셋 pak 생성까지다. 스테이징(Player.exe ·
	// DLL · Managed · cso를 배포 폴더로 복사)과 검증은 B2(build.ps1)가
	// 이 자리를 이어받는다 — 에디터 버튼과 CLI가 같은 스크립트를 부르게.
	if (!PackageGameAssets())
	{
		Debug->LogWarning("Asset packaging step completed with warnings.");
		return;
	}

	Debug->LogDebug("게임 에셋 pak 생성 완료 — Player.exe 옆에 두면 기동 시 언팩된다"
		" (스테이징 자동화는 BuildPipelinePlan B2).");
}

bool GameBuilderSystem::PackageGameAssets()
{
	return ::PackageGameAssets();
}

bool GameBuilderSystem::UnpackageGameAssets()
{
	return ::UnpackageGameAssets();
}

#endif // !DYNAMICCPP_EXPORTS
