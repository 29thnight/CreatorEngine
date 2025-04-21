#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
//old BT
class Node
{
public:
	virtual bool run(float tick) = 0;
	virtual ~Node() = default;
};

enum class ParallelPolicy
{
	ALL_SUCCESS,
	ANY_SUCCESS,
};

//조건 노드
class Condition : public Node
{
private:
	std::function<bool(float)> condition;
	std::vector<std::shared_ptr<Node>> children;
public:
	explicit Condition(std::function<bool(float)> condition) : condition(condition) {}
	bool run(float tick) override 
	{
		if(condition(tick))
		{
			for (auto& child : children)
			{
				if (!child->run(tick))
				{
					return false;
				}
			}
			return true;
		}
	}
};

//행동 노드
class Action : public Node
{
private:
	std::function<void(float)> action;
public:
	explicit Action(std::function<void(float)> action) : action(action) {}
	bool run(float tick) override 
	{
		action(tick);
		return true;
	}
};

//선택
class Selector : public Node
{
private:
	std::vector<std::shared_ptr<Node>> children;
public:
	explicit Selector(std::vector<std::shared_ptr<Node>> children) : children(std::move(children)) {}
	bool run(float tick) override 
	{
		for (auto& child : children) 
		{
			if (child->run(tick)) 
			{
				return true;
			}
		}
		return false;
	}
};

//순차
class Sequence : public Node
{
private:
	std::vector<std::shared_ptr<Node>> children;
public:
	explicit Sequence(std::vector<std::shared_ptr<Node>> children) : children(std::move(children)) {}
	bool run(float tick) override {
		for (auto& child : children) 
		{
			if (!child->run(tick)) 
			{
				return false;
			}
		}
		return true;
	}
};

//병렬
class Parallel : public Node
{
private:
	std::vector<std::shared_ptr<Node>> children;
	ParallelPolicy policy;
public:
	explicit Parallel(std::vector<std::shared_ptr<Node>> children, ParallelPolicy policy) : children(std::move(children)), policy(policy) {}
	bool run(float tick) override 
	{
		switch (policy)
		{
		case ParallelPolicy::ALL_SUCCESS:
			for (auto& child : children) 
			{
				if (!child->run(tick)) 
				{
					return false;
				}
			}
			return true;
		case ParallelPolicy::ANY_SUCCESS:
			for (auto& child : children) 
			{
				if (child->run(tick)) 
				{
					return true;
				}
			}
			return false;
		}
		return false;
	}
};

class BaseBehaviorTree
{
public:
	virtual ~BaseBehaviorTree() = default;
	virtual void buildTree() = 0;
	bool runTree(float tick) 
	{
		if (root && root->run(tick))
		{
			//std::cout << "Success" << std::endl;
			return true;
		}
		else
		{
			//std::cout << "Failure" << std::endl;
			return false;
		}
	}

protected:
	std::shared_ptr<Node> root;
};