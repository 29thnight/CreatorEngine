#pragma once
#include "BTHeader.h"
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"

namespace BT
{
	class NodeFactory : public DLLCore::Singleton<NodeFactory>
	{
	private:
		friend class DLLCore::Singleton<NodeFactory>;
		NodeFactory() = default;
		~NodeFactory() = default;

	public:
		using CreateNodeFunc = std::function<BTNode::NodePtr()>;

		void Register(const std::string& typeName, CreateNodeFunc func);
		BTNode::NodePtr Create(const std::string& typeName);

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
		
		bool IsRegistered(const std::string& typeName) const
		{
			return m_registry.find(typeName) != m_registry.end();
		}

		void Clear()
		{
			m_registry.clear();
		}

	private :
		std::map<std::string, CreateNodeFunc> m_registry;
	};
} // namespace BT

static auto BTNodeFactory = BT::NodeFactory::GetInstance();