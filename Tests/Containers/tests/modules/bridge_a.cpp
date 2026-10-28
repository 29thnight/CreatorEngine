import Creator.Containers;
ce::memory_resource* resource_a(){return &ce::default_resource();}
ce::utf8_string string_a(){return ce::utf8_string("module cross translation unit heap string");}
void register_a(){ce::lock_free_queue<int,1>::register_current_thread();}
void bounds_a(){ce::dynamic_array<int> a; (void)a.at(0);}
