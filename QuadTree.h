#pragma once
#include <vector>
#include "Core.Mathf.h"

//height���� ������ ����Ʈ��?

class QuadNode
{
public:

	//  1 1 1 1
	//2 ^n +1 ũ�⸦ ���°� �����ϴٰ��� 16 + 1 x 16 + 1     128 + 1 x 128 + 1 ,
	//���� ���� ������ �Է�? ex  129,129
	// ������� �ּ�ũ�� �ֱ� �ش����Ϸ� ���� ���ҳ�
	QuadNode(Mathf::Vector4 _size, int _nodeSize)
	{
		nodeSize = _nodeSize;
		left = _size.x;
		right = _size.y;
		top = _size.z;
		bottom = _size.w;

		//��ǥ�� �� ����̾ȳ�
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
	//��ͷ� �ڽ� �ٸ����
	void BulidChild(QuadNode* _parent)
	{
		//�ּҳ�� ũ�Ⱑ �ɶ����� ����
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


	//�ش��尡 �ø��������� or LOD level �������ʿ� 
	int LODlevel;

};
class QuadTree
{
public:
	QuadTree();
	QuadTree(Mathf::Vector4 _size, int _nodeSize);
	~QuadTree();
	//��Ʈ������ ũ�⸦ �޾Ƽ� ��� ������  
	void Bulid();

	//�ֻ��� �θ�
	QuadNode* Root;

	//���� Ʈ���� �ѳ���� �ּһ�����
	int nodeSize;

};

