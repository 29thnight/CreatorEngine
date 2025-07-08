#pragma once
#include "BTHeader.h"
#include "Core.Minimal.h"


namespace BT
{
	class NodeFactory : public Singleton<NodeFactory>
	{
	private:
		friend class Singleton;
		NodeFactory() = default;
		~NodeFactory() = default;

	public:
		using CreateNodeFunc = std::function<BTNode::NodePtr(const json&)>;

		void Register(const std::string& typeName, CreateNodeFunc func);
		BTNode::NodePtr Create(const std::string& typeName, const json& data = {});

		std::vector<std::string> GetReisteredKeys() const
		{
			std::vector<std::string> keys;
			keys.reserve(m_registry.size());
			for (const auto& pair : m_registry)
			{
				keys.push_back(pair.first);
			}
			return keys;
		}
		
	private :
		NodeFactory() = default;
		std::map<std::string, CreateNodeFunc> m_registry;
	};
} // namespace BT

static auto& BTNodeFactory = BT::NodeFactory::GetInstance();