#include "../../Engine/SceneRuntime/SceneObservation.h"
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace Runtime;
static unsigned checks;
static void require(bool ok){++checks;if(!ok)throw std::runtime_error("observation check "+std::to_string(checks));}
struct Listener : ISceneEntityObserver, IComponentWriteObserver {
    unsigned entities{},writes{}; std::function<void()> action;
    void OnEntityChanged(const SceneEntityEvent&) override {++entities;if(action)action();}
    void OnComponentWritten(const ComponentWriteEvent&) override {++writes;if(action)action();}
};
int main(){try{
    SceneObservation bus;
    auto a=std::make_shared<Listener>(),b=std::make_shared<Listener>();
    auto subscribe=[&](auto p){return bus.Subscribe(std::static_pointer_cast<IComponentWriteObserver>(p));};
    auto first=subscribe(a);require(first!=0);require(subscribe(a)==first);
    auto second=subscribe(b);
    a->action=[&]{bus.Unsubscribe(second);};
    bus.Publish(ComponentWriteEvent{});require(a->writes==1&&b->writes==0);
    a->action={};bus.Unsubscribe(first);require(!bus.HasWriteObservers());
    auto token=subscribe(b);require(token>second);
    std::thread worker([&]{bus.Publish(ComponentWriteEvent{});});worker.join();
    require(b->writes==0);bus.Drain();require(b->writes==1);
    b->action=[] {throw std::runtime_error("adapter fault");};
    bus.Publish(ComponentWriteEvent{});require(bus.Metrics().failures==1);
    b->action={};
    std::thread flood([&]{for(unsigned i=0;i<300;++i)bus.Publish(ComponentWriteEvent{});});flood.join();
    require(bus.Metrics().lost==44);bus.Drain();require(b->writes==258);
    auto c=std::make_shared<Listener>();auto ct=subscribe(c);
    c->action=[&]{bus.Unsubscribe(ct);bus.Publish(ComponentWriteEvent{});};
    bus.Publish(ComponentWriteEvent{});require(c->writes==1&&b->writes==260);
    std::weak_ptr<Listener> weak=b;b.reset();require(!weak.expired());bus.Unsubscribe(token);require(weak.expired());
    bus.Clear();require(!bus.HasWriteObservers());bus.Unsubscribe(first);
    auto e=std::make_shared<Listener>();auto et=bus.Subscribe(std::static_pointer_cast<ISceneEntityObserver>(e));
    require(et>ct);bus.Publish(SceneEntityEvent{42,{1,3,1},{2,5,2},EntityChange::Attached});require(e->entities==1);
    bool wrongThread=false;std::thread wrong([&]{try{bus.Unsubscribe(et);}catch(const std::logic_error&){wrongThread=true;}});wrong.join();require(wrongThread);
    bus.Clear();std::vector<std::shared_ptr<Listener>> owners;
    for(unsigned i=0;i<16;++i){owners.push_back(std::make_shared<Listener>());require(subscribe(owners.back())!=0);}
    require(subscribe(std::make_shared<Listener>())==0);
    bus.Clear();auto loop=std::make_shared<Listener>();subscribe(loop);loop->action=[&]{bus.Publish(ComponentWriteEvent{});};
    auto lost=bus.Metrics().lost;bus.Publish(ComponentWriteEvent{});require(loop->writes==256&&bus.Metrics().lost==lost+1);
    std::cout<<"SCENE_OBSERVATION_OK checks="<<checks<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
