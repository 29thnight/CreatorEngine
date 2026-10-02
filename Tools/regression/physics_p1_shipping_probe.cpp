#include "../../Engine/Physics/PhysicsScene.h"
#include <iostream>
int main()
{
    auto scene = ce::physics::PhysicsScene::create(ce::physics::scene_config{{0, -9.81f, 0}, 1});
    if (!scene || !(*scene)->begin_step(1.f / 60) || !(*scene)->finish_step())
        return 1;
    const auto status = (*scene)->status();
    if (status.sdk_errors || !status.completed_tasks || status.completed_tasks != status.submitted_tasks)
        return 2;
    scene->reset();
    // This executable links no diagnostics library or source. Any profiling call
    // surviving CE_SHIPPING=1 is therefore a link failure.
    std::cout << "{\"result\":\"PHYSICS_P1_OK\",\"checks\":4,\"sdk_tasks\":" << status.completed_tasks << "}\n";
}
