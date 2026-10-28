#include <ce/concurrent_queue.h>
struct value {
  int x = 0;
  explicit value(int v = 0) : x(v) {}
  value(const value &) = delete;
  value(value &&o) noexcept : x(o.x) { o.x = 0; }
  value &operator=(value &&o) noexcept {
    x = o.x;
    o.x = 0;
    return *this;
  }
  ~value() noexcept { x = 0; }
};
int main() {
  ce::concurrent_queue<value> q;
  q.emplace(7);
  value out;
  if (!q.try_pop(out) || out.x != 7)
    return 1;
  ce::bounded_spsc_ring<value, 2> r;
  if (!r.try_emplace(9) || !r.try_pop(out) || out.x != 9)
    return 2;
  return 0;
}
