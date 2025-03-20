#pragma once
#include "DeviceResources.h"

// Guillotine Algorithm
// ��ġ�� ��ġ�� ã�� ������ ����.

namespace LightMap {
	class LightMap
	{
	public:
		struct Rect
		{
			int x, y, width, height;
			Rect(int _x, int _y, int _width, int _height) : x(_x), y(_y), width(_width), height(_height) {}
		};

	public:
		Rect* FindBestFit(std::vector<Rect>& freeSpaces, int width, int height);
		void SplitSpace(std::vector<Rect>& freeSpaces, Rect used, int width, int height);
		void GuillotinePacking(int containerSize, std::vector<Rect>& rects);

	private:

	};
}
