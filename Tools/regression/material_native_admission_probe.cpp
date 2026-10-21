#include "MaterialGraphNativeAdmission.h"
#include <iostream>

int main() try
{
    // The first eight requests remain pending for many frames. Other passes and
    // another material must still be admitted, with at most eight new polls/frame.
    std::array<int,40> requests{};
    std::array<unsigned,40> firstPoll{};
    material_graph::detail::NativeAdmission first,second;
    for(unsigned frame=1;frame<=5;++frame) {
        unsigned remaining=8,newPolls=0;
        for(unsigned i=0;i<requests.size();++i) {
            auto& admission=i<20?first:second;
            if(admission.CanPoll(&requests[i],remaining,true) && !firstPoll[i]) {
                firstPoll[i]=frame;++newPolls;
            }
        }
        if(newPolls!=8 || remaining)throw std::runtime_error("Bounded tail admission failed");
    }
    if(firstPoll.back()!=5)throw std::runtime_error("A pending head starved another material");
    unsigned remaining=0;
    if(!first.CanPoll(&requests[0],remaining,true) || remaining)
        throw std::runtime_error("Pending completion could not be polled with exhausted admission budget");
    material_graph::detail::NativeAdmission replacement;
    if(replacement.CanPoll(&requests[0],remaining,true))
        throw std::runtime_error("Replacement generation inherited admission from its predecessor");
    remaining=1;
    if(!replacement.CanPoll(&requests[0],remaining,false) ||
       replacement.CanPoll(&requests[0],remaining,false))
        throw std::runtime_error("Blocking backend no longer budgets every call");
    std::cout<<"MATERIAL_NATIVE_ADMISSION_OK requests=40 budget=8 frames=5 pendingHead=true replacement=true blocking=true\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
