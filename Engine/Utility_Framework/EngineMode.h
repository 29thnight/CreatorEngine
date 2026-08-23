#pragma once
#include <cassert>
#include <cstdint>

// 실행 모드 — 이 프로세스가 에디터인가 플레이어인가 (BuildPipelinePlan B0-1).
//
// ── 왜 컴파일 타임(BUILD_FLAG)이 아니라 런타임인가 ──
//
// 게임 빌드가 유니티식(선빌드 플레이어 + 복사)으로 바뀌면서 라이브러리는
// 한 벌만 빌드된다. 에디터와 플레이어가 같은 라이브러리 바이너리를
// 링크하므로, "게임용으로 다시 컴파일할 때 갈린다"던 행동 분기(에셋 경로 ·
// pak 언팩 · 자동 시작 · 보더리스 창 · 태그 읽기 전용)는 실행 파일의
// 진입점이 정하는 런타임 값이 되어야 한다. BUILD_FLAG의 나머지 절반
// (에디터 전용 코드의 절단)은 매크로가 아니라 물리 분리(L2')가 맡는다.
//
// ── 규약 ──
//
// · 진입점(wWinMain → EngineBootstrap::Run)이 부팅 첫 줄에서 한 번 정한다.
// · 이후 불변 — 다른 값으로 다시 Set하면 assert가 잡는다.
// · 정해지기 전의 Get도 오류다. PathFinder::Initialize가 모드에 따라
//   에셋 경로를 갈리므로, 미정 상태의 경로 계산은 전부 오답이 될 수 있다.
enum class EngineRunMode : uint8_t
{
	Unset = 0,
	Editor,
	Player,
};

class EngineMode final
{
public:
	static void Set(EngineRunMode mode)
	{
		assert(EngineRunMode::Unset != mode && "EngineMode: Unset으로 되돌릴 수 없다");
		assert((EngineRunMode::Unset == s_mode || mode == s_mode) &&
			"EngineMode: 부팅 후 모드 변경은 금지다");
		s_mode = mode;
	}

	static EngineRunMode Get()
	{
		assert(EngineRunMode::Unset != s_mode &&
			"EngineMode: 진입점이 아직 모드를 정하지 않았다 (EngineBootstrap::Run)");
		return s_mode;
	}

	static bool IsEditor() { return EngineRunMode::Editor == Get(); }
	static bool IsPlayer() { return EngineRunMode::Player == Get(); }

private:
	EngineMode() = delete;

	static inline EngineRunMode s_mode = EngineRunMode::Unset;
};
