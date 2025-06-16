#pragma once
#include <assert.h>

#define COMPUTE_SIDE(i, bit, p, mid, newMin, newMax) \
if (p >= mid)         \
{                     \
    i |= bit;         \
    newMin = mid;     \
}                     \
else                  \
{                     \
    newMax = mid;     \
}


template <class N>
class Octree
{
protected:
    struct Point
    {
        float x;
        float y;
        float z;
        Point(const Point& p2) : x(p2.x), y(p2.y), z(p2.z) {}
        Point& operator=(const Point& p2) { x = p2.x; y = p2.y; z = p2.z; return *this; }
        Point(float in_x, float in_y, float in_z) : x(in_x), y(in_y), z(in_z) {}
        Point(const float p2[3]) : x(p2[0]), y(p2[1]), z(p2[2]) {}
        operator float* () { return &x; }
        operator const float* () const { return &x; }
        Point operator+(const Point& p2) const { return Point(x + p2.x, y + p2.y, z + p2.z); }
        Point operator-(const Point& p2) const { return Point(x - p2.x, y - p2.y, z - p2.z); }
        Point operator*(float f) const { return Point(x * f, y * f, z * f); }
        bool operator< (const Point& p2) const { return x < p2.x && y < p2.y && z < p2.z; }
        bool operator>=(const Point& p2) const { return x >= p2.x && y >= p2.y && z >= p2.z; }
    };

    struct OctreeNode
    {
        N _nodeData;
        OctreeNode* _children[8];
        OctreeNode()
        {
            for (int i = 0; i < 8; i++)
                _children[i] = 0;
        }
        virtual ~OctreeNode()
        {
            for (int i = 0; i < 8; i++)
                if (_children[i])
                    delete _children[i];
        }
    };

    Point _min;
    Point _max;
    Point _cellSize;
    OctreeNode* _root;

public:
    Octree(float min[3], float max[3], float cellSize[3]) : _min(min), _max(max), _cellSize(cellSize), _root(0) {}
    virtual ~Octree() { delete _root; }

    class Callback
    {
    public:
        // Return value: true = continue; false = abort.
        virtual bool operator()(const float min[3], const float max[3], N& nodeData) = 0;
    };

    N& getCell(const float pos[3], Callback* callback = NULL)
    {
        Point ppos(pos);
        assert(ppos >= _min && ppos < _max);
        Point currMin(_min);
        Point currMax(_max);
        Point delta = _max - _min;
        if (!_root)
            _root = new OctreeNode();
        OctreeNode* currNode = _root;
        while (delta >= _cellSize)
        {
            bool shouldContinue = true;
            if (callback)
                shouldContinue = callback->operator()(currMin, currMax, currNode->_nodeData);
            if (!shouldContinue)
                break;
            Point mid = (delta * 0.5f) + currMin;
            Point newMin(currMin);
            Point newMax(currMax);
            int index = 0;
            COMPUTE_SIDE(index, 1, ppos.x, mid.x, newMin.x, newMax.x)
                COMPUTE_SIDE(index, 2, ppos.y, mid.y, newMin.y, newMax.y)
                COMPUTE_SIDE(index, 4, ppos.z, mid.z, newMin.z, newMax.z)
                if (!(currNode->_children[index]))
                    currNode->_children[index] = new OctreeNode();
            currNode = currNode->_children[index];
            currMin = newMin;
            currMax = newMax;
            delta = currMax - currMin;
        }
        return currNode->_nodeData;
    }

    void traverse(Callback* callback)
    {
        assert(callback);
        traverseRecursive(callback, _min, _max, _root);
    }

    void clear()
    {
        delete _root;
        _root = NULL;
    }

    class Iterator
    {
    public:
        Iterator getChild(int i)
        {
            return Iterator(_currNode->_children[i]);
        }
        N* getData()
        {
            if (_currNode)
                return &_currNode->_nodeData;
            else return NULL;
        }
    protected:
        OctreeNode* _currNode;
        Iterator(OctreeNode* node) : _currNode(node) {}
        friend class Octree;
    };

    Iterator getIterator()
    {
        return Iterator(_root);
    }

protected:
    void traverseRecursive(Callback* callback, const Point& currMin, const Point& currMax, OctreeNode* currNode)
    {
        if (!currNode)
            return;
        bool shouldContinue = callback->operator()(currMin, currMax, currNode->_nodeData);
        if (!shouldContinue)
            return;
        Point delta = currMax - currMin;
        Point mid = (delta * 0.5f) + currMin;
        traverseRecursive(callback, currMin, mid, currNode->_children[0]);
        traverseRecursive(callback, Point(mid.x, currMin.y, currMin.z),
            Point(currMax.x, mid.y, mid.z), currNode->_children[1]);
        traverseRecursive(callback, Point(currMin.x, mid.y, currMin.z),
            Point(mid.x, currMax.y, mid.z), currNode->_children[2]);
        traverseRecursive(callback, Point(mid.x, mid.y, currMin.z),
            Point(currMax.x, currMax.y, mid.z), currNode->_children[3]);
        traverseRecursive(callback, Point(currMin.x, currMin.y, mid.z),
            Point(mid.x, mid.y, currMax.z), currNode->_children[4]);
        traverseRecursive(callback, Point(mid.x, currMin.y, mid.z),
            Point(currMax.x, mid.y, currMax.z), currNode->_children[5]);
        traverseRecursive(callback, Point(currMin.x, mid.y, mid.z),
            Point(mid.x, currMax.y, currMax.z), currNode->_children[6]);
        traverseRecursive(callback, mid, currMax, currNode->_children[7]);
    }
};
