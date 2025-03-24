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

		// ���ο� ���� ���� �߰� (���� ���� + ���� ����)
		freeSpaces.emplace_back(used.x + width, used.y, used.width - width, used.height);  // ������ ����
		freeSpaces.emplace_back(used.x, used.y + height, used.width, used.height - height); // �Ʒ��� ����
	}

	void LightMap::GuillotinePacking(int containerSize, std::vector<Rect>& rects)
	{
		std::vector<Rect> freeSpaces = { Rect(0, 0, containerSize, containerSize) }; // �ʱ� ����

		for (auto& rect : rects) {
			Rect* bestFit = FindBestFit(freeSpaces, rect.width, rect.height);
			if (!bestFit) {
				std::cout << "Failed to place rectangle (" << rect.width << ", " << rect.height << ")\n";
				continue;
			}

			// ��ġ ���� �� ���ο� ��ǥ ����
			rect.x = bestFit->x;
			rect.y = bestFit->y;

			// ���� ���� ����
			SplitSpace(freeSpaces, *bestFit, rect.width, rect.height);
		}
	}


}
