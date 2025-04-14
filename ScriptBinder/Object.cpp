#include "Object.h"
#include "SceneManager.h"

void Object::Destroy()
{
    if (m_destroyMark)
    {
        return;
    }
    m_destroyMark = true;
    TypeTrait::GUIDCreator::EraseGUID(m_instanceID);
}

void Object::SetDontDestroyOnLoad(Object* objPtr)
{
    if (objPtr == nullptr)
    {
        return;
    }

    if (objPtr->m_dontDestroyOnLoad)
    {
        return;
    }
    m_dontDestroyOnLoad = true;
    SceneManagers->AddDontDestroyOnLoad(objPtr);
}
