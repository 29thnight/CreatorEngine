#include "Core.Coroutine.h"
#include "TimeSystem.h"

void CoroutineManager::StartCoroutine(Coroutine<> coroutine)
{
    CoroutineWrapper* wrapper = new CoroutineWrapper(std::move(coroutine));
    StartCoroutineQueue.Link(wrapper);
    activeCoroutines.push_back(wrapper);
}

void CoroutineManager::StartCoroutine(const std::string& alias, Coroutine<> coroutine)
{
	CoroutineWrapper* wrapper = new CoroutineWrapper(std::move(coroutine));
	wrapper->aliasName = alias;
	StartCoroutineQueue.Link(wrapper);
	activeCoroutines.push_back(wrapper);
}

void CoroutineManager::StopAllCoroutines()
{
    for (CoroutineWrapper* wrapper : activeCoroutines)
    {
        wrapper->markedForDelete = true;
    }
}

void CoroutineManager::yield_Null()
{
    ProcessQueue(nullQueue);

    for (auto it = activeCoroutines.begin(); it != activeCoroutines.end(); ) {
        CoroutineWrapper* wrapper = *it;
        if (wrapper->markedForDelete)
        {
            nullQueue.Unlink(wrapper);
            waitForFixedUpdateQueue.Unlink(wrapper);
            waitForEndOfFrameQueue.Unlink(wrapper);
            it = activeCoroutines.erase(it);
			coroutineMap.erase(wrapper->aliasName);
            delete wrapper;
        }
        else
        {
            ++it;
        }
    }
}

void CoroutineManager::yield_StartCoroutine()
{
    ProcessQueue(StartCoroutineQueue);
}

void CoroutineManager::yield_WaitForSeconds()
{
    ProcessQueue(waitForSecondsQueue);
}

void CoroutineManager::yield_WaitForFixedUpdate()
{
    ProcessQueue(waitForFixedUpdateQueue);
}

void CoroutineManager::yield_WaitForEndOfFrame()
{
    ProcessQueue(waitForEndOfFrameQueue);
}

void CoroutineManager::yield_OtherEvent()
{
    ProcessQueue(waitForFramesQueue);
    ProcessQueue(waitUntilQueue);
    ProcessQueue(waitForSignalQueue);
}

void CoroutineManager::ProcessQueue(LinkedList<CoroutineWrapper>& queue)
{
    float deltaTime = Time != nullptr ? Time->GetElapsedSeconds() : 0.16f;
    for (auto it = queue.begin(); it != queue.end(); )
    {
        CoroutineWrapper* wrapper = &(*it);
        ++it;

        if (wrapper->markedForDelete)
            continue;

        if (wrapper->coroutine.is_done())
        {
            if (wrapper->onDone) wrapper->onDone();
            queue.Unlink(wrapper);
            wrapper->markedForDelete = true;
            continue;
        }

        if (wrapper->coroutine.current().Tick(deltaTime))
        {
            wrapper->coroutine.resume();
        }

        if (!wrapper->coroutine.is_done())
        {
            YieldInstructionType type = wrapper->coroutine.current().type;
            queue.Unlink(wrapper);

            switch (type)
            {
            case YieldInstructionType::None:
                StartCoroutineQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitForFixedUpdate:
                waitForFixedUpdateQueue.Link(wrapper);
                break;
            case YieldInstructionType::Null:
                nullQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitForSeconds:
                waitForSecondsQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitForFrames:
                waitForFramesQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitUntil:
                waitUntilQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitForSignal:
                waitForSignalQueue.Link(wrapper);
                break;
            case YieldInstructionType::WaitForEndOfFrame:
                waitForEndOfFrameQueue.Link(wrapper);
                break;
            default:
                nullQueue.Link(wrapper);
                break;
            }
        }
        else
        {
            if (wrapper->onDone) wrapper->onDone();
            queue.Unlink(wrapper);
            wrapper->markedForDelete = true;
        }
    }
}
