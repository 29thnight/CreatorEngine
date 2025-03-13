#include "QuadTree.h"

QuadTree::QuadTree()
{

}



QuadTree::QuadTree(Mathf::Vector4 _size, int _nodeSize)
{
	Root = new QuadNode(_size, _nodeSize);
}

QuadTree::~QuadTree()
{
	if(Root)
		delete Root;
}

void QuadTree::Bulid()
{

	if (Root)
	{
		Root->BulidChild(Root);
	}

}

