#pragma once
#include <stddef.h>
#include <stdint.h>
struct array_context {
 uint32_t page_faults=0;
 int64_t working_set_delta=0,private_delta=0;
 double sibling_busy=-1,core_busy=-1,performance=-1,dpc=-1,interrupt=-1;
};
void context_begin();
array_context context_end();
void context_print(const char*,size_t,size_t,const char*,const array_context&);
