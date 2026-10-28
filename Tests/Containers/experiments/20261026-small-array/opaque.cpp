void* volatile array_escaped_pointer=nullptr;
__declspec(noinline) void array_escape(void*p){array_escaped_pointer=p;}
