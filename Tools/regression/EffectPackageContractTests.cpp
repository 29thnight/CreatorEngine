#include "../../Engine/RenderEngine/Effects/IEffectSystem.h"
#include <stdio.h>
#include <stdexcept>

namespace
{
int initialized{}, shutdown{}, destroyed{};
enum class Mode { Success, Fail, Throw } mode{};
class Fake final : public ce::effects::IEffectSystem
{
public:
    ~Fake() override { ++destroyed; }
    bool Initialize(std::string& error) override
    {
        ++initialized;
        if (mode == Mode::Throw) throw std::runtime_error("expected");
        if (mode == Mode::Fail) { error = "expected"; return false; }
        return true;
    }
    void Shutdown() noexcept override { ++shutdown; }
};
std::unique_ptr<ce::effects::IEffectSystem> Create() { return std::make_unique<Fake>(); }
std::unique_ptr<ce::effects::IEffectSystem> Null() { return {}; }
void Require(bool value) { if (!value) throw std::runtime_error("Effect contract failed"); }
}

int main()
{
    using namespace ce::effects;
    std::string error;
    { Session s; Require(s.Initialize(error) && !s.Get()); }
    Require(!RegisterFactory(nullptr) && RegisterFactory(&Create));
    Require(!RegisterFactory(&Create) && !RegisterFactory(&Null));
    Require(!UnregisterFactory(&Null) && RegisteredFactory() == &Create);
    { Session s; Require(s.Initialize(error) && s.Get()); Require(!s.Initialize(error)); s.Reset(); s.Reset(); }
    Require(initialized == 1 && shutdown == 1 && destroyed == 1);
    mode = Mode::Fail;
    { Session s; Require(!s.Initialize(error) && !s.Get() && error == "expected"); }
    Require(initialized == 2 && shutdown == 2 && destroyed == 2);
    mode = Mode::Throw;
    try { Session s; s.Initialize(error); Require(false); }
    catch (const std::runtime_error& e) { Require(std::string(e.what()) == "expected"); }
    Require(initialized == 3 && shutdown == 3 && destroyed == 3);
    Require(UnregisterFactory(&Create) && !UnregisterFactory(&Create));
    Require(RegisterFactory(&Null));
    { Session s; Require(!s.Initialize(error) && !s.Get()); }
    Require(UnregisterFactory(&Null));
    mode = Mode::Success;
    Require(RegisterFactory(&Create));
    { Session s; Require(s.Initialize(error)); }
    Require(UnregisterFactory(&Create));
    { Session s; Require(s.Initialize(error) && !s.Get()); }
    Require(initialized == 4 && shutdown == 4 && destroyed == 4);
    std::puts("EFFECT_PACKAGE_CONTRACT_OK absent duplicate failed-init throw null-factory repeated-session unregister");
}
