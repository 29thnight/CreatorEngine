#include "LightMap.h"

namespace LightMap {
    LightMap::Rect* LightMap::LightMap::FindBestFit(std::vector<Rect>& freeSpaces, int width, int height)
    {
		Rect* bestFit = nullptr;
		int bestArea = INT_MAX;

		for (auto& space : freeSpaces)
		{
			if (space.width >= width && space.height >= height)
			{
				int area = space.width * space.height;
				if (area < bestArea)
				{
					bestFit = &space;
					bestArea = area;
				}
			}
		}
		return bestFit;
    }

	void LightMap::SplitSpace(std::vector<Rect>& freeSpaces, Rect used, int width, int height)
	{
		freeSpaces.erase(std::remove_if(freeSpaces.begin(), freeSpaces.end(),
			[&](Rect& r) { return r.x == used.x && r.y == used.y; }), freeSpaces.end());

		// 새로운 남은 공간 추가 (수직 분할 + 수평 분할)
		freeSpaces.emplace_back(used.x + width, used.y, used.width - width, used.height);  // 오른쪽 공간
		freeSpaces.emplace_back(used.x, used.y + height, used.width, used.height - height); // 아래쪽 공간
	}

	void LightMap::GuillotinePacking(int containerSize, std::vector<Rect>& rects)
	{
		std::vector<Rect> freeSpaces = { Rect(0, 0, containerSize, containerSize) }; // 초기 공간

		for (auto& rect : rects) {
			Rect* bestFit = FindBestFit(freeSpaces, rect.width, rect.height);
			if (!bestFit) {
				std::cout << "Failed to place rectangle (" << rect.width << ", " << rect.height << ")\n";
				continue;
			}

			// 배치 성공 → 새로운 좌표 설정
			rect.x = bestFit->x;
			rect.y = bestFit->y;

			// 공간 분할 수행
			SplitSpace(freeSpaces, *bestFit, rect.width, rect.height);
		}
	}


}
