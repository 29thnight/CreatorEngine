#pragma once
#include "Utility_Framework\Core.Definition.h"
#include "Utility_Framework\Core.Mathf.h"
#include <future> // std::async, std::future
#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <unordered_map>
#include <algorithm>


const int TILE_SIZE = 10; // Ÿ�� ũ�� (�ȼ�)

//XMINT2 start = { 0, 0 };
//XMINT2 end = { 9, 9 };
//
//const std::vector<XMINT2> xmdirections = {
//	{0, 1}, {0, -1}, {1, 0}, {-1, 0},
//	{1, 1}, {1, -1}, {-1, 1}, {-1, -1}
//};




//const std::vector<std::pair<int, int>> directions = {
//	{0, 1}, {0, -1}, {1, 0}, {-1, 0},
//	{1, 1}, {1, -1}, {-1, 1}, {-1, -1}
//};

const std::vector<std::pair<int, int>> directions = {
	{0, 1}, {0, -1}, {1, 0}, {-1, 0},
};



class PathFinding
{
private:
	struct Node {
		int x, z;
		double g, h, f;
		Node* parent;
		Node(int _x,int _z, double g_val,double h_val,Node* parent = nullptr) : x(_x),z(_z), g(g_val), h(h_val), f(g_val + h_val), parent(parent) {};
		bool operator>(const Node& other) const {
			return f > other.f;
		}
	};

	/*struct Node {
		int x, z, elevation;
		double g, h, f;
		Node* parent;

		Node(int _x, int _z, int _elevation, double _g, double _h, Node* _parent = nullptr)
			: x(_x), z(_z), elevation(_elevation), g(_g), h(_h), f(_g + _h), parent(_parent) {
		}

		bool operator>(const Node& other) const { return f > other.f; }
	};*/ 
	/*
	map[x][y] ������ �� ���� + ��ֹ� + ��� ������ ����
	���(1 �̻�) : ��ֹ�(��)
	����(-1 ����) : ����(���)
	0 : �Ϲ� �̵� ���� ����
	����(abs(map[x][y]))�� ����Ͽ� �� ���� ��������
	���(-10)�� �ִ� �������� �� ���� ����
	����(���) �̵� �� �߰� ��� ���� (extra_cost = 2.0)

	==>
	���踦 5�ܰ�(0, 10, 20, 30, 40, 50)�� Ȯ��

	map[x][y] = 0 �� ����(0��)
	map[x][y] = 10 �� 1��
	map[x][y] = 20 �� 2��
	map[x][y] = 30 �� 3��
	map[x][y] = 40 �� 4��
	map[x][y] = 50 �� 5��
	���(���踦 �����ϴ� Ÿ��) �߰� (-10, -20, -30, -40, -50)

	map[x][y] = -10 �� 0�� �� 1�� ���
	map[x][y] = -20 �� 1�� �� 2�� ���
	map[x][y] = -30 �� 2�� �� 3�� ���
	map[x][y] = -40 �� 3�� �� 4�� ���
	map[x][y] = -50 �� 4�� �� 5�� ���
	��ܿ����� ���� �̵��� �� �ֵ��� A �˰��� ����*

	���(map[nx][ny] < 0)�� ���,
	�� abs(map[nx][ny]) ���� ���� ���� 1�ܰ� ������ ���� �̵� ���
	�� ��: 0������ -10(0��1�� ���)���� �̵� ����, -20(1��2�� ���)�� �̵� �Ұ�
	���� �� �̵� �� �߰� ��� ����

	���� �̵� ���: 1.0
	��� �̵� ���: 2.0
	���� ��(3�� �̻�)���� �̵��Ҽ��� �߰� ��� ���� (+3.0, +4.0 ��)
	*/
	
	std::vector<std::vector<int>>& _map;
	std::pair<int, int> _start,	_end;
	int _mapX, _mapZ;
	//�޸���ƽ �Լ�
	double heuristic(int x1, int y1, int x2, int y2) {
		return abs(x1 - x2) + abs(y1 - y2);
	}

