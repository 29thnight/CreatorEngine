#pragma once
#include <vector>
#include "Core.Mathf.h"

//height맵의 지형용 쿼드트리?

class QuadNode
{
public:

	//  1 1 1 1
	//2 ^n +1 크기를 쓰는게 유리하다고함 16 + 1 x 16 + 1     128 + 1 x 128 + 1 ,
	//가로 세로 순으로 입력? ex  129,129
	// 각노드의 최소크기 주기 해당이하로 가면 분할끝
	QuadNode(Mathf::Vector4 _size, int _nodeSize)
	{
		nodeSize = _nodeSize;
		left = _size.x;
		right = _size.y;
		top = _size.z;
		bottom = _size.w;

		//좌표계 잘 기억이안남
		center.x = (right + left) / 2;
		center.y = (bottom + top) / 2;



	}
	~QuadNode()
	{
		if (!children.empty())
		{
			for (QuadNode* child : children)
			{
				delete child;
			}
		}
	}
	//재귀로 자식 다만들기
	void BulidChild(QuadNode* _parent)
	{
		//최소노드 크기가 될때까지 분할
		while (_parent->right - _parent->left > nodeSize)
		{

			QuadNode* leftTopNode = new QuadNode({ _parent->left, (_parent->left + _parent->right) / 2, _parent->top, (_parent->top + _parent->bottom) / 2 }, nodeSize);
			QuadNode* rightTopNode = new QuadNode({ (_parent->left + _parent->right) / 2, _parent->right, _parent->top, (_parent->top + _parent->bottom) / 2 }, nodeSize);
			QuadNode* leftBottomNode = new QuadNode({ _parent->left, (_parent->left + _parent->right) / 2, (_parent->top + _parent->bottom) / 2, _parent->bottom }, nodeSize);
			QuadNode* rightBottomNode = new QuadNode({ (_parent->left + _parent->right) / 2, _parent->right, (_parent->top + _parent->bottom) / 2, _parent->bottom }, nodeSize);

			AddChild(leftTopNode);
			AddChild(rightTopNode);
			AddChild(leftBottomNode);
			AddChild(rightBottomNode);

			leftTopNode->BulidChild(leftTopNode);
			rightTopNode->BulidChild(rightTopNode);
			leftBottomNode->BulidChild(leftBottomNode);
			rightBottomNode->BulidChild(rightBottomNode);
		}

	}


	void AddChild(QuadNode* _child) { children.push_back(_child); }


	Mathf::Vector2 center = { 0,0 };

	float left = 0;
	float right = 0;
	float top = 0;
	float bottom = 0;
	std::vector<QuadNode*>children;
	int nodeSize = 0;


	//해당노드가 컬링범위인지 or LOD level 나누기필요 
	int LODlevel;

};
class QuadTree
{
public:
	QuadTree();
	QuadTree(Mathf::Vector4 _size, int _nodeSize);
	~QuadTree();
	//루트노드부터 크기를 받아서 계속 나눈다  
	void Bulid();

	//최상위 부모
	QuadNode* Root;

	//쿼드 트리안 한노드의 최소사이즈
	int nodeSize;

};

