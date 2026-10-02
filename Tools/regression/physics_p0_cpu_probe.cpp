#include <PxPhysicsAPI.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace physx;
template<class T> struct release_px { void operator()(T* p) const { if(p) p->release(); } };
template<class T> using owner=std::unique_ptr<T,release_px<T>>;
class errors final : public PxErrorCallback {
public:
    int count=0;
    void reportError(PxErrorCode::Enum, const char* text, const char*, int) override {
        ++count; std::cerr<<text<<'\n';
    }
};
int main() {
    PxDefaultAllocator allocator; errors error;
    owner<PxFoundation> foundation{PxCreateFoundation(PX_PHYSICS_VERSION,allocator,error)};
    if(!foundation) return 1;
    owner<PxPhysics> physics{PxCreatePhysics(PX_PHYSICS_VERSION,*foundation,PxTolerancesScale(),false,nullptr)};
    if(!physics) return 2;
    owner<PxDefaultCpuDispatcher> dispatcher{PxDefaultCpuDispatcherCreate(4)};
    if(!dispatcher) return 3;
    owner<PxMaterial> material{physics->createMaterial(0.5f,0.4f,0.1f)};
    if(!material) return 4;
    for(int bodies : {100,1000,10000}) for(int repetition=0;repetition<3;++repetition) {
        PxSceneDesc desc(physics->getTolerancesScale()); desc.gravity={0,-9.81f,0};
        desc.cpuDispatcher=dispatcher.get(); desc.filterShader=PxDefaultSimulationFilterShader;
        desc.flags|=PxSceneFlag::eENABLE_ACTIVE_ACTORS;
        owner<PxScene> scene{physics->createScene(desc)}; if(!scene) return 5;
        std::vector<owner<PxRigidActor>> actors;
        auto* floor=PxCreateStatic(*physics,PxTransform(PxVec3(0,-0.5f,0)),PxBoxGeometry(200,0.5f,200),*material);
        if(!floor) return 6; actors.emplace_back(floor); scene->addActor(*floor);
        int width=int(std::ceil(std::sqrt(float(bodies))));
        for(int i=0;i<bodies;++i) {
            auto* body=PxCreateDynamic(*physics,PxTransform(PxVec3(float(i%width)*1.5f-width*0.75f,
                8.0f+float(i%7)*0.2f,float(i/width)*1.5f-width*0.75f)),PxBoxGeometry(0.5f,0.5f,0.5f),*material,1.0f);
            if(!body) return 7;
            body->setSleepThreshold(0); actors.emplace_back(body); scene->addActor(*body);
        }
        std::vector<double> samples; double submit_us=0,wait_us=0;
        for(int tick=0;tick<240;++tick) {
            auto start=std::chrono::steady_clock::now(); scene->simulate(1.0f/60);
            auto submitted=std::chrono::steady_clock::now();
            PxU32 failure=0; if(!scene->fetchResults(true,&failure)||failure) return 8;
            auto done=std::chrono::steady_clock::now();
            if(tick>=60) {
                samples.push_back(std::chrono::duration<double,std::micro>(done-start).count());
                submit_us+=std::chrono::duration<double,std::micro>(submitted-start).count();
                wait_us+=std::chrono::duration<double,std::micro>(done-submitted).count();
            }
        }
        double average=0;for(double x:samples) average+=x;average/=samples.size();
        std::sort(samples.begin(),samples.end());
        auto* body=static_cast<PxRigidDynamic*>(actors[1].get());float y=body->getGlobalPose().p.y;
        if(!std::isfinite(y)||y<0.45f||y>0.7f) return 9;
        std::cout<<"{\"kind\":\"sdk_cpu_reference\",\"physx\":\"5.5.0\",\"bodies\":"<<bodies
            <<",\"repetition\":"<<repetition<<",\"workers\":4,\"warmup\":60,\"samples\":180,\"fixed_dt\":0.0166666667,\"sleep_disabled\":true,\"mean_us\":"
            <<average<<",\"p99_us\":"<<samples[std::size_t(std::ceil(samples.size()*0.99))-1]
            <<",\"submit_mean_us\":"<<submit_us/samples.size()<<",\"fetch_mean_us\":"<<wait_us/samples.size()
            <<",\"floor_contact_y\":"<<y<<"}\n";
    }
    return error.count?10:0;
}
