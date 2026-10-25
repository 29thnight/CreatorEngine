#include "../../Engine/Utility_Framework/Reflection/TypeTrait.h"

bool InsertGuidFromPeer(HashedGuid guid)
{
    return TypeTrait::GUIDCreator::InsertGUID(guid);
}

void EraseGuidFromPeer(HashedGuid guid)
{
    TypeTrait::GUIDCreator::EraseGUID(guid);
}

HashedGuid MakeGuidFromPeer()
{
    return TypeTrait::GUIDCreator::MakeGUID();
}
