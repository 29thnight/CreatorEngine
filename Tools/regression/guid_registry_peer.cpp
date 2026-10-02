// GUID 장부 프로브의 두 번째 번역 단위 — 장부를 **다른 TU 에서** 만진다.
//
// 장부가 헤더의 `static` 전역이던 시절에는 이 파일과 guid_registry_probe.cpp 가
// 각자 자기 표를 가졌다. 지금은 TypeTrait.cpp 의 표 하나를 함께 본다.
#include <cstddef>
#include <vector>

#include "TypeTrait.h"

namespace guid_probe
{
	void make_from_peer(std::size_t count, std::vector<HashedGuid>& out)
	{
		out.reserve(out.size() + count);
		for (std::size_t i = 0; i < count; ++i)
		{
			out.push_back(TypeTrait::GUIDCreator::MakeGUID());
		}
	}

	// 만들고 지우고 다시 넣고 지운다 — Object 생성·파괴와 되돌리기(GameObjectCommand)의
	// 모양이다. 발급과 겹쳐 돌면서 표를 계속 흔든다.
	void churn_from_peer(std::size_t count)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			const HashedGuid id = TypeTrait::GUIDCreator::MakeGUID();
			TypeTrait::GUIDCreator::EraseGUID(id);
			TypeTrait::GUIDCreator::InsertGUID(id);
			TypeTrait::GUIDCreator::EraseGUID(id);
		}
	}
}
