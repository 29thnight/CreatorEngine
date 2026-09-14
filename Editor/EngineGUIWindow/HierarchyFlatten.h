#pragma once

// PHASE 21 W7-2 — Hierarchy 의 평탄 표시 목록.
//
// 계획서 §8.3 의 순서 제약이 이것이다: *"flatten cache가 clipping의 선행조건이다.
// Hierarchy는 `TreeNodeEx` 재귀로 그려지고 자식은 `GetChildrenIndices()`로 내려간다.
// clipper는 '인덱스로 접근 가능한 평탄 목록'을 요구하므로, `EntityHandle + depth +
// expanded`의 파생 목록을 먼저 만들지 않으면 clipper를 끼울 자리 자체가 없다."*
//
// 실측이 그 자리를 숫자로 세웠다(docs/analysis/EditorPanelCostBaselineW7.md):
// 엔티티 50,000 에서 그린 행이 **정확히 50,000** 이고 p95 33.3 ms 였다. 화면에는
// 50 줄쯤 보인다.
//
// ── 무엇을 담고 무엇을 담지 않는가 ────────────────────────────────────────
//
// 담는 것은 **파생값**뿐이다 — 슬롯 인덱스 · 깊이 · 자식 유무 · 접힘 · 띠 번호.
// parent / children / occupied 의 정본은 `HierarchyStore` 것이고 여기로 복제하지
// 않는다. 이름 · 아이콘 · 잠금 · 활성 · 선택은 아예 담지 않는다 — 그리는 순간에
// 정본에서 읽으므로 캐시가 낡을 여지가 없다.
//
// ── 다시 만들 근거 (계획서 §8.3) ──────────────────────────────────────────
//
// *"invalidation 근거가 없으면 매 frame 전체 cache를 믿지 말고 fail-safe rebuild
// 한다."* 목록의 모양을 바꾸는 입력은 넷이고, 근거를 가진 것은 셋뿐이다.
//
//  1. **계층** — `HierarchyStore::Revision()`. W7-2 에서 세웠다(그전에는 없었다).
//  2. **엔티티 수** — 슬롯 생성/파괴는 ①을 올리지만, 값이 싸므로 함께 본다.
//  3. **DDOL 수** — `Object::SetDontDestroyOnLoad` 는 Entity 의 플래그만 바꾸고
//     계층을 건드리지 않는다. ①이 못 잡는 축이라 O(1) 로 세어 함께 본다.
//  4. **이름** — 근거가 **없다.** 이름에는 revision 이 없다. 그래서 검색이 켜져
//     있는 동안에는 매 프레임 다시 만든다(fail-safe). 그래도 옛 경로보다 싸다 —
//     옛 `IsMatchedRecursive` 는 **행마다** 자기 서브트리를 통째로 다시 훑어
//     O(n·깊이) 였고, 여기서는 한 번의 순회로 끝난다.
//
// ── 접힘을 왜 이쪽이 소유하는가 ───────────────────────────────────────────
//
// 지금까지 접힘은 ImGui 가 `TreeNodeEx` 안에 들고 있었다. 그런데 clipper 를 끼우면
// 화면 밖 노드는 `TreeNodeEx` 가 **호출되지 않으므로** ImGui 는 그 노드가 열렸는지
// 말할 기회가 없다. "보이는 행만 그린다" 와 "열린 노드를 ImGui 가 기억한다" 는
// 함께 설 수 없다. 그래서 접힘을 이쪽이 갖고, 매 프레임 `SetNextItemOpen` 으로
// ImGui 에 알려 준 뒤 `IsItemToggledOpen()` 으로 되받는다.
//
// 기본값은 옛 동작 그대로다 — `parentIndex == 0` 인 것(씬 루트의 직계)만 처음부터
// 펼쳐져 있다. 옛 코드의 `ImGuiTreeNodeFlags_DefaultOpen` 이 같은 조건이었다.
// 그래서 담는 것은 **기본값에서 벗어난 것**뿐이고, 새로 생긴 노드는 자동으로
// 옛 기본값을 따른다.
//
// 펼침/접힘이 목록에 반영되는 것은 **다음 프레임**이다(사람이 누른 것은 그리는
// 도중에 알게 되므로). 60 fps 에서 16 ms 이고, 옛 경로는 같은 프레임에 반영됐다.
// 이 한 프레임을 받는 대신 clipping 이 설 자리를 얻는다.

#include "ImGui.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class Scene;

namespace editor
{
	enum class hierarchy_row_kind : std::uint8_t
	{
		entity = 0,
		scene_group,   ///< 씬 이름 머리. 합성 행이고 늘 펼쳐져 있다
		ddol_group,    ///< "[ Dont Destroy On Load ]" 머리. 합성 행이고 늘 펼쳐져 있다
	};

	struct hierarchy_flat_row
	{
		int index{};                 ///< 슬롯 인덱스(scene_group 은 0, ddol_group 은 -1)
		int depth{};                 ///< 들여쓰기 단계. ImGui 의 TreePush 깊이와 같다
		int bandIndex{};             ///< 홀짝 띠 번호. **entity 행만** 센다(옛 m_rowIndex)
		hierarchy_row_kind kind{};
		bool hasChildren{};
		bool expanded{};
	};

	/// 무효화 근거. 창이 모아서 넘긴다 — 이 모듈이 Scene 바깥(SceneManager)을
	/// 알지 않게 하려는 것이고, 무엇이 근거인지 호출부에서 한눈에 보이게 하려는
	/// 것이기도 하다.
	struct hierarchy_flat_key
	{
		std::uint64_t revision{};        ///< HierarchyStore::Revision()
		std::size_t entityCount{};
		std::size_t dontDestroyCount{};
		bool searchActive{};             ///< 켜져 있으면 매 프레임 재구축한다
	};

	struct hierarchy_flat_stats
	{
		std::uint64_t rebuilds{};    ///< 다시 만든 횟수(누계)
		std::uint64_t visited{};     ///< 마지막 재구축이 방문한 노드 수
		std::size_t   rows{};        ///< 지금 목록의 행 수(접힌 것은 없다)
	};

	/// Hierarchy 창이 하나 소유한다. 창이 하나뿐이라 전역으로 두지 않는다.
	class HierarchyFlatView
	{
	public:
		/// 보이는 행의 목록. 근거가 하나도 안 바뀌었으면 **다시 만들지 않는다.**
		const std::vector<hierarchy_flat_row>& Rows(
			Scene* scene, const hierarchy_flat_key& key, const ImGuiTextFilter& filter);

		/// 사람이 화살표를 눌렀다. 기본값에서 벗어난 집합을 뒤집는다.
		void Toggle(int index);

		/// 근거를 댈 수 없는 변화를 만났을 때. 다음 호출이 전량 재구축한다.
		/// (씬 포인터는 건드리지 않는다 — 그것이 바뀌면 접힘까지 버려야 한다.)
		void Invalidate() noexcept { m_built.revision = 0; }

		hierarchy_flat_stats Stats() const noexcept { return m_stats; }

	private:
		void Rebuild(Scene* scene, const ImGuiTextFilter& filter, bool searching);

		std::vector<hierarchy_flat_row> m_rows;
		std::vector<std::uint8_t> m_matched;   ///< 검색: 자신 또는 자손이 걸린다
		std::unordered_set<int> m_toggled;     ///< 기본값에서 **벗어난** 슬롯만 담는다
		hierarchy_flat_key m_built{};
		std::uint64_t m_toggleStamp{ 1 };
		std::uint64_t m_builtToggleStamp{};
		const Scene* m_builtScene{};
		hierarchy_flat_stats m_stats{};
	};
}
