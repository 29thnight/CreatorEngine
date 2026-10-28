#pragma once
// Single-threaded experiment only: shared state avoids copying pool ownership
// when the original map copies its byte allocator for a pending index.
struct pool_control_allocator {
 struct slot{alignas(8) unsigned char data[16];};
 struct chunk{chunk* next;size_t used=0;slot cells[64];};
 struct link{link* next;};
 struct state{size_t refs=1;chunk* head=nullptr;link* free=nullptr;~state(){while(head){auto* next=head->next;head->~chunk();ce::heap_allocator{}.deallocate(head,sizeof(chunk),alignof(chunk));head=next;}}};
 state* owner=new state;
 pool_control_allocator()=default;
 pool_control_allocator(const pool_control_allocator& other)noexcept:owner(other.owner){++owner->refs;}
 pool_control_allocator& operator=(const pool_control_allocator& other)noexcept{if(owner!=other.owner){if(!--owner->refs)delete owner;owner=other.owner;++owner->refs;}return *this;}
 ~pool_control_allocator(){if(!--owner->refs)delete owner;}
 void* allocate(size_t bytes,size_t alignment){
  if(bytes!=16 || alignment>8)return ce::heap_allocator{}.allocate(bytes,alignment);
  if(owner->free){auto* p=owner->free;owner->free=p->next;return p;}
  if(!owner->head || owner->head->used==64){auto* p=ce::heap_allocator{}.allocate(sizeof(chunk),alignof(chunk));auto* c=::new(p)chunk;c->next=owner->head;owner->head=c;}
  return owner->head->cells[owner->head->used++].data;
 }
 void deallocate(void* p,size_t bytes,size_t alignment)noexcept{
  if(bytes!=16 || alignment>8){ce::heap_allocator{}.deallocate(p,bytes,alignment);return;}
  auto* node=::new(p)link{owner->free};owner->free=node;
 }
};
