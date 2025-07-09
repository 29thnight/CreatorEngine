#include "NodeFactory.h"

using namespace BT;
    void NodeFactory::Register(const std::string& typeName, CreateNodeFunc func)
    {
		m_registry[typeName] = func;
    }

    BTNode::NodePtr NodeFactory::Create(const std::string& typeName, const json& data)
    {
        auto it = m_registry.find(typeName);
        if (it != m_registry.end()) {
            return it->second(data); // Call the registered function to create the node
        }
		Debug->LogError("NodeFactory: Type not found: " + typeName);
		return nullptr; // Return nullptr if type not found
    }
