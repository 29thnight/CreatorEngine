#pragma once

enum class EResourceType
{
	CONVRX_MESH,
	TRIANGLE_MESH,
	HEIGHT_FIELD,

	END
};



class ResourceBase
{
public:
	ResourceBase(const EResourceType& type);
	virtual ~ResourceBase();

	inline const EResourceType& GetResourceType() const { return m_resourceType; }

private:
	EResourceType m_resourceType;
};

