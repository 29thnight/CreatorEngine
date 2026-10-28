from pathlib import Path

def optimize(s, append=True, constructor=2):
 if append:
  for view,unit in [('utf8_string_view','char'),('string_view','char16_t')]:
   start=s.index('  void append('+view+' v) {'); end=s.index('  void push_back('+unit+' c)',start)
   width='1' if unit=='char' else 'sizeof(char16_t)'; limit='SIZE_MAX - 1' if unit=='char' else 'max_size()'
   s=s[:start]+f'''  void append({view} v) {{
    const size_t count=v.size(), old=n_;
    if(count<=cap_-old) {{
      auto p=ptr(); const size_t total=old+count;
      n_=total;
      if(count) memmove(p+old,v.data(),count*{width});
      p[total]=0;
      return;
    }}
    if(count>{limit}-old) throw std::bad_alloc();
    auto source=reinterpret_cast<uintptr_t>(v.data()),base=reinterpret_cast<uintptr_t>(ptr());
    bool alias=source>=base && source-base<=old*{width};
    size_t offset=alias?(source-base)/{width}:0;
    size_t total=old+count; reserve(grown(total)); auto p=ptr();
    if(count) memmove(p+old,alias?p+offset:v.data(),count*{width});
    n_=total; p[total]=0;
  }}
'''+s[end:]
 if constructor:
  old='  explicit basic_utf16_string(string_view v,const Allocator& a=Allocator{}) :basic_utf16_string(a) { assign(v); }'
  short='if(v.size()) memcpy(data_.small,v.data(),v.size()*sizeof(char16_t));'
  if constructor==2:
   short='''size_t bytes=v.size()*sizeof(char16_t);
      auto p=reinterpret_cast<char*>(data_.small); auto source=reinterpret_cast<const char*>(v.data());
      if(bytes>=16) { memcpy(p,source,16); memcpy(p+bytes-8,source+bytes-8,8); }
      else if(bytes>=8) { memcpy(p,source,8); memcpy(p+bytes-8,source+bytes-8,8); }
      else if(bytes>=4) { memcpy(p,source,4); memcpy(p+bytes-4,source+bytes-4,4); }
      else if(bytes) data_.small[0]=v[0];'''
  new='''  explicit basic_utf16_string(string_view v,const Allocator& a=Allocator{}) :alloc_(a) {
    if(v.size()<=short_capacity) {
      '''+short+'''
      n_=v.size(); data_.small[n_]=0;
    } else {
      if(v.size()>max_size()) throw std::bad_alloc();
      auto p=static_cast<char16_t*>(alloc_.allocate((v.size()+1)*sizeof(char16_t),alignof(char16_t)));
      memcpy(p,v.data(),v.size()*sizeof(char16_t)); p[v.size()]=0;
      data_.large=p; n_=cap_=v.size();
    }
  }'''
  if old not in s: raise RuntimeError('Constructor changed')
  s=s.replace(old,new)
 return s

if __name__=='__main__':
 p=Path('../../Engine/Containers/modules/string.ixx');p.write_text(optimize(p.read_text(encoding='utf-8')),encoding='utf-8')
