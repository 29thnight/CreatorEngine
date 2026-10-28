import Creator.Containers;
ce::memory_resource* resource_b(){return &ce::default_resource();}
bool string_b(const ce::utf8_string& s){return s==ce::utf8_string_view("module cross translation unit heap string");}
bool register_b(){try{ce::lock_free_queue<int,1>::register_current_thread();}catch(const ce::queue_operation_limit&){return true;}return false;}
