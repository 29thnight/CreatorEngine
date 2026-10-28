#include <thread>
import Creator.Containers;
ce::memory_resource* resource_a();ce::memory_resource* resource_b();
ce::utf8_string string_a();bool string_b(const ce::utf8_string&);
void register_a();bool register_b();void bounds_a();
int main(){
 if(resource_a()!=resource_b())return 1;
 auto s=string_a();if(!string_b(s))return 2;
 bool caught=false;try{bounds_a();}catch(const ce::bounds_error&){caught=true;}
 if(!caught)return 3;
 register_a();bool shared=false;std::thread worker([&]{shared=register_b();});worker.join();
 return shared?0:4;
}