	//A* �˰���
	std::vector<std::pair<int, int>> aStar3D() {

		/*for (const auto& dir : xmdirections) {
			XMVECTOR va = XMLoadSInt2(&start);
			XMVECTOR vb = XMLoadSInt2(&dir);
			XMVECTOR res=_mm_add_ps(va, vb);
		}*/


		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_list;
		std::unordered_map<int, std::unordered_map<int, Node*>> all_nodes;
		std::unordered_map<int, std::unordered_map<int, bool>> closed_set;

		//int start_elevation = abs(map[start.first][start.second]);//�� ����
		Node* startNode = new Node(_start.first, _start.second, 0, heuristic(_start.first, _start.second, _end.first, _end.second));
		open_list.push(*startNode);
		all_nodes[_start.first][_start.second] = startNode;

		while (!open_list.empty()) {
			Node current = open_list.top();
			open_list.pop();

			if (current.x == _end.first && current.z == _end.second) {  // ��ǥ ����
				std::vector<std::pair<int, int>> path;
				Node* temp = &current;
				while (temp) {
					path.push_back({ temp->x, temp->z });
					temp = temp->parent;
				}
				std::reverse(path.begin(), path.end());
				return path;
			}

			closed_set[current.x][current.z] = true;

			for (const auto& dir : directions) {
				int nx = current.x + dir.first;
				int ny = current.z + dir.second;

				if (nx < 0 || nx >= _mapX || ny < 0 || ny >= _mapZ || _map[nx][ny] == 1 || closed_set[nx][ny]) {
					continue;
				}

				//int next_elevation = abs(map[nx][ny]);  // �������� �� ���� Ȯ��
				//double extra_cost = 0.0;

				 // **�� �̵� üũ (��ܿ����� ����)**
				//if (next_elevation != current.elevation) {
				//	if (map[nx][ny] < 0) {  // ����̸� �̵� ����
				//		int stair_level = abs(map[nx][ny]);
				//		if (abs(stair_level - current.elevation) == 10) {
				//			extra_cost = 2.0 + (stair_level / 10) * 0.5;  // ���� �������� �߰� ��� ����
				//		}
				//		else {
				//			continue;  // ����� �־ �� �ܰ� �̻� ���̳��� �̵� �Ұ�
				//		}
				//	}
				//	else {
				//		continue;  // ����� ������ �̵� �Ұ�
				//	}
				//}

				//double g_new = current.g + ((dir.first == 0 || dir.second == 0) ? 1.0 : 1.4) + extra_cost; // ���� 1.0, �밢�� 1.4 + �߰� ���

				double g_new = current.g + 1; // ���� 1.0, �밢�� 1.4
				if (!all_nodes[nx][ny] || g_new < all_nodes[nx][ny]->g) {
					Node* neighbor = new Node(nx, ny, g_new, heuristic(nx, ny, _end.first, _end.second), all_nodes[current.x][current.z]); // <-- elvation �߰� �ʿ�
					open_list.push(*neighbor);
					all_nodes[nx][ny] = neighbor;
				}
			}
		}
		return {};  // ��� ����
	}

public:
	PathFinding(std::vector<std::vector<int>>& map,int mapX,int mapZ, std::pair<int, int>pos, std::pair<int, int>targetpos)
		: _start(pos), _end(targetpos), _map(map), _mapX(mapX), _mapZ(mapZ){
	}


	void updateCurrentPos(std::pair<int, int>& pos) {
		_start = pos;
	}

	void updateTargetPos(std::pair<int, int>& target) {
		_end = target;
	}

	std::pair<int, int> update() {
		//async
        //std::future<std::vector<std::pair<int, int>>> futurePath = std::async(std::launch::async, &PathFinding::aStar3D, this);
		
		std::vector<std::pair<int, int>> path = aStar3D();

		//std::vector<std::pair<int, int>> path = futurePath.get();
		//
		if (path.size() > 1)
		{
			return path[1];
		}
		return _start;
	}
};

