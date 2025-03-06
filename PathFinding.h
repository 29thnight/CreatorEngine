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


const int TILE_SIZE = 10; // 타일 크기 (픽셀)

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
	map[x][y] 값으로 층 정보 + 장애물 + 계단 정보를 포함
	양수(1 이상) : 장애물(벽)
	음수(-1 이하) : 층계(계단)
	0 : 일반 이동 가능 지역
	절댓값(abs(map[x][y]))을 사용하여 층 정보 가져오기
	계단(-10)이 있는 곳에서만 층 변경 가능
	경사로(계단) 이동 시 추가 비용 적용 (extra_cost = 2.0)

	==>
	층계를 5단계(0, 10, 20, 30, 40, 50)로 확장

	map[x][y] = 0 → 평지(0층)
	map[x][y] = 10 → 1층
	map[x][y] = 20 → 2층
	map[x][y] = 30 → 3층
	map[x][y] = 40 → 4층
	map[x][y] = 50 → 5층
	계단(층계를 연결하는 타일) 추가 (-10, -20, -30, -40, -50)

	map[x][y] = -10 → 0층 ↔ 1층 계단
	map[x][y] = -20 → 1층 ↔ 2층 계단
	map[x][y] = -30 → 2층 ↔ 3층 계단
	map[x][y] = -40 → 3층 ↔ 4층 계단
	map[x][y] = -50 → 4층 ↔ 5층 계단
	계단에서만 층을 이동할 수 있도록 A 알고리즘 수정*

	계단(map[nx][ny] < 0)인 경우,
	→ abs(map[nx][ny]) 값이 현재 층과 1단계 차이일 때만 이동 허용
	→ 예: 0층에서 -10(0↔1층 계단)으로 이동 가능, -20(1↔2층 계단)은 이동 불가
	높은 층 이동 시 추가 비용 적용

	평지 이동 비용: 1.0
	계단 이동 비용: 2.0
	높은 층(3층 이상)으로 이동할수록 추가 비용 증가 (+3.0, +4.0 등)
	*/
	
	std::vector<std::vector<int>>& _map;
	std::pair<int, int> _start,	_end;
	int _mapX, _mapZ;
	//휴리스틱 함수
	double heuristic(int x1, int y1, int x2, int y2) {
		return abs(x1 - x2) + abs(y1 - y2);
	}

	//A* 알고리즘
	std::vector<std::pair<int, int>> aStar3D() {

		/*for (const auto& dir : xmdirections) {
			XMVECTOR va = XMLoadSInt2(&start);
			XMVECTOR vb = XMLoadSInt2(&dir);
			XMVECTOR res=_mm_add_ps(va, vb);
		}*/


		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_list;
		std::unordered_map<int, std::unordered_map<int, Node*>> all_nodes;
		std::unordered_map<int, std::unordered_map<int, bool>> closed_set;

		//int start_elevation = abs(map[start.first][start.second]);//층 정보
		Node* startNode = new Node(_start.first, _start.second, 0, heuristic(_start.first, _start.second, _end.first, _end.second));
		open_list.push(*startNode);
		all_nodes[_start.first][_start.second] = startNode;

		while (!open_list.empty()) {
			Node current = open_list.top();
			open_list.pop();

			if (current.x == _end.first && current.z == _end.second) {  // 목표 도착
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

				//int next_elevation = abs(map[nx][ny]);  // 절댓값으로 층 정보 확인
				//double extra_cost = 0.0;

				 // **층 이동 체크 (계단에서만 가능)**
				//if (next_elevation != current.elevation) {
				//	if (map[nx][ny] < 0) {  // 계단이면 이동 가능
				//		int stair_level = abs(map[nx][ny]);
				//		if (abs(stair_level - current.elevation) == 10) {
				//			extra_cost = 2.0 + (stair_level / 10) * 0.5;  // 층이 높을수록 추가 비용 증가
				//		}
				//		else {
				//			continue;  // 계단이 있어도 한 단계 이상 차이나면 이동 불가
				//		}
				//	}
				//	else {
				//		continue;  // 계단이 없으면 이동 불가
				//	}
				//}

				//double g_new = current.g + ((dir.first == 0 || dir.second == 0) ? 1.0 : 1.4) + extra_cost; // 직선 1.0, 대각선 1.4 + 추가 비용

				double g_new = current.g + 1; // 직선 1.0, 대각선 1.4
				if (!all_nodes[nx][ny] || g_new < all_nodes[nx][ny]->g) {
					Node* neighbor = new Node(nx, ny, g_new, heuristic(nx, ny, _end.first, _end.second), all_nodes[current.x][current.z]); // <-- elvation 추가 필요
					open_list.push(*neighbor);
					all_nodes[nx][ny] = neighbor;
				}
			}
		}
		return {};  // 경로 없음
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

