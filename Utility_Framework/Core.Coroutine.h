#pragma once
#include "CoroutineHelper.h"
#include "ClassProperty.h"
#include "TimeSystem.h"

class CoroutineManager : public Singleton<CoroutineManager>
{
private:
	friend class Singleton;
	CoroutineManager() = default;
	~CoroutineManager() = default;
public:
    void StartCoroutine(Coroutine<> coroutine);
    void StartCoroutine(const std::string& alias, Coroutine<> coroutine);
    void StopAllCoroutines();
    void yield_Null();
    void yield_StartCoroutine();
    void yield_WaitForSeconds();
    void yield_WaitForFixedUpdate();
    void yield_WaitForEndOfFrame();
    void yield_OtherEvent();

private:
    struct CoroutineWrapper : public LinkProperty<CoroutineWrapper>
    {
        Coroutine<> coroutine;
        std::string aliasName;
        std::function<void()> onDone;
        bool markedForDelete = false;

        CoroutineWrapper(Coroutine<> c)
            : coroutine(std::move(c)), LinkProperty(this)
        {
        }
    };

    LinkedList<CoroutineWrapper> StartCoroutineQueue;
    LinkedList<CoroutineWrapper> waitForFixedUpdateQueue;
    LinkedList<CoroutineWrapper> nullQueue;
    LinkedList<CoroutineWrapper> waitForSecondsQueue;
    LinkedList<CoroutineWrapper> waitForFramesQueue;
    LinkedList<CoroutineWrapper> waitUntilQueue;
    LinkedList<CoroutineWrapper> waitForSignalQueue;
    LinkedList<CoroutineWrapper> waitForEndOfFrameQueue;

	std::unordered_map<std::string, CoroutineWrapper*> coroutineMap;
    std::vector<CoroutineWrapper*> activeCoroutines;

    void ProcessQueue(LinkedList<CoroutineWrapper>& queue);
   
};
static auto& CoroutineManagers = CoroutineManager::GetInstance();

inline void StartCoroutine(Coroutine<> coroutine)
{
	CoroutineManagers->StartCoroutine(std::move(coroutine));
}

inline void StartCoroutine(const std::string& alias, Coroutine<> coroutine)
{
	CoroutineManagers->StartCoroutine(alias, std::move(coroutine));
}

inline void StopAllCoroutines()
{
	CoroutineManagers->StopAllCoroutines();
}

inline YieldInstruction WaitForSeconds(float sec)
{
    return YieldInstruction{ YieldInstructionType::WaitForSeconds, sec, 0 };
}

inline YieldInstruction WaitForFrames(int frames)
{
    return YieldInstruction{ YieldInstructionType::WaitForFrames, 0.0f, frames };
}

inline YieldInstruction WaitUntil(std::function<bool()> func)
{
    YieldInstruction inst;
    inst.type = YieldInstructionType::WaitUntil;
    inst.condition = std::move(func);
    return inst;
}

inline YieldInstruction WaitForSignal(bool* signalFlag)
{
    YieldInstruction inst;
    inst.type = YieldInstructionType::WaitForSignal;
    inst.signal = signalFlag;
    return inst;
}

inline YieldInstruction WaitForFixedUpdate()
{
    return YieldInstruction{ YieldInstructionType::WaitForFixedUpdate };
}

inline YieldInstruction WaitForEndOfFrame()
{
    return YieldInstruction{ YieldInstructionType::WaitForEndOfFrame };
}
