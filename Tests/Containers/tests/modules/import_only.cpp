import Creator.Containers;
#ifdef CE_FORCEINLINE
#error Implementation macros must not leak through a module import
#endif
struct counted {
 static inline int alive=0, copies=-1;
 int value=0;
 counted(){++alive;}
 counted(const counted& other):value(other.value){if(copies==0)throw 1;if(copies>0)--copies;++alive;}
 counted(counted&& other)noexcept:value(other.value){++alive;}
 ~counted()noexcept{--alive;}
};
int main(){
 constexpr ce::fixed_array<int,3> fixed{1,2,3};
 static_assert(fixed.size()==3&&fixed[2]==3);
 static_assert(sizeof(fixed)==sizeof(int)*3);
 static_assert(ce::fixed_array<int,0>{}.empty());
 ce::fixed_array<int,3> sorted{3,1,2};
 auto [firstFixed,secondFixed,thirdFixed]=sorted;
 if(firstFixed!=3||secondFixed!=1||thirdFixed!=2)return 23;
 // iterator_traits is checked by the separate migration consumer.
 if(*sorted.rbegin()!=2 || sorted.rend()-sorted.rbegin()!=3)return 21;
 sorted.fill(4);if(sorted.front()!=4||sorted.back()!=4)return 22;
 ce::dynamic_array<int> empty; empty.assign(empty.begin(),empty.end());
 if(!empty.empty())return 14;
 ce::dynamic_array<int> assigned(4);
 for(int i=0;i<4;++i)assigned[i]=i+1;
 assigned.assign(assigned.begin()+1,assigned.end());
 if(assigned.size()!=3||assigned.front()!=2||assigned.back()!=4)return 10;
 {
  ce::dynamic_array<counted> source(3),dest(1);
  dest[0].value=42;counted::copies=1;
  try{dest.assign(source.begin(),source.end());return 11;}catch(int){}
  if(dest.size()!=1||dest[0].value!=42||counted::alive!=4)return 12;
  counted::copies=-1;
 }
 if(counted::alive!=0)return 13;
 ce::dynamic_array<int> edits{1,2,3};
 edits.insert(edits.begin()+1, edits.begin(), edits.end());
 if(!(edits==ce::dynamic_array<int>{1,1,2,3,2,3}))return 15;
 edits.erase(edits.begin()+1,edits.begin()+4);
 edits.erase(decltype(edits)::size_type(0)); edits.insert(edits.begin(),decltype(edits)::size_type(2),9);
 if(!(edits==ce::dynamic_array<int>{9,9,2,3}))return 16;
 ce::dynamic_array<int> append{4,5,6};
 append.shrink_to_fit();
 append.insert(append.end(),append.begin(),append.end());
 if(!(append==ce::dynamic_array<int>{4,5,6,4,5,6}))return 19;
 append.reserve(100);auto* storage=append.data();
 append.insert(append.end(),append.begin()+1,append.begin()+3);
 if(append.data()!=storage||append.size()!=8||append[6]!=5||append[7]!=6)return 20;
 edits.reserve(100); edits.shrink_to_fit();
 if(edits.capacity()!=edits.size())return 17;
 edits={};edits.shrink_to_fit();if(edits.data()!=nullptr)return 18;
 ce::dynamic_array<int> values; values.push_back(7);
 ce::utf8_string text("module");
 ce::flat_hash_map<int> map;map.try_emplace(4,9);
 ce::bounded_mpsc_queue<int,7> queue;queue.try_push(11);
 int out=0;
 return values[0]!=7 || text.size()!=6 || *map.find(4)!=9 || !queue.try_pop(out) || out!=11;
}
