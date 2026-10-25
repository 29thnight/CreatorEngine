#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../Engine/RenderEngine/Render/Scene/PreparedViewInputPool.h"

#include <cstdio>
#include <vector>

namespace
{
    unsigned failures{};
    unsigned constructions{};
    struct Input
    {
        std::vector<int> lights;
        unsigned view{};
        Input() { ++constructions; }
    };

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }
} // namespace

int main()
{
    PreparedViewInputPool<Input, 2> scene;
    PreparedViewInputPool<Input, 2> game;
    auto sceneWrite = scene.Acquire();
    sceneWrite->view = 1;
    sceneWrite->lights = {10, 20};
    const auto firstAddress = &*sceneWrite;
    own::shared_owner<const Input> sceneGraph = std::move(sceneWrite);
    auto gameWrite = game.Acquire();
    gameWrite->view = 2;
    gameWrite->lights = {30};
    own::shared_owner<const Input> gameGraph = std::move(gameWrite);
    Expect(sceneGraph->view == 1 && sceneGraph->lights == std::vector<int>({10, 20}),
           "preparing Game leaves Scene's published input unchanged");

    auto next = scene.Acquire();
    Expect(next && &*next != firstAddress, "in-flight input cannot be acquired for mutation");
    next->lights = {40, 50, 60};
    own::shared_owner<const Input> nextGraph = std::move(next);
    Expect(!scene.Acquire(), "capacity exhaustion defers preparation without overwriting a live graph");
    Expect(sceneGraph->lights.size() == 2 && sceneGraph->lights[0] == 10,
           "exhaustion preserves the first graph's exact snapshot");
    const auto warmedConstructions = constructions;
    const auto retainedCapacity = sceneGraph->lights.capacity();
    sceneGraph.reset();
    auto recycled = scene.Acquire();
    Expect(recycled && &*recycled == firstAddress, "last consumer release makes the exact slot reusable");
    Expect(constructions == warmedConstructions && recycled->lights.capacity() == retainedCapacity,
           "recycling preserves allocated storage without constructing another payload");
    Expect(gameGraph->view == 2 && gameGraph->lights == std::vector<int>({30}),
           "Scene recycling cannot change the other view");

    own::shared_owner<const Input> survivingGraph;
    {
        PreparedViewInputPool<Input, 1> retiring;
        auto input = retiring.Acquire();
        input->lights = {99};
        survivingGraph = std::move(input);
    }
    Expect(survivingGraph->lights[0] == 99, "a retained graph owns its input after the pool retires");
    std::printf("PreparedViewInputPool: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
